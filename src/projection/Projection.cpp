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
    return detail::ProjectionSession::getInstance().withLockedState([](detail::ProjectionState& state, overlay::BoundsWireframe&) {
        if (!state.enabled || !state.structure || !state.structure->composed) return detail::getPublishedBuildProgress();
        if (state.structure->independentFiles) {
            auto* file=detail::selectedFileProjection(state);
            if (!file) return BuildProgress{};
            return BuildProgress{file->progressCorrectCount,file->structure->renderBlocks.size(),
                file->progressVisibleCorrectCount,file->progressVisibleTotal,
                file->progressWrongTypeCount,file->progressWrongStateCount,file->progressExtraCount};
        }
        auto source=structure::detail::StructureSession::getInstance().selectedSourceFor(*state.structure);
        return source>=0 && source<static_cast<int>(state.sourceProgress.size()) ? state.sourceProgress[source] : BuildProgress{};
    });
}

std::optional<MaterialProgressKey> getMaterialProgressKey() {
    return detail::ProjectionSession::getInstance().withLockedState(
        [](detail::ProjectionState& root, overlay::BoundsWireframe&)
            -> std::optional<MaterialProgressKey> {
            auto* selected=detail::selectedFileProjection(root);
            if (!root.enabled || !selected) return std::nullopt;
            auto& state=*selected;
            if (!state.enabled || !state.structure
                || !state.cachedLayerDisplayMode || !state.cachedLayerAxis
                || !state.initialCorrectionScan.complete()) {
                return std::nullopt;
            }
            return MaterialProgressKey{
                state.structureGeneration,
                state.activationGeneration,
                state.progressRevision,
                *state.cachedLayerDisplayMode,
                state.cachedDisplayLayer,
                *state.cachedLayerAxis,
                state.structure->composed ? structure::detail::StructureSession::getInstance().selectedSourceFor(*state.structure) : -1,
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
            auto const matches = state.enabled && state.structure
                && state.initialCorrectionScan.complete()
                && state.structureGeneration == expected.structureGeneration
                && state.activationGeneration == expected.activationGeneration
                && state.progressRevision == expected.progressRevision
                && state.cachedLayerDisplayMode == expected.layerDisplayMode
                && state.cachedDisplayLayer == expected.displayLayer
                && state.cachedLayerAxis == expected.layerAxis
                && (!state.structure->composed || structure::detail::StructureSession::getInstance().selectedSourceFor(*state.structure)==expected.selectedSource);
            if (!matches) return std::nullopt;
            return MaterialProgressSnapshot{expected, state.structure, state.progressCorrect};
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

ProjectionQuery queryProjection(LocalPlayer& player, BlockPos const& worldPos) {
    ProjectionQuery result{nullptr, false};
    detail::ProjectionSession::getInstance().withLockedState(
        [&](detail::ProjectionState& state, overlay::BoundsWireframe&) {
            if (!state.enabled || !state.structure) return;
            if (!projectionWorldViewMatches(state, player)) return;
            if (auto* selected=detail::selectedFileProjection(state))
                result = detail::queryProjectionCell(*selected, worldPos);
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
            if (auto* selected=detail::selectedFileProjection(state))
                result = detail::queryMissingProjectionCells(*selected, center, radius);
        }
    );
    return result;
}

} // namespace lholo::projection
