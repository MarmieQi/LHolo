// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/correction/ProjectionCorrectionTracker.h"

#include "block/BlockPlacementRules.h"
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionState.h"
#include "projection/runtime/ProjectionWorldEvents.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>

#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

namespace lholo::projection::detail {
namespace {

void markSectionDirty(ProjectionState& state, std::size_t section) {
    if (section >= state.sections.size()) return;
    auto& sectionState = state.sections[section];
    sectionState.dirty = true;
    sectionState.incrementalDirty = true;
    ++sectionState.requestedRevision;
}

SubChunkKey localSectionKey(BlockPos const& position) {
    auto const floorDiv16 = [](int value) {
        return value >= 0 ? value / 16 : -1 - ((-1 - value) / 16);
    };
    return {floorDiv16(position.x), floorDiv16(position.y), floorDiv16(position.z)};
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
    state.liquidProxySectionMeshes.emplace_back();
    state.blockEntityPlaceholderSectionMeshes.emplace_back();
    return section;
}

} // namespace

CorrectionProgressChanges updateCorrectionTracker(
    ProjectionState&                state,
    BlockSource&                    region,
    LegacyStructureSettings const& transformSettings,
    int                             mirrorMode,
    int                             rotationTurns,
    int                             offsetX,
    int                             offsetY,
    int                             offsetZ,
    int                             layerDisplayMode,
    int                             displayLayer,
    int                             layerAxis
) {
    CorrectionProgressChanges changes;
    auto const totalBlocks = state.structure->renderBlocks.size();
    // Share one fixed world-read budget between initial cache population and
    // incremental block notifications.
    constexpr std::size_t kCorrectionChecksPerFrame = 4096;
    constexpr std::size_t kSubChunkEventsPerFrame    = 64;
    bool const identityTransform = mirrorMode == 0 && rotationTurns == 0;

    auto const updateCorrection = [&](std::size_t index) {
        auto const& entry = state.structure->renderBlocks[index];
        auto const visible = isLayerVisible(
            layerAxis == 1 ? entry.x : entry.y, layerDisplayMode, displayLayer,
            entry.materialIndex, entry.liquidMaterialIndex, layerAxis
        );
        auto const transformed = transformStructurePosition(
            entry, *state.structure, mirrorMode, rotationTurns
        );
        BlockPos const position{
            state.anchor.x + offsetX + transformed.x,
            state.anchor.y + offsetY + transformed.y,
            state.anchor.z + offsetZ + transformed.z
        };
        auto const* expected = transformExpectedBlock(entry.block, transformSettings, identityTransform);
        auto const* expectedLiquid = transformExpectedBlock(
            entry.liquid, transformSettings, identityTransform
        );
        auto const& actual = region.getBlock(position);
        auto const& actualLiquid = region.getLiquidBlock(position);
        auto const bodyMissing = expected && actual.isAir();
        auto const liquidMissing = expectedLiquid && actualLiquid.isAir();
        auto const bodyTypeWrong = expected
            && !actual.isAir()
            && block::placeableBaseName(actual.getTypeName())
                != block::placeableBaseName(expected->getTypeName());
        auto const liquidTypeWrong = expectedLiquid
            && !actualLiquid.isAir() && actualLiquid.getTypeName() != expectedLiquid->getTypeName();
        auto const liquidCellOccupiedBySolid = !expected && expectedLiquid && !actual.isAir()
            && actual.getTypeName() != expectedLiquid->getTypeName();
        auto nextState = CorrectionState::Correct;
        if (bodyMissing || liquidMissing) {
            nextState = CorrectionState::Missing;
        } else if (bodyTypeWrong || liquidTypeWrong || liquidCellOccupiedBySolid) {
            nextState = CorrectionState::WrongType;
        } else if ((expected && !projectionStatesMatch(*expected, actual))
            || (expectedLiquid && actualLiquid != *expectedLiquid)) {
            nextState = CorrectionState::WrongState;
        }
        auto const nowCorrect = nextState == CorrectionState::Correct;
        auto const wasCorrect = state.progressCorrect[index] != 0;
        if (nowCorrect != wasCorrect) {
            state.progressCorrect[index] = nowCorrect ? 1 : 0;
            ++state.progressRevision;
            if (nowCorrect) ++state.progressCorrectCount;
            else --state.progressCorrectCount;
            changes.overall = true;
            if (visible) {
                if (nowCorrect) ++state.progressVisibleCorrectCount;
                else --state.progressVisibleCorrectCount;
                changes.visible = true;
            }
        }
        auto const nextErrorKind = nextState == CorrectionState::WrongType ? uchar{1}
            : nextState == CorrectionState::WrongState ? uchar{2}
            : uchar{0};
        auto const previousErrorKind = state.progressErrorKind[index];
        if (nextErrorKind != previousErrorKind) {
            if (previousErrorKind == 1) --state.progressWrongTypeCount;
            else if (previousErrorKind == 2) --state.progressWrongStateCount;
            if (nextErrorKind == 1) ++state.progressWrongTypeCount;
            else if (nextErrorKind == 2) ++state.progressWrongStateCount;
            state.progressErrorKind[index] = nextErrorKind;
            changes.errors = true;
        }
        // Progress always describes the whole structure. Hidden layers are
        // still checked above, but their correction/model meshes remain
        // suppressed by the layer renderer.
        if (!visible) return;
        if (state.correctionStates[index] != nextState) {
            state.correctionStates[index] = nextState;
            markSectionDirty(state, state.blockToSection[index]);
            // A missing-cell shell omits faces shared with adjacent missing
            // cells. If either side changes, both section meshes may need an
            // exposed face added or removed (including across 16^3 borders).
            constexpr int neighbors[6][3] = {
                {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                {0, 1, 0}, {0, 0, -1}, {0, 0, 1}
            };
            for (auto const& delta : neighbors) {
                auto const neighbor = state.expectedWorldBlockIndices->find(std::tuple{
                    position.x + delta[0], position.y + delta[1], position.z + delta[2]
                });
                if (neighbor != state.expectedWorldBlockIndices->end()) {
                    markSectionDirty(state, state.blockToSection[neighbor->second]);
                }
            }
        }
    };

    auto const hasExpectedLocalCell = [&](BlockPos const& localPosition) {
        auto const found = std::lower_bound(
            state.structure->renderBlocks.begin(),
            state.structure->renderBlocks.end(),
            localPosition,
            [](structure::LoadedStructure::RenderBlock const& entry, BlockPos const& position) {
                return std::tie(entry.x, entry.y, entry.z)
                    < std::tie(position.x, position.y, position.z);
            }
        );
        return found != state.structure->renderBlocks.end()
            && found->x == localPosition.x
            && found->y == localPosition.y
            && found->z == localPosition.z;
    };

    auto const updateExtra = [&](
        BlockPos const& localPosition,
        BlockPos const& worldPosition,
        bool            visible
    ) {
        auto const key = SubChunkKey{localPosition.x, localPosition.y, localPosition.z};
        bool const isExtra = !region.getBlock(worldPosition).isAir();
        auto const detected = state.detectedExtraBlockPositions.find(key);
        if (isExtra != (detected != state.detectedExtraBlockPositions.end())) {
            if (isExtra) {
                state.detectedExtraBlockPositions.insert(key);
                ++state.progressExtraCount;
            } else {
                state.detectedExtraBlockPositions.erase(detected);
                --state.progressExtraCount;
            }
            changes.errors = true;
        }

        bool const shouldRender = isExtra && visible;
        auto const rendered = state.extraBlockPositions.find(key);
        if (shouldRender == (rendered != state.extraBlockPositions.end())) return;

        std::size_t section{};
        if (shouldRender) {
            auto const transformedPosition = transformStructurePosition(
                localPosition, *state.structure, mirrorMode, rotationTurns
            );
            section = ensureCorrectionSection(state, localPosition, transformedPosition);
            state.extraBlockPositions.insert(key);
            state.sectionExtraBlockPositions[section].insert(key);
        } else {
            auto const sectionFound = state.localSectionIndices.find(localSectionKey(localPosition));
            if (sectionFound == state.localSectionIndices.end()) return;
            section = sectionFound->second;
            state.extraBlockPositions.erase(rendered);
            state.sectionExtraBlockPositions[section].erase(key);
        }
        markSectionDirty(state, section);
        constexpr int neighbors[6][3] = {
            {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
            {0, 1, 0}, {0, 0, -1}, {0, 0, 1}
        };
        for (auto const& delta : neighbors) {
            BlockPos const neighbor{
                localPosition.x + delta[0],
                localPosition.y + delta[1],
                localPosition.z + delta[2],
            };
            auto const found = state.localSectionIndices.find(localSectionKey(neighbor));
            if (found != state.localSectionIndices.end()) markSectionDirty(state, found->second);
        }
    };

    // The cache is populated once after a structure/transform change. Once
    // that pass completes, a stable world performs no correction reads.
    // Official BlockSource notifications update only changed cells.
    auto const changedPositions = takePendingBlockChanges(kCorrectionChecksPerFrame);
    std::size_t correctionChecks{};
    for (auto const& change : changedPositions) {
        auto const& changedPosition = change.position;
        auto const found = state.expectedWorldBlockIndices->find(std::tuple{
            changedPosition.x, changedPosition.y, changedPosition.z
        });
        if (found != state.expectedWorldBlockIndices->end()) {
            if (change.destroyedAt != 0) {
                state.pendingBrokenCells.push_back(BrokenProjectionCell{
                    changedPosition.x,
                    changedPosition.y,
                    changedPosition.z,
                    change.destroyedAt,
                });
            }
            updateCorrection(found->second);
            ++correctionChecks;
            continue;
        }
        BlockPos const transformed{
            changedPosition.x - state.anchor.x - offsetX,
            changedPosition.y - state.anchor.y - offsetY,
            changedPosition.z - state.anchor.z - offsetZ,
        };
        auto const local = inverseTransformStructurePosition(
            transformed, *state.structure, mirrorMode, rotationTurns
        );
        if (isStructureCellCovered(*state.structure, local)
            && !hasExpectedLocalCell(local)) {
            auto const visible = isLayerVisible(
                layerAxis == 1 ? local.x : local.y,
                layerDisplayMode,
                displayLayer,
                -1,
                -1,
                layerAxis
            );
            updateExtra(local, changedPosition, visible);
            ++correctionChecks;
        }
    }

    auto const loadedSubChunks = takePendingLoadedSubChunks(kSubChunkEventsPerFrame);
    state.pendingLoadedSubChunks.insert(loadedSubChunks.begin(), loadedSubChunks.end());

    auto const scanRemaining = totalBlocks - state.correctionScanCursor;
    auto const checks = std::min(scanRemaining, kCorrectionChecksPerFrame - correctionChecks);
    for (std::size_t checked = 0; checked < checks; ++checked) {
        updateCorrection(state.correctionScanCursor++);
    }
    correctionChecks += checks;

    // Air cells have no render-block index, so discover extras with a separate
    // cursor while sharing the same fixed per-frame correction budget. Region
    // boxes preserve litematic gaps and avoid scanning their merged bounds.
    while (correctionChecks < kCorrectionChecksPerFrame
        && state.extraScanRegion < state.structure->regions.size()) {
        auto const& box = state.structure->regions[state.extraScanRegion];
        auto const regionVolume = static_cast<std::uint64_t>(box.sizeX)
            * static_cast<std::uint64_t>(box.sizeY) * static_cast<std::uint64_t>(box.sizeZ);
        if (state.extraScanCell >= regionVolume) {
            ++state.extraScanRegion;
            state.extraScanCell = 0;
            continue;
        }
        auto const yz = static_cast<std::uint64_t>(box.sizeY) * box.sizeZ;
        auto const x = state.extraScanCell / yz;
        auto const remainder = state.extraScanCell % yz;
        auto const y = remainder / static_cast<std::uint64_t>(box.sizeZ);
        auto const z = remainder % static_cast<std::uint64_t>(box.sizeZ);
        ++state.extraScanCell;
        ++correctionChecks;
        BlockPos const local{
            box.x + static_cast<int>(x),
            box.y + static_cast<int>(y),
            box.z + static_cast<int>(z),
        };
        auto const visible = isLayerVisible(
            layerAxis == 1 ? local.x : local.y,
            layerDisplayMode,
            displayLayer,
            -1,
            -1,
            layerAxis
        );
        if (hasExpectedLocalCell(local)) continue;
        auto const transformed = transformStructurePosition(
            local, *state.structure, mirrorMode, rotationTurns
        );
        BlockPos const world{
            state.anchor.x + offsetX + transformed.x,
            state.anchor.y + offsetY + transformed.y,
            state.anchor.z + offsetZ + transformed.z,
        };
        updateExtra(local, world, visible);
    }

    // A newly received client subchunk may not emit one block notification per
    // cell. Refresh only its projected cells, capped to one 16^3 region/frame.
    if (state.correctionScanCursor == totalBlocks
        && state.extraScanRegion == state.structure->regions.size()
        && correctionChecks == 0
        && !state.pendingLoadedSubChunks.empty()) {
        auto loaded = state.pendingLoadedSubChunks.begin();
        auto const [subChunkX, subChunkY, subChunkZ] = *loaded;
        state.pendingLoadedSubChunks.erase(loaded);
        auto const minX = subChunkX * 16;
        auto const minY = subChunkY * 16;
        auto const minZ = subChunkZ * 16;
        for (int x = minX; x < minX + 16; ++x) {
            for (int y = minY; y < minY + 16; ++y) {
                for (int z = minZ; z < minZ + 16; ++z) {
                    auto const found = state.expectedWorldBlockIndices->find(std::tuple{x, y, z});
                    if (found != state.expectedWorldBlockIndices->end()) {
                        updateCorrection(found->second);
                        continue;
                    }
                    BlockPos const transformed{
                        x - state.anchor.x - offsetX,
                        y - state.anchor.y - offsetY,
                        z - state.anchor.z - offsetZ,
                    };
                    auto const local = inverseTransformStructurePosition(
                        transformed, *state.structure, mirrorMode, rotationTurns
                    );
                    if (!isStructureCellCovered(*state.structure, local)
                        || hasExpectedLocalCell(local)) {
                        continue;
                    }
                    auto const visible = isLayerVisible(
                        layerAxis == 1 ? local.x : local.y,
                        layerDisplayMode,
                        displayLayer,
                        -1,
                        -1,
                        layerAxis
                    );
                    updateExtra(local, BlockPos{x, y, z}, visible);
                }
            }
        }
    }
    return changes;
}

} // namespace lholo::projection::detail
