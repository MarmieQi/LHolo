// LHolo - View-relative move steps for the projection offset hotkeys
//
// Layering: pure math over plain floats. It must not include any Minecraft or
// LeviLamina header, so the logic tests can cover every direction rule; the
// caller (StructureLoader) is the layer that reads the player.

#pragma once

#include "input/HotkeyTypes.h"

namespace lholo::input {

// One whole-block step in world coordinates.
struct ViewMoveStep {
    int  dx{};
    int  dy{};
    int  dz{};
    // False when there is no direction to apply; callers must not queue a step.
    bool valid{};
};

// Step produced by one move hotkey for a player facing `yawDegrees` (the value
// of Actor::getRotation().y, where 0 = south, 90 = west, 180 = north, -90 =
// east).
//
// Forward/back follow the facing and left/right are perpendicular to it; all
// four stay horizontal, so pitch never participates and a player looking
// straight down can still step sideways. Up/down stay on the world Y axis.
//
// The dominant horizontal axis wins, so a step always changes exactly one
// coordinate and diagonal facings never produce a diagonal step; ties (facing
// exactly along a diagonal) resolve to X so the same facing is always the same
// step.
[[nodiscard]] ViewMoveStep viewRelativeMoveStep(HotkeyId move, float yawDegrees);

// Step along a view vector (the fixed Alt+wheel gesture). Unlike the hotkeys
// above this keeps its own rule: pitch participates and every axis rounds
// independently, so a diagonal view produces a diagonal step. `steps` is the
// wheel's whole-notch count.
[[nodiscard]] ViewMoveStep viewForwardStep(float viewX, float viewY, float viewZ, int steps);

} // namespace lholo::input
