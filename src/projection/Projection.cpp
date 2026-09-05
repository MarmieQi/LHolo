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
#include "projection/core/ProjectionState.h"
#include "projection/world/ProjectionQueries.h"

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

float getOpacity() {
    return detail::ProjectionSession::getInstance().opacity();
}

void setOpacity(float opacity) {
    detail::ProjectionSession::getInstance().setOpacity(opacity);
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

void requestNextStructureAnchor(int x, int y, int z) {
    detail::ProjectionSession::getInstance().requestAnchor(x, y, z);
}

void cancelNextStructureAnchorRequest() {
    detail::ProjectionSession::getInstance().cancelAnchorRequest();
}

bool consumeWorldExitRequest() {
    return detail::consumeWorldExitRequest();
}

bool isDimensionSuspended() {
    return detail::ProjectionSession::getInstance().dimensionSuspended();
}

BuildProgress getBuildProgress() {
    return detail::getPublishedBuildProgress();
}

std::optional<MaterialProgressKey> getMaterialProgressKey() {
    return detail::ProjectionSession::getInstance().withLockedState(
        [](detail::ProjectionState& state, overlay::BoundsWireframe&)
            -> std::optional<MaterialProgressKey> {
            if (!state.enabled || !state.structure
                || state.cachedLayerDisplayMode < 0 || state.cachedLayerAxis < 0
                || state.correctionScanCursor != state.structure->renderBlocks.size()) {
                return std::nullopt;
            }
            return MaterialProgressKey{
                state.structureGeneration,
                state.activationGeneration,
                state.progressRevision,
                state.cachedLayerDisplayMode,
                state.cachedDisplayLayer,
                state.cachedLayerAxis,
            };
        }
    );
}

std::optional<MaterialProgressSnapshot> captureMaterialProgress(
    MaterialProgressKey const& expected
) {
    return detail::ProjectionSession::getInstance().withLockedState(
        [&expected](detail::ProjectionState& state, overlay::BoundsWireframe&)
            -> std::optional<MaterialProgressSnapshot> {
            auto const matches = state.enabled && state.structure
                && state.correctionScanCursor == state.structure->renderBlocks.size()
                && state.structureGeneration == expected.structureGeneration
                && state.activationGeneration == expected.activationGeneration
                && state.progressRevision == expected.progressRevision
                && state.cachedLayerDisplayMode == expected.layerDisplayMode
                && state.cachedDisplayLayer == expected.displayLayer
                && state.cachedLayerAxis == expected.layerAxis;
            if (!matches) return std::nullopt;
            return MaterialProgressSnapshot{expected, state.structure, state.progressCorrect};
        }
    );
}

bool isLayerVisible(
    int layer,
    int layerDisplayMode,
    int displayLayer,
    int materialIndex,
    int secondaryMaterialIndex,
    int layerAxis
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
        }
    );
    return result;
}

ProjectionQuery queryProjection(LocalPlayer& player, BlockPos const& worldPos) {
    ProjectionQuery result{nullptr, false};
    detail::ProjectionSession::getInstance().withLockedState(
        [&](detail::ProjectionState& state, overlay::BoundsWireframe&) {
            if (!state.enabled || !state.structure) return;
            if (!projectionWorldViewMatches(state, player)) return;
            result = detail::queryProjectionCell(state, worldPos);
        }
    );
    return result;
}

std::vector<RangeCandidate> queryMissingCellsInRange(LocalPlayer& player, Vec3 const& center, float radius) {
    std::vector<RangeCandidate> result;
    detail::ProjectionSession::getInstance().withLockedState(
        [&](detail::ProjectionState& state, overlay::BoundsWireframe&) {
            if (!state.enabled || !state.structure) return;
            if (!projectionWorldViewMatches(state, player)) return;
            result = detail::queryMissingProjectionCells(state, center, radius);
        }
    );
    return result;
}

} // namespace lholo::projection
