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

#include <string>

namespace lholo::place {

void setEnabled(bool enabled);
bool isEnabled();
// Range placement: auto-place missing projection cells within a radius of the
// player (still bounded by the player's actual placement reach).
void setRangeEnabled(bool enabled);
bool isRangeEnabled();
void setPlacementRadius(int radius);
int  getPlacementRadius();
// Manual mode: only place while the right mouse button is held, instead of
// placing automatically. Applies to both easy-place and range placement.
void setManualMode(bool manual);
bool isManualMode();
// Name of the block-entity block (chest, sign, hopper, ...) the crosshair
// points at, for the HUD to identify projected placeholder boxes.
std::string getAimedBlockEntityName();

bool installHook();
void uninstallHook();

} // namespace lholo::place
