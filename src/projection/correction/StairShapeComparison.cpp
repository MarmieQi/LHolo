#include "projection/correction/StairShapeComparison.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace lholo::projection::detail {

bool sameStairShape(std::span<AABB const> expected, std::span<AABB const> actual) {
    if (expected.empty() || actual.empty()) return false;
    std::array<std::vector<float>, 3> planes;
    auto append = [&](std::span<AABB const> boxes) {
        for (auto const& box : boxes) {
            std::array const lower{box.min.x, box.min.y, box.min.z};
            std::array const upper{box.max.x, box.max.y, box.max.z};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (!std::isfinite(lower[axis]) || !std::isfinite(upper[axis])
                    || lower[axis] >= upper[axis]) return false;
                planes[axis].push_back(lower[axis]);
                planes[axis].push_back(upper[axis]);
            }
        }
        return true;
    };
    if (!append(expected) || !append(actual)) return false;
    for (auto& axis : planes) {
        std::sort(axis.begin(), axis.end());
        axis.erase(std::unique(axis.begin(), axis.end()), axis.end());
    }
    auto covers = [](std::span<AABB const> boxes, AABB const& cell) {
        return std::any_of(boxes.begin(), boxes.end(), [&](AABB const& box) {
            return box.min.x <= cell.min.x && box.max.x >= cell.max.x
                && box.min.y <= cell.min.y && box.max.y >= cell.max.y
                && box.min.z <= cell.min.z && box.max.z >= cell.max.z;
        });
    };
    // Every box boundary belongs to this grid. Each open grid cell is either
    // fully occupied or fully empty, so this compares the exact union without
    // voxel sampling or an epsilon that could accept a different stair shape.
    for (std::size_t x = 1; x < planes[0].size(); ++x)
        for (std::size_t y = 1; y < planes[1].size(); ++y)
            for (std::size_t z = 1; z < planes[2].size(); ++z) {
                AABB const cell{
                    Vec3{planes[0][x - 1], planes[1][y - 1], planes[2][z - 1]},
                    Vec3{planes[0][x], planes[1][y], planes[2][z]}
                };
                if (covers(expected, cell) != covers(actual, cell)) return false;
            }
    return true;
}

} // namespace lholo::projection::detail
