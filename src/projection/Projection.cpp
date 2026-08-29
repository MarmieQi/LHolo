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
#include "projection/runtime/ProjectionProgress.h"
#include "projection/runtime/ProjectionSession.h"
#include "projection/runtime/ProjectionLifecycle.h"
#include "projection/core/ProjectionState.h"
#include "projection/world/ProjectionQueries.h"

#include "structure/StructureLoader.h"

#include <vector>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"

namespace lholo::projection {

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

bool getProjectionSeeThrough() {
    return detail::ProjectionSession::getInstance().projectionSeeThrough();
}

void setProjectionSeeThrough(bool enabled) {
    detail::ProjectionSession::getInstance().setProjectionSeeThrough(enabled);
}

void requestNextStructureAnchor(int x, int y, int z) {
    detail::ProjectionSession::getInstance().requestAnchor(x, y, z);
}

void cancelNextStructureAnchorRequest() {
    detail::ProjectionSession::getInstance().cancelAnchorRequest();
}

BuildProgress getBuildProgress() {
    return detail::getPublishedBuildProgress();
}

std::vector<BrokenProjectionCell> takeBrokenProjectionCells(LocalPlayer& player) {
    std::vector<BrokenProjectionCell> result;
    detail::ProjectionSession::getInstance().withLockedState(
        [&](detail::ProjectionState& state, overlay::BoundsWireframe&) {
            if (!state.enabled || !state.structure) return;
            if (state.level != &player.getLevel() || state.dimension != &player.getDimension()) return;
            result.swap(state.pendingBrokenCells);
        }
    );
    return result;
}

ProjectionQuery queryProjection(LocalPlayer& player, BlockPos const& worldPos) {
    ProjectionQuery result{nullptr, false};
    bool clearStructure = false;
    detail::ProjectionSession::getInstance().withLockedState(
        [&](detail::ProjectionState& state, overlay::BoundsWireframe&) {
            if (!state.enabled || !state.structure) return;
            if (state.level != &player.getLevel() || state.dimension != &player.getDimension()) {
                detail::resetProjectionState(state);
                clearStructure = true;
                return;
            }
            result = detail::queryProjectionCell(state, worldPos);
        }
    );
    if (clearStructure) structure::clear();
    return result;
}

std::vector<RangeCandidate> queryMissingCellsInRange(LocalPlayer& player, Vec3 const& center, float radius) {
    std::vector<RangeCandidate> result;
    bool clearStructure = false;
    detail::ProjectionSession::getInstance().withLockedState(
        [&](detail::ProjectionState& state, overlay::BoundsWireframe&) {
            if (!state.enabled || !state.structure) return;
            if (state.level != &player.getLevel() || state.dimension != &player.getDimension()) {
                detail::resetProjectionState(state);
                clearStructure = true;
                return;
            }
            result = detail::queryMissingProjectionCells(state, center, radius);
        }
    );
    if (clearStructure) structure::clear();
    return result;
}

} // namespace lholo::projection
