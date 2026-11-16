// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Read-only projection cell queries used by assisted placement. Locking and
// world identity validation remain in the public Projection.cpp boundary.

#pragma once

#include "projection/ProjectionTypes.h"

#include <vector>

class BlockPos;
class Vec3;

namespace lholo::projection::detail {

struct ProjectionState;

ProjectionQuery queryProjectionCell(
    ProjectionState const& state,
    BlockPos const&        worldPosition,
    bool                   localPlacement
);

std::vector<RangeCandidate> queryMissingProjectionCells(
    ProjectionState const& state,
    Vec3 const&            center,
    float                  radius,
    bool                   localPlacement
);

} // namespace lholo::projection::detail
