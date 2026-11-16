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
#include "place/AimedProjectedBlock.h"
#include "place/LocalPlacementMode.h"

namespace lholo::place {

void setEnabled(bool enabled);
bool isEnabled();
// Range placement: auto-place missing projection cells within a radius of the
// player (still bounded by the player's actual placement reach).
void setRangeEnabled(bool enabled);
bool isRangeEnabled();
void setLocalPlacementMode(LocalPlacementMode mode);
LocalPlacementMode getLocalPlacementMode();
bool localRangeAvailable();
void setPlacementRadius(int radius);
int  getPlacementRadius();
void setLocalPlacementRadius(int radius);
int getLocalPlacementRadius();
void setLocalPlacementLiquidsDisabled(bool disabled);
bool getLocalPlacementLiquidsDisabled();
void setLocalShulkerRestock(bool enabled);
bool getLocalShulkerRestock();
void setLocalCreativeRestock(bool enabled);
bool getLocalCreativeRestock();
void setLocalCreativeRestockOverwrite(bool enabled);
bool getLocalCreativeRestockOverwrite();
void setAutoPlacementBreakCooldownSeconds(int seconds);
int  getAutoPlacementBreakCooldownSeconds();
// Manual mode: only place while the right mouse button is held, instead of
// placing automatically. Applies to both easy-place and range placement.
void setManualMode(bool manual);
bool isManualMode();
// Display name of the projected blueprint block currently under the crosshair.
AimedProjectedBlock getAimedProjectedBlock();

// Clears actions and coordinate-keyed caches owned by the previous dimension
// while preserving the user's active assisted-placement modes and settings.
void resetDimensionSession();

// Clears state that belongs to the current world while preserving user
// configuration such as placement radius and cooldown.
void resetWorldSession();

bool installHook();
void uninstallHook();

} // namespace lholo::place
