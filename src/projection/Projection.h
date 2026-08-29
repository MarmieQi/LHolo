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
bool getProjectionSeeThrough();
void setProjectionSeeThrough(bool enabled);
void requestNextStructureAnchor(int x, int y, int z);
void cancelNextStructureAnchorRequest();
BuildProgress getBuildProgress();
std::vector<BrokenProjectionCell> takeBrokenProjectionCells(LocalPlayer& player);

ProjectionQuery queryProjection(LocalPlayer& player, BlockPos const& worldPos);

std::vector<RangeCandidate> queryMissingCellsInRange(LocalPlayer& player, Vec3 const& center, float radius);

} // namespace lholo::projection
