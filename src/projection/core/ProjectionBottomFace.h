// Conservative recognition of a downward quad on a cell's exact base plane.
#pragma once

#include <array>
#include <cmath>

namespace lholo::projection::detail {

inline bool projectionBottomFace(
    std::array<std::array<float, 3>, 4> const& vertices, std::array<float, 3> const& cell
) {
    for (auto const& p : vertices) {
        if (!std::isfinite(p[0]) || !std::isfinite(p[2]) || p[1] != cell[1]
            || p[0] < cell[0] || p[0] > cell[0] + 1.0f
            || p[2] < cell[2] || p[2] > cell[2] + 1.0f) return false;
    }
    auto const& a = vertices[0];
    auto const& b = vertices[1];
    auto const& c = vertices[2];
    float const normalY = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
    return normalY < 0.0f;
}

} // namespace lholo::projection::detail
