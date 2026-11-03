// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/correction/ProjectionCorrectionTracker.h"
#include "projection/correction/BannerCorrection.h"
#include "projection/world/CorrectionWorldReader.h"
#include "projection/correction/CorrectionMeshRules.h"
#include <utility>

#include "block/BlockPlacementRules.h"
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionState.h"
#include "projection/runtime/ProjectionWorldEvents.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <array>
#include <cassert>
#include <string_view>
#include <tuple>

#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

namespace lholo::projection::detail {
namespace {

SubChunkKey localSectionKey(BlockPos const& position) {
    return correctionSectionKey(position.x, position.y, position.z);
}

std::size_t ensureCorrectionSection(
    ProjectionState& state,
    BlockPos const&  position,
    BlockPos const&  transformedPosition
) {
    auto const key = localSectionKey(position);
    if (auto const found = state.localSectionIndices.find(key);
        found != state.localSectionIndices.end()) {
        if (state.sectionBlockIndices[found->second].empty()) {
            ++state.drawMeshRevision;
            state.sections[found->second].center = Vec3{
                static_cast<float>(transformedPosition.x) + 0.5f,
                static_cast<float>(transformedPosition.y) + 0.5f,
                static_cast<float>(transformedPosition.z) + 0.5f,
            };
        }
        return found->second;
    }
    auto const section = state.sections.size();
    state.localSectionIndices.emplace(key, section);
    state.localSectionKeys.push_back(key);
    state.sectionBlockIndices.emplace_back();
    state.sectionExtraBlockPositions.emplace_back();
    SectionState sectionState;
    sectionState.buildBounds = projectionSectionBounds(
        key, *state.structure, state.cachedMirror, state.cachedRotation
    );
    sectionState.center = Vec3{
        static_cast<float>(transformedPosition.x) + 0.5f,
        static_cast<float>(transformedPosition.y) + 0.5f,
        static_cast<float>(transformedPosition.z) + 0.5f,
    };
    sectionState.dirty = true;
    sectionState.requestedRevision = 1;
    state.sections.push_back(std::move(sectionState));
    state.warningFillSectionMeshes.emplace_back();
    state.correctionOutlineSectionMeshes.emplace_back();
    state.wrongFillSectionMeshes.emplace_back();
    state.wrongOutlineSectionMeshes.emplace_back();
    state.blockEntityPlaceholderSectionMeshes.emplace_back();
    return section;
}

} // namespace

CorrectionProgressChanges updateCorrectionTracker(ProjectionState& state, BlockSource& region,
                                                  LegacyStructureSettings const& transformSettings, int mirrorMode,
                                                  int rotationTurns, int offsetX, int offsetY, int offsetZ,
                                                  structure::LayerDisplayMode layerDisplayMode, int displayLayer,
                                                  structure::LayerAxis layerAxis, Vec3 const& cameraPosition,
                                                  CorrectionWorkKind work, CorrectionFrameBudget& budget) {
    CorrectionProgressChanges changes;
    struct DirtySection {
        std::size_t section;
        bool models;
    };
    // Existing correction updates touch at most seven sections per cell;
    // world-neighbor dependencies add six. A loaded tile overlaps at most 27.
    std::array<DirtySection, CorrectionQuantum * 13 + 27> dirtySections;
    std::size_t dirtyCount{};
    auto const markDirty = [&](std::size_t section, bool models = false) {
        assert(dirtyCount < dirtySections.size());
        dirtySections[dirtyCount++] = {section, models};
    };
    bool const identityTransform = mirrorMode == 0 && rotationTurns == 0;
    CorrectionWorldReader worldReader{region};
    auto const findCell = [&](int x, int y, int z) {
        return findIndexedCorrectionCell(state.localSectionIndices, state.correctionIndex, state.sectionBlockIndices, x,
                                         y, z);
    };

    auto const updateCorrection = [&](std::size_t index) {
        auto const& entry = state.structure->renderBlocks[index];
        auto const visible =
            state.fileTransform
                ? state.refreshTaskCellVisibility(index)
                : isLayerVisible(layerAxis == structure::LayerAxis::X ? entry.x : entry.y, layerDisplayMode,
                                 displayLayer, entry.materialIndex, entry.liquidMaterialIndex, layerAxis);
        auto const transformed = transformStructurePosition(entry, *state.structure, mirrorMode, rotationTurns);
        BlockPos const position{state.anchor.x + offsetX + transformed.x, state.anchor.y + offsetY + transformed.y,
                                state.anchor.z + offsetZ + transformed.z};
        auto classify = [&]() {
            if (worldReader.empty(position)) {
                ++state.correctionEmptyFastCells;
                return CorrectionState::Missing;
            }
            auto const* expected = transformExpectedBlock(entry.block, transformSettings, identityTransform);
            auto const* expectedLiquid = transformExpectedBlock(entry.liquid, transformSettings, identityTransform);
            auto const& actual = region.getBlock(position);
            auto const isBubbleColumn = (expected && expected->getTypeName() == "minecraft:bubble_column") ||
                                        (actual.getTypeName() == "minecraft:bubble_column");
            auto const& actualLiquid =
                isBubbleColumn ? region.getExtraBlock(position) : region.getLiquidBlock(position);
            auto const bodyMissing = expected && actual.isAir();
            auto const liquidMissing = expectedLiquid && actualLiquid.isAir();
            auto const bodyTypeWrong =
                expected && !actual.isAir() &&
                block::placeableBaseName(actual.getTypeName()) != block::placeableBaseName(expected->getTypeName());
            auto const liquidTypeWrong =
                expectedLiquid && !actualLiquid.isAir() && actualLiquid.getTypeName() != expectedLiquid->getTypeName();
            auto const liquidCellOccupiedBySolid =
                !expected && expectedLiquid && !actual.isAir() && actual.getTypeName() != expectedLiquid->getTypeName();
            auto bannerState = CorrectionState::Correct;
            if (expected && entry.blockEntityNbt && !bodyMissing && !bodyTypeWrong &&
                expected->getBlockType().getBlockEntityType() == BlockActorType::Banner) {
                auto appearance = state.bannerAppearances.find(entry.blockEntityNbt.get());
                if (appearance == state.bannerAppearances.end()) {
                    auto snapshot = loadBannerAppearance(*expected, *entry.blockEntityNbt, *state.level);
                    if (snapshot)
                        appearance =
                            state.bannerAppearances.emplace(entry.blockEntityNbt.get(), std::move(*snapshot)).first;
                }
                auto const actualBanner = readBannerAppearance(region.getBlockEntity(position));
                // A client may receive the block before its entity packet. Do not
                // count it as complete until the native banner data is available.
                bannerState = appearance != state.bannerAppearances.end() && actualBanner
                                  ? compareBannerAppearance(appearance->second, *actualBanner)
                                  : CorrectionState::WrongState;
            }
            auto nextState = CorrectionState::Correct;
            if (bodyMissing || liquidMissing) {
                nextState = CorrectionState::Missing;
            } else if (bodyTypeWrong || liquidTypeWrong || liquidCellOccupiedBySolid ||
                       bannerState == CorrectionState::WrongType) {
                nextState = CorrectionState::WrongType;
            } else if ((expected &&
                        !projectionStatesMatch(
                            // Native component/wall connections are derived from
                            // the neighborhood, rather than player orientation.
                            // Normalize Java or unflattened palette states before
                            // comparing with the real world's current permutation.
                            withNeighborConnections(*expected, region, position), actual, region, position)) ||
                       (expectedLiquid && !projectionStatesMatch(*expectedLiquid, actualLiquid)) ||
                       bannerState == CorrectionState::WrongState) {
                nextState = CorrectionState::WrongState;
            }
            return nextState;
        };
        auto nextState = classify();
        auto const nowCorrect = nextState == CorrectionState::Correct;
        auto const previousState = state.correctionStates.get(index);
        auto const wasCorrect = previousState == CorrectionState::Correct;
        if (state.fileTransform) {
            if (visible && nextState != previousState) {
                removeTaskCellProgress(state.fileProgress, previousState);
                addTaskCellProgress(state.fileProgress, nextState);
                ++state.progressRevision;
            }
            state.progressCorrect[index] = nowCorrect ? 1 : 0;
        }
        if (!state.fileTransform && nowCorrect != wasCorrect) {
            state.progressCorrect[index] = nowCorrect ? 1 : 0;
            ++state.progressRevision;
            if (nowCorrect)
                ++state.progressCorrectCount;
            else
                --state.progressCorrectCount;
            changes.overall = true;
            if (visible) {
                if (nowCorrect)
                    ++state.progressVisibleCorrectCount;
                else
                    --state.progressVisibleCorrectCount;
                changes.visible = true;
            }
        }
        auto const nextErrorKind = nextState == CorrectionState::WrongType    ? uchar{1}
                                   : nextState == CorrectionState::WrongState ? uchar{2}
                                                                              : uchar{0};
        auto const previousErrorKind = state.progressErrorKind[index];
        if (!state.fileTransform && nextErrorKind != previousErrorKind) {
            if (previousErrorKind == 1)
                --state.progressWrongTypeCount;
            else if (previousErrorKind == 2)
                --state.progressWrongStateCount;
            if (nextErrorKind == 1)
                ++state.progressWrongTypeCount;
            else if (nextErrorKind == 2)
                ++state.progressWrongStateCount;
            changes.errors = true;
        }
        state.progressErrorKind[index] = nextErrorKind;
        state.initialCorrectionScan.record(index);
        // Store truth even in hidden layers; visibility only affects mesh
        // consumption. Their world events must still maintain total progress.
        auto const stateChanged = state.correctionStates.set(index, nextState);
        if (!visible)
            return;
        if (stateChanged) {
            if (state.fileTransform)
                state.taskNeighborChanges.emplace_back(position.x, position.y, position.z);
            bool models = correctionNeedsModelUpdate(previousState, nextState);
            markDirty(state.correctionStates.section(index), models);
            // A missing-cell shell omits faces shared with adjacent missing
            // cells. If either side changes, both section meshes may need an
            // exposed face added or removed (including across 16^3 borders).
            constexpr int neighbors[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
            for (auto const& delta : neighbors) {
                auto const neighbor = state.worldLookup->indexAt(
                    std::tuple{position.x + delta[0], position.y + delta[1], position.z + delta[2]});
                if (neighbor) {
                    auto const section = state.correctionStates.section(*neighbor);
                    markDirty(section, models || state.sections[section].worldOcclusionDependent);
                }
            }
        }
    };

    auto const updateExtra = [&](BlockPos const& localPosition, BlockPos const& worldPosition, bool visible) {
        auto const key = SubChunkKey{localPosition.x, localPosition.y, localPosition.z};
        bool empty = worldReader.empty(worldPosition);
        if (empty)
            ++state.correctionEmptyFastCells;
        bool const isExtra = !empty && !region.getBlock(worldPosition).isAir() &&
                             state.worldLookup->ownsAir(std::tuple{worldPosition.x, worldPosition.y, worldPosition.z});
        auto const detected = state.detectedExtraBlockPositions.find(key);
        if (isExtra != (detected != state.detectedExtraBlockPositions.end())) {
            if (isExtra) {
                state.detectedExtraBlockPositions.insert(key);
                ++state.detectedExtraSectionCounts[localSectionKey(localPosition)];
                ++state.progressExtraCount;
            } else {
                state.detectedExtraBlockPositions.erase(detected);
                auto count = state.detectedExtraSectionCounts.find(localSectionKey(localPosition));
                if (--count->second == 0)
                    state.detectedExtraSectionCounts.erase(count);
                --state.progressExtraCount;
            }
            changes.errors = true;
            if (state.fileTransform)
                state.fileProgress.extra = state.progressExtraCount;
        }

        bool const shouldRender = isExtra && visible;
        auto const rendered = state.extraBlockPositions.find(key);
        if (shouldRender == (rendered != state.extraBlockPositions.end()))
            return;

        std::size_t section{};
        if (shouldRender) {
            auto const transformedPosition =
                transformStructurePosition(localPosition, *state.structure, mirrorMode, rotationTurns);
            section = ensureCorrectionSection(state, localPosition, transformedPosition);
            state.extraBlockPositions.insert(key);
            state.sectionExtraBlockPositions[section].insert(key);
        } else {
            auto const sectionFound = state.localSectionIndices.find(localSectionKey(localPosition));
            if (sectionFound == state.localSectionIndices.end())
                return;
            section = sectionFound->second;
            state.extraBlockPositions.erase(rendered);
            state.sectionExtraBlockPositions[section].erase(key);
        }
        // This cell is absent from the projected-world map. Native connection
        // tessellators can therefore read its real block through the fallback.
        markDirty(section, true);
        if (state.fileTransform)
            state.taskNeighborChanges.emplace_back(worldPosition.x, worldPosition.y, worldPosition.z);
        constexpr int neighbors[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
        for (auto const& delta : neighbors) {
            BlockPos const neighbor{
                localPosition.x + delta[0],
                localPosition.y + delta[1],
                localPosition.z + delta[2],
            };
            auto const found = state.localSectionIndices.find(localSectionKey(neighbor));
            if (found != state.localSectionIndices.end())
                markDirty(found->second, true);
        }
    };

    auto const localFromWorld = [&](BlockPos const& world) -> std::optional<BlockPos> {
        auto x = static_cast<long long>(world.x) - state.anchor.x - offsetX;
        auto y = static_cast<long long>(world.y) - state.anchor.y - offsetY;
        auto z = static_cast<long long>(world.z) - state.anchor.z - offsetZ;
        bool rotated = (rotationTurns & 1) != 0;
        if (x < 0 || x >= (rotated ? state.structure->sizeZ : state.structure->sizeX) || y < 0 ||
            y >= state.structure->sizeY || z < 0 || z >= (rotated ? state.structure->sizeX : state.structure->sizeZ))
            return std::nullopt;
        return inverseTransformStructurePosition(
            BlockPos{static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)}, *state.structure, mirrorMode,
            rotationTurns);
    };
    auto const invalidateWorldNeighbors = [&](BlockPos const& world) {
        constexpr int neighbors[6][3]{{-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}};
        for (auto const& d : neighbors) {
            auto const found = state.worldLookup->indexAt(
                std::tuple{world.x + d[0], world.y + d[1], world.z + d[2]});
            if (!found) continue;
            auto const section = state.correctionStates.section(*found);
            auto const& target = state.sections[section];
            if (target.worldOcclusionDependent || target.buildInFlight) markDirty(section, true);
        }
    };
    auto const checkWorld = [&](BlockPos const& world, std::uint64_t destroyedAt, bool neighborEvent = false) {
        // Halo events invalidate dependent geometry but never count as extras
        // or expand the structure's correction/placement domain.
        if (neighborEvent) invalidateWorldNeighbors(world);
        auto localResult = localFromWorld(world);
        if (!localResult)
            return;
        auto const& local = *localResult;
        auto found = findCell(local.x, local.y, local.z);
        if (found) {
            if (destroyedAt && state.worldLookup->owns(*found))
                state.pendingBrokenCells.push_back({world.x, world.y, world.z, destroyedAt});
            updateCorrection(*found);
        } else if (isStructureCellCovered(*state.structure, local)) {
            bool visible = isLayerVisible(layerAxis == structure::LayerAxis::X ? local.x : local.y, layerDisplayMode,
                                          displayLayer, -1, -1, layerAxis);
            updateExtra(local, world, visible);
        }
    };

    if (work == CorrectionWorkKind::Event) {
        for (std::size_t i = 0; i < CorrectionQuantum && !state.correctionEvents.empty(); ++i) {
            if (!budget.tryConsume())
                break;
            auto event = *state.correctionEvents.front();
            state.correctionEvents.pop();
            checkWorld(BlockPos{event.position[0], event.position[1], event.position[2]}, event.destroyedAt, true);
        }
    } else if (work == CorrectionWorkKind::Loaded) {
        if (state.correctionSubChunks.empty() && !state.taskAirRefresh.empty())
            state.correctionSubChunks.push(state.taskAirRefresh.take());
        if (auto refresh = state.correctionSubChunks.front()) {
            auto limit = std::min(CorrectionQuantum, 4096 - refresh->cell);
            std::size_t processed{};
            while (processed < limit && budget.tryConsume()) {
                if (refresh->cell == 0 && processed == 0) {
                    BlockPos const origin{state.anchor.x + offsetX, state.anchor.y + offsetY, state.anchor.z + offsetZ};
                    if (auto box = projectionSectionLocalIntersection(refresh->position, *state.structure,
                            origin, mirrorMode, rotationTurns, 1)) {
                        for (int x = box->minimum[0] / 16; x <= (box->maximum[0] - 1) / 16; ++x)
                            for (int y = box->minimum[1] / 16; y <= (box->maximum[1] - 1) / 16; ++y)
                                for (int z = box->minimum[2] / 16; z <= (box->maximum[2] - 1) / 16; ++z) {
                                    auto const found = state.localSectionIndices.find(std::tuple{x,y,z});
                                    if (found == state.localSectionIndices.end()) continue;
                                    auto const& target = state.sections[found->second];
                                    if (target.worldOcclusionDependent || target.buildInFlight)
                                        markDirty(found->second, true);
                                }
                    }
                }
                auto cell = refresh->cell + processed++;
                checkWorld(BlockPos{refresh->position[0] * 16 + static_cast<int>(cell >> 8),
                                    refresh->position[1] * 16 + static_cast<int>((cell >> 4) & 15),
                                    refresh->position[2] * 16 + static_cast<int>(cell & 15)},
                           0);
            }
            if (processed)
                state.correctionSubChunks.advance(processed);
        }
    } else if (work == CorrectionWorkKind::Initial && !state.taskSectionRefresh.empty()) {
        auto section = state.taskSectionRefresh.section();
        auto const &indices = state.sectionBlockIndices[section];
        auto offset = state.taskSectionRefresh.offset();
        auto limit = std::min(CorrectionQuantum, indices.size() - offset);
        std::size_t processed{};
        while (processed < limit && budget.tryConsume())
            state.refreshTaskCellVisibility(indices[offset + processed++]);
        if (processed)
            state.taskSectionRefresh.advance(processed, indices.size());
    } else if (work == CorrectionWorkKind::Initial) {
        bool rotated = (rotationTurns & 1) != 0;
        auto relative = [](float camera, int anchor, int offset, int size) {
            return static_cast<int>(std::clamp(std::floor(static_cast<double>(camera) - anchor - offset), 0.0,
                                               static_cast<double>(std::max(0, size - 1))));
        };
        auto localCamera = inverseTransformStructurePosition(
            BlockPos{relative(cameraPosition.x, state.anchor.x, offsetX,
                              rotated ? state.structure->sizeZ : state.structure->sizeX),
                     relative(cameraPosition.y, state.anchor.y, offsetY, state.structure->sizeY),
                     relative(cameraPosition.z, state.anchor.z, offsetZ,
                              rotated ? state.structure->sizeX : state.structure->sizeZ)},
            *state.structure, mirrorMode, rotationTurns);
        std::optional<std::size_t> nearby;
        if (auto found = state.localSectionIndices.find(localSectionKey(localCamera));
            found != state.localSectionIndices.end())
            nearby = found->second;
        auto selected = state.correctionScan.select(
            [&](std::size_t section) {
                auto const& target = state.sections[section];
                auto dx = target.center.x + static_cast<float>(state.anchor.x + offsetX) - cameraPosition.x;
                auto dy = target.center.y + static_cast<float>(state.anchor.y + offsetY) - cameraPosition.y;
                auto dz = target.center.z + static_cast<float>(state.anchor.z + offsetZ) - cameraPosition.z;
                float distance = dx * dx + dy * dy + dz * dz;
                bool visible = state.correctionStates.sectionVisible(section);
                return CorrectionPriority{!visible              ? 3
                                          : distance <= 32 * 32 ? 0
                                          : target.buildVisible ? 1
                                                                : 2,
                                          distance};
            },
            nearby);
        if (selected) {
            auto const& indices = state.sectionBlockIndices[*selected];
            auto offset = state.correctionScan.offset(*selected);
            auto limit = std::min(CorrectionQuantum, indices.size() - offset);
            std::size_t processed{};
            while (processed < limit && budget.tryConsume())
                updateCorrection(indices[offset + processed++]);
            if (processed)
                state.correctionScan.advance(*selected, processed, indices.size());
        }
    } else {
        for (std::size_t processed = 0; processed < CorrectionQuantum && !state.extraScan.complete(); ++processed) {
            if (!budget.tryConsume())
                break;
            auto cell = state.extraScan.step(state.structure->regions, [&](std::array<int, 3> const& section) {
                auto found = state.localSectionIndices.find(std::tuple{section[0], section[1], section[2]});
                return found != state.localSectionIndices.end() && found->second < state.correctionIndex.size()
                           ? state.correctionIndex[found->second].mask()
                           : CorrectionCellMask{};
            });
            if (!cell)
                continue;
            BlockPos local{(*cell)[0], (*cell)[1], (*cell)[2]};
            auto transformed = transformStructurePosition(local, *state.structure, mirrorMode, rotationTurns);
            BlockPos world{state.anchor.x + offsetX + transformed.x, state.anchor.y + offsetY + transformed.y,
                           state.anchor.z + offsetZ + transformed.z};
            bool visible = isLayerVisible(layerAxis == structure::LayerAxis::X ? local.x : local.y, layerDisplayMode,
                                          displayLayer, -1, -1, layerAxis);
            updateExtra(local, world, visible);
            // A tile with previously detected extras must visit those cells to
            // remove their counts/meshes. Fresh empty tiles may skip in bulk.
            if (!worldReader.empty(world) || state.detectedExtraSectionCounts.contains(localSectionKey(local)))
                continue;
            auto origin = BlockPos{state.anchor.x + offsetX, state.anchor.y + offsetY, state.anchor.z + offsetZ};
            if (auto box = projectionSectionLocalIntersection(worldReader.emptySection(), *state.structure, origin,
                                                              mirrorMode, rotationTurns))
                state.correctionEmptyFastCells += state.extraScan.eraseBox(box->minimum, box->maximum);
        }
    }
    // One revision per affected section per quantum, including shared faces.
    // This also preserves the interactivity priority until a matching upload.
    auto dirtyEnd = dirtySections.begin() + dirtyCount;
    std::sort(dirtySections.begin(), dirtyEnd, [](auto const& a, auto const& b) { return a.section < b.section; });
    for (auto it = dirtySections.begin(); it != dirtyEnd;) {
        auto section = it->section;
        bool models{};
        do {
            models = models || it->models;
            ++it;
        } while (it != dirtyEnd && it->section == section);
        auto& target = state.sections[section];
        target.dirty = target.incrementalDirty = true;
        target.interactiveDirty = target.interactiveDirty || work == CorrectionWorkKind::Event;
        target.modelDirty = target.modelDirty || models;
        if (models)
            target.biomeRefresh = {};
        ++target.requestedRevision;
    }
    state.correctionEmptyProbes += worldReader.probes();
    return changes;
}

} // namespace lholo::projection::detail
