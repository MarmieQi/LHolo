// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Placement planning and execution. The executor owns the easy/range tick
// logic; game hooks stay in PlaceHelper and only call the public entry points.

#pragma once

#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"

class LocalPlayer;
class Block;

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

// Fresh, synchronous check of whether the crosshair is over a placeable missing
// projection cell (raytrace at call time). The manual-mode build hooks use it to
// decide whether to place, or to let vanilla interact / show the blocked hint.
bool manualTargetUnderCrosshair();

// How many of each block the player currently holds, matched item-for-item to
// what LHolo would place. Same order/size as `blockNames`.
std::vector<int> availableCounts(std::vector<std::string> const& blockNames);

// Max stack size of the item `block` resolves to (64 normally, 16 for signs
// etc., 1 for filled buckets); 64 when it maps to no real item. Touches the
// item registry, so call it on the game tick thread only.
int maxStackForBlock(Block const& block);

void tickRangePlace(LocalPlayer& player, PlacementContext const& context);

} // namespace lholo::place::detail
