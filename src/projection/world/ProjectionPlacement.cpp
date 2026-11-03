// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/world/ProjectionPlacement.h"

#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionState.h"
#include "projection/world/ProjectionVirtualWorld.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/client/renderer/blockactor/BlockActorRenderDispatcher.h"
#include "mc/dataloadhelper/NewUniqueIdsDataLoadHelper.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/VanillaBlockActorFactory.h"
#include "mc/world/level/block/actor/component/IVanillaRenderBlockActorComponent.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

namespace lholo::projection::detail {
void pairProjectedChests(BlockSource &region, ProjectionState &state,
                         std::unordered_set<BlockActor const *> const *created) {
    constexpr std::array<std::pair<int, int>, 4> horizontalNeighbors{{
        {-1, 0},
        {1, 0},
        {0, -1},
        {0, 1},
    }};

    ScopedTessellationBlocks projectedWorld{
        *state.worldLookup, *state.expectedWorldBlockActors
    };
    ScopedRegionWriteSuppression noWorldWrites;
    for (auto const& [key, actor] : *state.expectedWorldBlockActors) {
        if (created && !created->contains(actor.get()))
            continue;
        if (actor->mType != BlockActorType::Chest) continue;
        if (!state.worldLookup->indexAt(key))
            continue;
        auto* chest = static_cast<ChestBlockActor*>(actor.get());
        if (chest->mLargeChestPaired != nullptr) continue;

        auto const [x, y, z] = key;
        for (auto const [dx, dz] : horizontalNeighbors) {
            BlockPos const neighbor{x + dx, y, z + dz};
            auto *found = state.worldLookup->task()
                              ? state.worldLookup->taskActorAt(std::tuple{neighbor.x, neighbor.y, neighbor.z})
                              : [&]() -> BlockActor const * {
                auto it = state.expectedWorldBlockActors->find(std::tuple{neighbor.x, neighbor.y, neighbor.z});
                return it == state.expectedWorldBlockActors->end() ? nullptr : it->second.get();
            }();
            if (!found || found->mType != BlockActorType::Chest || (created && !created->contains(found))) {
                continue;
            }

            chest->_tryToPairWith(region, neighbor);
            if (chest->mLargeChestPaired != nullptr) break;
        }
    }
}

void rebuildProjectedBlockActors(ProjectionState &state, BlockActorRenderDispatcher &dispatcher,
                                 LegacyStructureSettings const &transformSettings,
                                 ProjectionPlacementSettings const &settings, std::span<std::size_t const> indices,
                                 bool replaceAll) {
    auto actors = replaceAll ? std::make_shared<ExpectedBlockActorMap>()
                             : std::make_shared<ExpectedBlockActorMap>(*state.expectedWorldBlockActors);
    auto available = replaceAll ? std::make_shared<std::unordered_set<std::size_t>>()
                                : std::make_shared<std::unordered_set<std::size_t>>(*state.blockActorRendererAvailable);
    if (replaceAll) {
        state.projectedBlockActors.clear();
    } else {
        std::unordered_set<std::size_t> replacing(indices.begin(), indices.end());
        std::erase_if(state.projectedBlockActors, [&](auto const &p) { return replacing.contains(p.structureIndex); });
    }
    for (auto index : indices) {
        auto const& entry = state.structure->renderBlocks[index];
        auto const visible = isLayerVisible(
            settings.layerAxis == structure::LayerAxis::X ? entry.x : entry.y,
            settings.layerDisplayMode,
            settings.displayLayer,
            entry.materialIndex,
            entry.liquidMaterialIndex,
            settings.layerAxis
        );
        if (!visible) continue;
        auto const transformed = transformStructurePosition(
            entry, *state.structure, settings.mirrorMode, settings.rotationTurns
        );
        auto const* transformedBlock = transformExpectedBlock(
            entry.block, transformSettings, settings.identityTransform
        );
        BlockPos const worldPosition{
            state.anchor.x + settings.offsetX + transformed.x,
            state.anchor.y + settings.offsetY + transformed.y,
            state.anchor.z + settings.offsetZ + transformed.z
        };
        auto const worldKey = std::tuple{worldPosition.x, worldPosition.y, worldPosition.z};
        actors->erase(worldKey);
        available->erase(index);
        auto blockActor = VanillaBlockActorFactory::createBlockActor(worldPosition, transformedBlock->getBlockType());
        if (blockActor) {
            if (entry.blockEntityNbt) {
                NewUniqueIdsDataLoadHelper dataLoadHelper;
                dataLoadHelper.mLevel = state.level;
                blockActor->load(*state.level, *entry.blockEntityNbt, dataLoadHelper);
                blockActor->mPosition = worldPosition;
            }
            auto *actor = blockActor.get();
            actors->insert_or_assign(worldKey, std::move(blockActor));
            auto *renderComponent = actor->_getRenderComponent();
            if (renderComponent && dispatcher.mRenderers.get()[renderComponent->getRendererId()]) {
                state.projectedBlockActors.push_back({worldPosition, transformedBlock, actor, index});
                available->insert(index);
            }
        }
    }

    state.expectedWorldBlockActors = std::move(actors);
    state.blockActorRendererAvailable = std::move(available);
}

void rebuildProjectionPlacement(ProjectionState &state, BlockSource &region, BlockActorRenderDispatcher &dispatcher,
                                LegacyStructureSettings const &transformSettings,
                                ProjectionPlacementSettings const &settings) {
    ++state.drawMeshRevision; // invalidate before mutating layout/centers
    for (auto &section : state.sections)
        section.biomeRefresh = {};
    state.biomeRefreshCursor = state.biomeRefreshVisibleCursor = 0;
    // A moved placement keeps local GPU geometry. The immutable coordinate
    // view follows the new origin without allocating per-cell world maps.
    state.blockTessellator = std::make_unique<BlockTessellator>(&region);

    // Publish a new immutable virtual-world version. In-flight workers keep
    // the previous view alive without observing a partially rebuilt placement.
    auto const totalBlocks = state.structure->renderBlocks.size();
    std::array<long long, 3> origin{static_cast<long long>(state.anchor.x) + settings.offsetX,
                                    static_cast<long long>(state.anchor.y) + settings.offsetY,
                                    static_cast<long long>(state.anchor.z) + settings.offsetZ};
    structure::LayerSettings layers{settings.layerAxis, settings.layerDisplayMode, settings.displayLayer};
    if (state.localWorldLookup && state.localWorldLookup->hasTransform(settings.mirrorMode, settings.rotationTurns)) {
        state.localWorldLookup = state.localWorldLookup->relocated(origin, layers);
    } else {
        ProjectionWorldLookup::BlockTransforms transformedBlocks;
        state.structure->renderBlocks.forEachStoredValue([&](auto const &cell) {
            for (auto value : {cell.block, cell.liquid}) {
                if (!value || transformedBlocks.contains(value))
                    continue;
                transformedBlocks.emplace(value,
                                          transformExpectedBlock(value, transformSettings, settings.identityTransform));
            }
        });
        state.localWorldLookup = std::make_shared<ProjectionWorldLookup>(
            state.structure, origin, settings.mirrorMode, settings.rotationTurns, layers, std::move(transformedBlocks));
    }
    state.worldLookup = state.localWorldLookup;
    rebuildProjectedBlockActors(state, dispatcher, transformSettings, settings, state.blockActorIndices, true);

    // Bounds centers are stable under visibility changes and require no scan
    // over tens of millions of cells when only the placement origin changes.
    for (std::size_t section = 0; section < state.sections.size(); ++section) {
        auto &value = state.sections[section];
        value.buildBounds = projectionSectionBounds(state.localSectionKeys[section], *state.structure,
                                                    settings.mirrorMode, settings.rotationTurns);
        value.center = (value.buildBounds.minimum + value.buildBounds.maximum) / 2.0f;
    }
    if (!state.fileTransform)
        pairProjectedChests(region, state);
    auto* stateAddress = &state;
    auto* regionAddress = &region;
    state.blockTessellator->mCachedGetBlock.get()
        = [stateAddress, regionAddress](BlockPos const& position) -> Block const& {
            auto const found = stateAddress->worldLookup->blockAt(
                std::tuple{position.x, position.y, position.z}
            );
            return !found
                ? regionAddress->getBlock(position) : *found;
        };
    if (settings.resetCorrectionScan) {
        state.correctionScan.reset(state.sectionBlockIndices);
        state.correctionEvents.clear();
        state.correctionSubChunks.clear();
        state.initialCorrectionScan.reset(totalBlocks);
    }
}

} // namespace lholo::projection::detail
