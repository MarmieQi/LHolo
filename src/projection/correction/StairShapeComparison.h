#pragma once

#include "mc/world/phys/AABB.h"

#include <span>

namespace lholo::projection::detail {

// Compare the occupied volume of native stair shapes, independent of cuboid
// order, overlap or subdivision. Empty/invalid native results never match.
bool sameStairShape(std::span<AABB const> expected, std::span<AABB const> actual);

} // namespace lholo::projection::detail
