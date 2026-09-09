// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Placement planning and execution. The executor owns the easy/range tick
// logic; game hooks stay in PlaceHelper and only call the public entry points.

#pragma once

#include "mc/deps/core/math/Vec3.h"

class LocalPlayer;

namespace lholo::place::detail {

struct PlacementContext {
    Vec3  eye;
    float reachSquared;
    int   eyeX;
    int   eyeY;
    int   eyeZ;
    int   viewX;
    int   viewY;
    int   viewZ;
};

void tickEasyPlace();

enum class ManualTargetStatus {
    None,
    MissingMaterial,
    Ready,
};

// Fresh synchronous raytrace used by manual-mode hooks. Target detection and
// inventory availability stay separate so a missing item is never reported as
// an aiming failure or handed back to vanilla use-item handling.
ManualTargetStatus manualTargetStatusUnderCrosshair();

void tickRangePlace(LocalPlayer& player, PlacementContext const& context);

} // namespace lholo::place::detail
