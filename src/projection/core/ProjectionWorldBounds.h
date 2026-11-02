#pragma once

#include <array>

namespace lholo::projection::detail {

// Pure integer bounds: inclusive minimum/exclusive maximum in world cells. Wide coordinates keep
// event routing safe at negative positions and near the integer world limits.
struct ProjectionWorldBounds {
    std::array<long long, 3> minimum{}, maximum{};
    bool intersects(std::array<long long, 3> const& lo, std::array<long long, 3> const& hi) const {
        for (int axis = 0; axis < 3; ++axis)
            if (minimum[axis] >= maximum[axis] || hi[axis] < minimum[axis] || lo[axis] >= maximum[axis])
                return false;
        return true;
    }
};

} // namespace lholo::projection::detail
