#pragma once
#include "structure/capture/StructureCapture.h"
#include <array>
#include <limits>

namespace lholo::structure::capture {
// Native dimensions and format-1 NBT list lengths are signed int32 values.
// Check before BoundingBox/BlockPos arithmetic or native allocation.
inline std::optional<std::array<int, 3>> captureSize(Bounds const& bounds) {
    std::array<long long, 3> const lengths{
        static_cast<long long>(bounds.max.x) - bounds.min.x + 1,
        static_cast<long long>(bounds.max.y) - bounds.min.y + 1,
        static_cast<long long>(bounds.max.z) - bounds.min.z + 1};
    std::array<int, 3> result;
    std::uint64_t volume = 1;
    for (int axis = 0; axis < 3; ++axis) {
        auto const length = lengths[axis];
        if (length <= 0 || length > std::numeric_limits<int>::max()
            || volume > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) / length)
            return {};
        result[axis] = static_cast<int>(length);
        volume *= length;
    }
    return result;
}
}
