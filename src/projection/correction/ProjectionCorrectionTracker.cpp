// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/correction/ProjectionCorrectionTracker.h"

#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionState.h"
#include "projection/runtime/ProjectionWorldEvents.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <tuple>

#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

namespace lholo::projection::detail {
namespace {

// A game-driven runtime variant (lit lamp/ore, burning furnace, powered
// repeater/comparator, powered-off torch) shares a base block with its resting
// form. Comparing base names keeps such a mismatch out of WrongType (red) — it
// falls through to WrongState (orange, "same block, wrong state") instead.
std::string_view runtimeBaseName(std::string_view name) {
    if (name == "minecraft:lit_redstone_lamp")          return "minecraft:redstone_lamp";
    if (name == "minecraft:lit_redstone_ore")           return "minecraft:redstone_ore";
    if (name == "minecraft:lit_deepslate_redstone_ore") return "minecraft:deepslate_redstone_ore";
    if (name == "minecraft:lit_furnace")                return "minecraft:furnace";
    if (name == "minecraft:lit_blast_furnace")          return "minecraft:blast_furnace";
    if (name == "minecraft:lit_smoker")                 return "minecraft:smoker";
    if (name == "minecraft:unlit_redstone_torch")       return "minecraft:redstone_torch";
    if (name == "minecraft:powered_repeater")           return "minecraft:unpowered_repeater";
    if (name == "minecraft:powered_comparator")         return "minecraft:unpowered_comparator";
    return name;
}

void markSectionDirty(ProjectionState& state, std::size_t section) {
    if (section >= state.sections.size()) return;
    auto& sectionState = state.sections[section];
    sectionState.dirty = true;
    sectionState.incrementalDirty = true;
    ++sectionState.requestedRevision;
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
            layerAxis == 1 ? entry.x : entry.y, layerDisplayMode, displayLayer
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
            && runtimeBaseName(actual.getTypeName()) != runtimeBaseName(expected->getTypeName());
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

    // A newly received client subchunk may not emit one block notification per
    // cell. Refresh only its projected cells, capped to one 16^3 region/frame.
    if (state.correctionScanCursor == totalBlocks && correctionChecks == 0
        && !state.pendingLoadedSubChunks.empty()) {
        auto loaded = state.pendingLoadedSubChunks.begin();
        auto const [subChunkX, subChunkY, subChunkZ] = *loaded;
        state.pendingLoadedSubChunks.erase(loaded);
        auto const minX = subChunkX * 16;
        auto const minY = subChunkY * 16;
        auto const minZ = subChunkZ * 16;
        for (int x = minX; x < minX + 16; ++x) {
            for (int y = minY; y < minY + 16; ++y) {
                auto found = state.expectedWorldBlockIndices->lower_bound(std::tuple{x, y, minZ});
                while (found != state.expectedWorldBlockIndices->end()) {
                    auto const& [foundX, foundY, foundZ] = found->first;
                    if (foundX != x || foundY != y || foundZ >= minZ + 16) break;
                    updateCorrection(found->second);
                    ++found;
                }
            }
        }
    }
    return changes;
}

} // namespace lholo::projection::detail
