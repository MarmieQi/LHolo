// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "projection/Projection.h"
#include "projection/ProjectionController.h"
#include "projection/core/ProjectionRules.h"
#include "projection/runtime/ProjectionProgress.h"
#include "projection/runtime/ProjectionSession.h"
#include "projection/runtime/ProjectionWorldEvents.h"
#include "projection/runtime/IndependentFileProjection.h"
#include "projection/core/ProjectionState.h"
#include "projection/world/ProjectionQueries.h"
#include "structure/StructureSession.h"

#include <vector>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"

namespace lholo::projection {
namespace {

bool projectionWorldViewMatches(detail::ProjectionState const& state, LocalPlayer& player) {
    return state.level == &player.getLevel() && state.dimension == &player.getDimension();
}

} // namespace

bool installHook() {
    return detail::projectionController().installHooks();
}

void uninstallHook() {
    detail::projectionController().uninstallHooks();
}

void disable() {
    detail::projectionController().disableProjection();
}

float getCorrectionFillOpacity() {
    return detail::ProjectionSession::getInstance().correctionFillOpacity();
}

void setCorrectionFillOpacity(float opacity) {
    detail::ProjectionSession::getInstance().setCorrectionFillOpacity(opacity);
}

float getCorrectionOutlineOpacity() {
    return detail::ProjectionSession::getInstance().correctionOutlineOpacity();
}

void setCorrectionOutlineOpacity(float opacity) {
    detail::ProjectionSession::getInstance().setCorrectionOutlineOpacity(opacity);
}

bool getStructureBoundsEnabled() {
    return detail::ProjectionSession::getInstance().structureBoundsEnabled();
}

void setStructureBoundsEnabled(bool enabled) {
    detail::ProjectionSession::getInstance().setStructureBoundsEnabled(enabled);
}

bool getCorrectionSeeThrough() {
    return detail::ProjectionSession::getInstance().correctionSeeThrough();
}

void setCorrectionSeeThrough(bool enabled) {
    detail::ProjectionSession::getInstance().setCorrectionSeeThrough(enabled);
}

bool getMissingSeeThrough() {
    return detail::ProjectionSession::getInstance().missingSeeThrough();
}

void setMissingSeeThrough(bool enabled) {
    detail::ProjectionSession::getInstance().setMissingSeeThrough(enabled);
}

bool consumeWorldExitRequest() {
    return detail::consumeWorldExitRequest();
}

bool isDimensionSuspended() {
    return detail::ProjectionSession::getInstance().dimensionSuspended();
}

BuildProgress getBuildProgress() {
    return detail::ProjectionSession::getInstance().withLockedState(
        [](detail::ProjectionState &state, overlay::BoundsWireframe &) {
            if (!state.enabled || !state.structure || !state.structure->independentFiles)
                return detail::getPublishedBuildProgress();
            auto *file = detail::selectedFileProjection(state);
            return file ? file->fileProgress : BuildProgress{};
        });
}

std::optional<MaterialProgressKey> getMaterialProgressKey() {
    return detail::ProjectionSession::getInstance().withLockedState(
        [](detail::ProjectionState& root, overlay::BoundsWireframe&)
            -> std::optional<MaterialProgressKey> {
            auto* selected=detail::selectedFileProjection(root);
            if (!root.enabled || !selected) return std::nullopt;
            auto& state=*selected;
            if (!state.enabled || !state.structure || !state.cachedLayerDisplayMode || !state.cachedLayerAxis ||
                !state.initialCorrectionScan.complete() || !state.taskSectionRefresh.empty()) {
                return std::nullopt;
            }
            return MaterialProgressKey{
                state.structureGeneration,
                state.activationGeneration,
                state.progressRevision,
                state.progressDeltaSeq,
                *state.cachedLayerDisplayMode,
                state.cachedDisplayLayer,
                *state.cachedLayerAxis,
            };
        }
    );
}

std::optional<MaterialProgressSnapshot> captureMaterialProgress(
    MaterialProgressKey const& expected
) {
    return detail::ProjectionSession::getInstance().withLockedState(
        [&expected](detail::ProjectionState& root, overlay::BoundsWireframe&)
            -> std::optional<MaterialProgressSnapshot> {
            auto* selected=detail::selectedFileProjection(root);
            if (!root.enabled || !selected) return std::nullopt;
            auto& state=*selected;
            auto const matches = state.enabled && state.structure && state.initialCorrectionScan.complete() &&
                                 state.structureGeneration == expected.structureGeneration &&
                                 state.activationGeneration == expected.activationGeneration &&
                                 state.progressRevision == expected.progressRevision &&
                                 state.progressDeltaSeq == expected.deltaSeq &&
                                 state.cachedLayerDisplayMode == expected.layerDisplayMode &&
                                 state.cachedDisplayLayer == expected.displayLayer &&
                                 state.cachedLayerAxis == expected.layerAxis;
            if (!matches) return std::nullopt;
            auto task = state.worldLookup->task();
            return MaterialProgressSnapshot{expected, state.structure, state.progressCorrect,
                                            task ? task->placements : nullptr, state.worldLookup->fileIndex()};
        }
    );
}

std::optional<MaterialProgressDelta> takeMaterialProgressDelta(
    MaterialProgressKey const& expected,
    bool requireSameLayer
) {
    return detail::ProjectionSession::getInstance().withLockedState(
        [&expected, requireSameLayer](detail::ProjectionState& root, overlay::BoundsWireframe&)
            -> std::optional<MaterialProgressDelta> {
            auto* selected = detail::selectedFileProjection(root);
            if (!root.enabled || !selected) return std::nullopt;
            auto& state = *selected;
            bool const sameLayer = !requireSameLayer
                || (state.cachedLayerDisplayMode == expected.layerDisplayMode
                    && state.cachedDisplayLayer == expected.displayLayer
                    && state.cachedLayerAxis == expected.layerAxis);
            bool const sameContext = state.enabled && state.structure
                && state.initialCorrectionScan.complete()
                && state.taskSectionRefresh.empty()
                && state.structureGeneration == expected.structureGeneration
                && state.activationGeneration == expected.activationGeneration
                && sameLayer;
            if (!sameContext || expected.deltaSeq < state.progressDeltaMinSeq
                || expected.deltaSeq > state.progressDeltaSeq) {
                return std::nullopt;
            }
            MaterialProgressKey const currentKey{
                state.structureGeneration,
                state.activationGeneration,
                state.progressRevision,
                state.progressDeltaSeq,
                *state.cachedLayerDisplayMode,
                state.cachedDisplayLayer,
                *state.cachedLayerAxis,
            };
            std::vector<MaterialProgressFlip> flips;
            for (auto const& entry : state.progressDeltas) {
                if (entry.seq > expected.deltaSeq) {
                    flips.push_back({entry.index, entry.correct});
                }
            }
            return MaterialProgressDelta{currentKey, std::move(flips)};
        }
    );
}

bool isLayerVisible(
    int layer,
    structure::LayerDisplayMode layerDisplayMode,
    int displayLayer,
    int materialIndex,
    int secondaryMaterialIndex,
    structure::LayerAxis layerAxis
) {
    return detail::isLayerVisible(
        layer, layerDisplayMode, displayLayer,
        materialIndex, secondaryMaterialIndex, layerAxis
    );
}

std::vector<BrokenProjectionCell> takeBrokenProjectionCells(LocalPlayer& player) {
    std::vector<BrokenProjectionCell> result;
    detail::ProjectionSession::getInstance().withLockedState(
        [&](detail::ProjectionState& state, overlay::BoundsWireframe&) {
            if (!state.enabled || !state.structure) return;
            if (!projectionWorldViewMatches(state, player)) return;
            result.swap(state.pendingBrokenCells);
            for (auto& file:state.files) {
                result.insert(result.end(),file->pendingBrokenCells.begin(),file->pendingBrokenCells.end());
                file->pendingBrokenCells.clear();
            }
        }
    );
    return result;
}

ProjectionQuery queryProjection(LocalPlayer& player, BlockPos const& worldPos, bool localPlacement) {
    ProjectionQuery result{nullptr, false};
    detail::ProjectionSession::getInstance().withLockedState(
        [&](detail::ProjectionState& state, overlay::BoundsWireframe&) {
            if (!state.enabled || !state.structure) return;
            if (!projectionWorldViewMatches(state, player)) return;
            if (auto* selected=detail::selectedFileProjection(state))
                result = detail::queryProjectionCell(*selected, worldPos, localPlacement);
        }
    );
    return result;
}

std::vector<RangeCandidate> queryMissingCellsInRange(
    LocalPlayer& player, Vec3 const& center, float radius, bool localPlacement) {
    std::vector<RangeCandidate> result;
    detail::ProjectionSession::getInstance().withLockedState(
        [&](detail::ProjectionState& state, overlay::BoundsWireframe&) {
            if (!state.enabled || !state.structure) return;
            if (!projectionWorldViewMatches(state, player)) return;
            if (auto* selected=detail::selectedFileProjection(state))
                result = detail::queryMissingProjectionCells(*selected, center, radius, localPlacement);
        }
    );
    return result;
}

std::optional<RangeCandidate> queryCurrentPlacementCandidate(BlockPos const& position, int dimension) {
    std::optional<RangeCandidate> current;
    detail::ProjectionSession::getInstance().withLockedState(
        [&](detail::ProjectionState& state, overlay::BoundsWireframe&) {
            if (!state.enabled || !state.structure || state.dimensionId != dimension) return;
            auto const* selected = detail::selectedFileProjection(state);
            if (!selected) return;
            auto const query = detail::queryProjectionCell(*selected, position, true);
            if (query.missing) current = RangeCandidate{position.x, position.y, position.z, query.block, query.hasLiquid};
        }
    );
    return current;
}

} // namespace lholo::projection
