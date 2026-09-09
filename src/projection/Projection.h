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

#pragma once

#include <optional>
#include <vector>

#include "projection/ProjectionTypes.h"

class BlockPos;
class LocalPlayer;
class Vec3;

namespace lholo::projection {

bool installHook();
void uninstallHook();

void disable();
float getOpacity();
void setOpacity(float opacity);
float getCorrectionFillOpacity();
void setCorrectionFillOpacity(float opacity);
float getCorrectionOutlineOpacity();
void setCorrectionOutlineOpacity(float opacity);
bool getStructureBoundsEnabled();
void setStructureBoundsEnabled(bool enabled);
bool getCorrectionSeeThrough();
void setCorrectionSeeThrough(bool enabled);
bool getMissingSeeThrough();
void setMissingSeeThrough(bool enabled);
void requestNextStructureAnchor(int x, int y, int z);
void cancelNextStructureAnchorRequest();
// Consumes the lightweight signal produced by the existing LevelListener when
// the active world is destroyed. Heavy cleanup runs outside the callback.
bool consumeWorldExitRequest();
bool isDimensionSuspended();
BuildProgress getBuildProgress();
// Lightweight version query followed by a conditional snapshot capture. This
// keeps ProjectionState and its lock private while avoiding a large byte-vector
// copy when the caller already holds the current version.
std::optional<MaterialProgressKey> getMaterialProgressKey();
std::optional<MaterialProgressSnapshot> captureMaterialProgress(
    MaterialProgressKey const& expected
);
bool isLayerVisible(
    int layer,
    int layerDisplayMode,
    int displayLayer,
    int materialIndex = -1,
    int secondaryMaterialIndex = -1,
    int layerAxis = 0
);
std::vector<BrokenProjectionCell> takeBrokenProjectionCells(LocalPlayer& player);

ProjectionQuery queryProjection(LocalPlayer& player, BlockPos const& worldPos);

std::vector<RangeCandidate> queryMissingCellsInRange(LocalPlayer& player, Vec3 const& center, float radius);

} // namespace lholo::projection
