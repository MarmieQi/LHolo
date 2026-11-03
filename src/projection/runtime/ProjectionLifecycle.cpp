// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/runtime/ProjectionLifecycle.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/mesh/ProjectionMeshWorker.h"
#include "projection/runtime/ProjectionProgress.h"
#include "projection/core/ProjectionState.h"
#include "projection/section/ProjectionSectionStateStore.h"
#include "projection/runtime/ProjectionWorldEvents.h"
#include "structure/StructureLoader.h"

#include <atomic>
#include <cstddef>
#include <map>
#include <tuple>
#include <utility>

#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActorType.h"

namespace lholo::projection::detail {
namespace {

std::atomic_uint64_t sProjectionActivationGeneration{};

enum class ProjectionReleaseScope : unsigned char {
    Dimension,
    World,
};

void releaseProjectionState(ProjectionState& state, ProjectionReleaseScope scope) {
    // Keep teardown ordering in one place: workers may retain dimension-owned
    // snapshots, so they must finish before listeners and state are released.
    stopMeshWorker();
    if (scope == ProjectionReleaseScope::Dimension) {
        detachProjectionDimensionEvents();
    } else {
        detachProjectionWorldEvents();
    }
    resetPublishedBuildProgress();
    state = ProjectionState{};
}

bool resolveTerrainTexture(IClientInstance& client, ProjectionState& state) {
    auto* levelRenderer = client.getLevelRenderer();
    if (!levelRenderer) return false;
    auto const& atlasTexture = levelRenderer->mAtlasTexture.get();
    auto const& atlasData = atlasTexture.mClientTexture;
    if (!atlasData || atlasData->mIsMissingTexture == IsMissingTexture::Yes) return false;
    state.terrainTexture.emplace(atlasTexture);
    state.terrainTextureVariant.emplace(*state.terrainTexture);
    return true;
}

} // namespace

bool prepareProjectionState(
    ProjectionState&                              state,
    BaseActorRenderContext&                       renderContext,
    std::shared_ptr<structure::LoadedStructure const> loaded
) {
    auto& client = renderContext.mClientInstance;
    auto* player = client.getLocalPlayer();
    if (!player || !loaded || (loaded->renderBlocks.empty() && !loaded->composed)) return false;

    state.client = &client;
    state.level = &player->getLevel();
    state.dimension = &player->getDimension();
    state.dimensionId = static_cast<int>(player->getDimensionId());
    state.structure = std::move(loaded);
    state.structureGeneration = state.structure->generation;
    state.activationGeneration = sProjectionActivationGeneration.fetch_add(
        1,
        std::memory_order_relaxed
    ) + 1;
    state.blockTessellator = std::make_unique<BlockTessellator>(
        &player->getDimensionBlockSource()
    );
    state.initialCorrectionScan.reset(state.structure->renderBlocks.size());
    state.progressCorrect.resize(state.structure->renderBlocks.size(), 0);
    state.progressErrorKind.resize(state.structure->renderBlocks.size(), 0);
    std::unordered_set<Block const *> actorBlocks;
    state.structure->renderBlocks.forEachStoredValue([&](auto const &cell) {
        if (cell.block && cell.block->getBlockType().getBlockEntityType() != BlockActorType::Undefined)
            actorBlocks.insert(cell.block);
    });
    state.fileProgress.total = state.fileProgress.visibleTotal = state.structure->renderBlocks.size();
    // Force the first render pass to build the transformed virtual-world lookup.
    state.cachedRotation = -1;
    state.cachedMirror = -1;

    std::vector<Vec3> centers;
    for (std::size_t index = 0; index < state.structure->renderBlocks.size(); ++index) {
        auto const& entry = state.structure->renderBlocks[index];
        if (actorBlocks.contains(entry.block))
            state.blockActorIndices.push_back(index);
        auto const key = std::tuple{entry.x / 16, entry.y / 16, entry.z / 16};
        auto [found, inserted] = state.localSectionIndices.try_emplace(
            key, state.sectionBlockIndices.size()
        );
        if (inserted) {
            state.sectionBlockIndices.emplace_back();
            state.localSectionKeys.push_back(key);
            auto const [sx, sy, sz] = key;
            centers.emplace_back(
                static_cast<float>(sx * 16 + 8),
                static_cast<float>(sy * 16 + 8),
                static_cast<float>(sz * 16 + 8)
            );
        }
        state.sectionBlockIndices[found->second].push_back(index);
    }
    state.correctionStates.initialize(state.sectionBlockIndices, state.structure->renderBlocks.size());
    state.correctionIndex.resize(state.sectionBlockIndices.size());
    for (std::size_t section = 0; section < state.sectionBlockIndices.size(); ++section) {
        std::vector<std::uint16_t> slots;
        slots.reserve(state.sectionBlockIndices[section].size());
        for (auto index : state.sectionBlockIndices[section]) {
            auto const& cell = state.structure->renderBlocks[index];
            slots.push_back(correctionCellSlot(cell.x, cell.y, cell.z));
        }
        state.correctionIndex[section].initialize(std::move(slots));
    }
    state.correctionScan.reset(state.sectionBlockIndices);
    initializeSectionStates(state.sections, centers);
    state.warningFillSectionMeshes.resize(state.sectionBlockIndices.size());
    state.correctionOutlineSectionMeshes.resize(state.sectionBlockIndices.size());
    state.wrongFillSectionMeshes.resize(state.sectionBlockIndices.size());
    state.wrongOutlineSectionMeshes.resize(state.sectionBlockIndices.size());
    state.blockEntityPlaceholderSectionMeshes.resize(state.sectionBlockIndices.size());
    state.sectionExtraBlockPositions.resize(state.sectionBlockIndices.size());
    return resolveTerrainTexture(client, state);
}

void resetProjectionState(ProjectionState& state) {
    releaseProjectionState(state, ProjectionReleaseScope::World);
}

void suspendProjectionState(ProjectionState& state) {
    // A dimension owns its BlockSource, but the Level remains the identity of
    // the current world. Retaining only its listener preserves authoritative
    // world-exit cleanup while all dimension-owned runtime data is released.
    releaseProjectionState(state, ProjectionReleaseScope::Dimension);
}

ProjectionContextStatus classifyProjectionContext(
    ProjectionState const& state,
    IClientInstance&       client,
    Actor*                 player
) {
    // The local player can briefly disappear while a dimension is loading.
    // Level destruction remains the authoritative world-exit signal, so a
    // missing player pauses rendering instead of discarding the projection.
    if (!player) return ProjectionContextStatus::Unavailable;
    if (state.client != &client || state.level != &player->getLevel()) {
        return ProjectionContextStatus::WorldChanged;
    }
    if (state.dimension != &player->getDimension()) {
        return ProjectionContextStatus::DimensionChanged;
    }
    return ProjectionContextStatus::Current;
}

} // namespace lholo::projection::detail
