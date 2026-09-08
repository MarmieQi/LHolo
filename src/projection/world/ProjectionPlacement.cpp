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
#include "mc/dataloadhelper/DataLoadHelper.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/VanillaBlockActorFactory.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

namespace lholo::projection::detail {
namespace {

void pairProjectedChests(BlockSource& region, ProjectionState& state) {
    constexpr std::array<std::pair<int, int>, 4> horizontalNeighbors{{
        {-1, 0},
        {1, 0},
        {0, -1},
        {0, 1},
    }};

    ScopedTessellationBlocks projectedWorld{
        *state.expectedWorldBlocks, *state.expectedWorldBlockActors
    };
    for (auto const& [key, actor] : *state.expectedWorldBlockActors) {
        // 26.32: BlockActor::isType()/ChestBlockActor::isLargeChest() were
        // inlined out; the same state stays on public members.
        if (actor->mType != BlockActorType::Chest) continue;
        auto* chest = static_cast<ChestBlockActor*>(actor.get());
        if (chest->mLargeChestPaired != nullptr) continue;

        auto const [x, y, z] = key;
        for (auto const [dx, dz] : horizontalNeighbors) {
            BlockPos const neighbor{x + dx, y, z + dz};
            auto const found = state.expectedWorldBlockActors->find(
                std::tuple{neighbor.x, neighbor.y, neighbor.z}
            );
            if (found == state.expectedWorldBlockActors->end()
                || found->second->mType != BlockActorType::Chest) {
                continue;
            }

            chest->_tryToPairWith(region, neighbor);
            if (chest->mLargeChestPaired != nullptr) break;
        }
    }
}

// NewUniqueIdsDataLoadHelper() was inlined out of the 26.32 SDK. Display
// actors only need identity NBT translation, so a pass-through helper
// replaces it (no actor unique ids are remapped).
class DisplayDataLoadHelper final : public DataLoadHelper {
public:
    ::Vec3  loadPosition(::Vec3 const& position) override { return position; }
    ::BlockPos loadBlockPosition(::BlockPos const& blockPos) override { return blockPos; }
    ::BlockPos loadBlockPositionOffset(::BlockPos const& blockPosOffset) override { return blockPosOffset; }
    float   loadRotationDegreesX(float x) override { return x; }
    float   loadRotationDegreesY(float y) override { return y; }
    float   loadRotationRadiansX(float x) override { return x; }
    float   loadRotationRadiansY(float y) override { return y; }
    uchar   loadFacingID(uchar facing) override { return facing; }
    ::Vec3  loadDirection(::Vec3 const& direction) override { return direction; }
    ::Direction::Type loadDirection(::Direction::Type direction) override { return direction; }
    ::Rotation loadRotation(::Rotation rotation) override { return rotation; }
    ::Mirror loadMirror(::Mirror mirror) override { return mirror; }
    ::ActorUniqueID loadActorUniqueID(::ActorUniqueID id) override { return id; }
    ::ActorUniqueID loadOwnerID(::ActorUniqueID id) override { return id; }
    ::InternalComponentRegistry::ComponentInfo const* loadActorInternalComponentInfo(
        ::std::unordered_map<::HashedString, ::InternalComponentRegistry::ComponentInfo> const&,
        ::std::string const&
    ) override {
        return nullptr;
    }
    ::DataLoadHelperType getType() const override { return ::DataLoadHelperType::Default; }
    bool  shouldResetTime() override { return false; }
};

} // namespace

void rebuildProjectionPlacement(
    ProjectionState&                   state,
    BlockSource&                       region,
    BlockActorRenderDispatcher&        dispatcher,
    LegacyStructureSettings const&     transformSettings,
    ProjectionPlacementSettings const& settings
) {
    // A moved placement keeps its local GPU geometry, but the virtual world
    // and correction lookup must follow the new world origin.
    state.blockTessellator = std::make_unique<BlockTessellator>(&region);

    // Publish a new immutable virtual-world version. In-flight workers keep
    // the previous maps alive without observing a partially rebuilt placement.
    state.expectedWorldBlocks = std::make_shared<ExpectedBlockMap>();
    state.expectedWorldBlockActors = std::make_shared<ExpectedBlockActorMap>();
    state.projectedBlockActors.clear();
    std::fill(
        state.blockActorRendererAvailable.begin(),
        state.blockActorRendererAvailable.end(),
        0
    );
    state.expectedWorldBlockIndices = std::make_shared<ExpectedBlockIndexMap>();
    std::vector<Vec3> centerSums(state.sections.size(), Vec3{});
    std::vector<std::size_t> centerCounts(state.sections.size(), 0);

    for (std::size_t index = 0; index < state.structure->renderBlocks.size(); ++index) {
        auto const& entry = state.structure->renderBlocks[index];
        if (!isLayerVisible(
                settings.layerAxis == 1 ? entry.x : entry.y,
                settings.layerDisplayMode,
                settings.displayLayer
            )) {
            // Hidden layers behave like completed cells for mesh generation,
            // but are excluded from the world lookup below.
            state.correctionStates[index] = CorrectionState::Correct;
            continue;
        }
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
        if (transformedBlock) {
            state.expectedWorldBlocks->emplace(worldKey, transformedBlock);
            if (transformedBlock->getBlockEntityType() != BlockActorType::Undefined) {
                // BlockType::newBlockEntity() was inlined out of the 26.32
                // SDK; the vanilla factory creates the same actor.
                auto blockActor = VanillaBlockActorFactory::createBlockActor(
                    worldPosition, transformedBlock->getBlockType()
                );
                if (blockActor) {
                    if (entry.blockEntityNbt) {
                        // NewUniqueIdsDataLoadHelper() was inlined out of the
                        // 26.32 SDK. Display actors only need identity NBT
                        // translation, so a pass-through helper replaces it.
                        DisplayDataLoadHelper dataLoadHelper{};
                        blockActor->load(*state.level, *entry.blockEntityNbt, dataLoadHelper);
                        // 26.32: moveTo() was inlined out; the position member stays public.
                        blockActor->mPosition.get() = worldPosition;
                    }
                    auto* actor = blockActor.get();
                    state.expectedWorldBlockActors->emplace(worldKey, std::move(blockActor));
                    // 26.32: getRenderer() was inlined out of the dispatcher;
                    // an actor with a render component is renderable.
                    if (actor->_getRenderComponent() != nullptr) {
                        state.projectedBlockActors.push_back({
                            worldPosition, transformedBlock, actor, index
                        });
                        state.blockActorRendererAvailable[index] = 1;
                    }
                }
            }
        } else {
            // Liquids join the virtual world so vanilla liquid-height queries
            // see stacked virtual water (full-cell columns).
            auto const* transformedLiquid = transformExpectedBlock(
                entry.liquid, transformSettings, settings.identityTransform
            );
            if (transformedLiquid) {
                state.expectedWorldBlocks->emplace(worldKey, transformedLiquid);
            }
        }
        state.expectedWorldBlockIndices->emplace(worldKey, index);
        auto const section = state.blockToSection[index];
        centerSums[section] += Vec3{
            static_cast<float>(transformed.x) + 0.5f,
            static_cast<float>(transformed.y) + 0.5f,
            static_cast<float>(transformed.z) + 0.5f
        };
        ++centerCounts[section];
    }

    pairProjectedChests(region, state);
    for (std::size_t section = 0; section < state.sections.size(); ++section) {
        if (centerCounts[section] != 0) {
            state.sections[section].center
                = centerSums[section] / static_cast<float>(centerCounts[section]);
        }
    }
    auto* stateAddress = &state;
    auto* regionAddress = &region;
    state.blockTessellator->mCachedGetBlock.get()
        = [stateAddress, regionAddress](BlockPos const& position) -> Block const& {
            auto const found = stateAddress->expectedWorldBlocks->find(
                std::tuple{position.x, position.y, position.z}
            );
            return found == stateAddress->expectedWorldBlocks->end()
                ? regionAddress->getBlock(position) : *found->second;
        };
    state.correctionScanCursor = 0;
}

} // namespace lholo::projection::detail
