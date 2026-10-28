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
#include "mc/deps/nbt/CompoundTag.h"
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

} // namespace

void rebuildProjectionPlacement(
    ProjectionState&                   state,
    BlockSource&                       region,
    BlockActorRenderDispatcher&        dispatcher,
    LegacyStructureSettings const&     transformSettings,
    ProjectionPlacementSettings const& settings
) {
    ++state.drawMeshRevision; // invalidate before mutating layout/centers
    for (auto& section : state.sections) section.biomeRefresh = {};
    state.biomeRefreshCursor = state.biomeRefreshVisibleCursor = 0;
    // A moved placement keeps its local GPU geometry, but the virtual world
    // and correction lookup must follow the new world origin.
    state.blockTessellator = std::make_unique<BlockTessellator>(&region);

    // Publish a new immutable virtual-world version. In-flight workers keep
    // the previous maps alive without observing a partially rebuilt placement.
    auto const totalBlocks = state.structure->renderBlocks.size();
    state.expectedWorldBlocks = std::make_shared<ExpectedBlockMap>();
    state.expectedWorldBlocks->reserve(totalBlocks);
    state.expectedWorldBlockActors = std::make_shared<ExpectedBlockActorMap>();
    state.projectedBlockActors.clear();
    // Workers retain the previous immutable availability version while layout
    // changes publish a fresh one, just like the virtual-world maps above.
    state.blockActorRendererAvailable = std::make_shared<std::vector<uchar>>(totalBlocks, 0);
    state.expectedWorldBlockIndices = std::make_shared<ExpectedBlockIndexMap>();
    state.expectedWorldBlockIndices->reserve(totalBlocks);
    std::vector<Vec3> centerSums(state.sections.size(), Vec3{});
    std::vector<std::size_t> centerCounts(state.sections.size(), 0);

    for (std::size_t index = 0; index < state.structure->renderBlocks.size(); ++index) {
        auto const& entry = state.structure->renderBlocks[index];
        if (!isLayerVisible(
                settings.layerAxis == structure::LayerAxis::X ? entry.x : entry.y,
                settings.layerDisplayMode,
                settings.displayLayer,
                entry.materialIndex,
                entry.liquidMaterialIndex,
                settings.layerAxis
            )) {
            // Hidden layers behave like completed cells for mesh generation,
            // but are excluded from the world lookup below.
            if ((*state.correctionStates)[index] != CorrectionState::Correct) {
                if (state.correctionStates.use_count() > 1) {
                    state.correctionStates = std::make_shared<std::vector<CorrectionState>>(*state.correctionStates);
                }
                (*state.correctionStates)[index] = CorrectionState::Correct;
            }
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
            if (transformedBlock->getBlockType().getBlockEntityType() != BlockActorType::Undefined) {
                auto blockActor = VanillaBlockActorFactory::createBlockActor(
                    worldPosition, transformedBlock->getBlockType()
                );
                if (blockActor) {
                    if (entry.blockEntityNbt) {
                        NewUniqueIdsDataLoadHelper dataLoadHelper;
                        dataLoadHelper.mLevel = state.level;
                        // A piston saved extended carries State 2 / Progress 1
                        // and keeps its head in a separate arm block, which is
                        // not placeable and not drawn; loaded as is, the piston
                        // vanished from the projection. Show what the player
                        // places: the retracted piston.
                        std::unique_ptr<CompoundTag> retracted;
                        auto const& typeName = transformedBlock->getTypeName();
                        if (typeName == "minecraft:piston" || typeName == "minecraft:sticky_piston") {
                            retracted = entry.blockEntityNbt->clone();
                            retracted->putInt("State", 0);
                            retracted->putInt("NewState", 0);
                            retracted->putFloat("Progress", 0.0f);
                            retracted->putFloat("LastProgress", 0.0f);
                        }
                        // The banner renderer draws nothing for a projected
                        // banner with pattern layers (custom and ominous
                        // banners vanished). Show its base colour instead.
                        if ((typeName == "minecraft:standing_banner" || typeName == "minecraft:wall_banner")
                            && (entry.blockEntityNbt->contains("Patterns") || entry.blockEntityNbt->contains("Type"))) {
                            retracted = entry.blockEntityNbt->clone();
                            retracted->erase("Patterns");
                            retracted->putInt("Type", 0);
                        }
                        blockActor->load(
                            *state.level, retracted ? *retracted : *entry.blockEntityNbt, dataLoadHelper
                        );
                        blockActor->mPosition = worldPosition;
                    }
                    auto* actor = blockActor.get();
                    state.expectedWorldBlockActors->emplace(worldKey, std::move(blockActor));
                    auto* renderComponent = actor->_getRenderComponent();
                    if (renderComponent
                        && dispatcher.mRenderers.get()[renderComponent->getRendererId()]) {
                        state.projectedBlockActors.push_back({
                            worldPosition, transformedBlock, actor, index
                        });
                        (*state.blockActorRendererAvailable)[index] = 1;
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
        state.sections[section].buildBounds = projectionSectionBounds(
            state.localSectionKeys[section], *state.structure, settings.mirrorMode, settings.rotationTurns
        );
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
