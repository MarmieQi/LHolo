#pragma once
#include "structure/write/StructureWriteTypes.h"
#include <limits>
#include <utility>
namespace lholo::structure { struct LoadedStructure; }
namespace lholo::structure::write {
StructureWritePlan makeWritePlan(LoadedStructure const&, WriteTarget);
// Inclusive world bounds, matching the existing file transform convention.
inline bool writeBounds(WriteTarget const& target, std::array<int, 3> size, std::array<int, 3>& maximum) {
    if (target.rotation & 1) std::swap(size[0], size[2]);
    for (int axis = 0; axis < 3; ++axis) {
        auto const end = static_cast<long long>(target.position[axis]) + size[axis] - 1;
        if (size[axis] <= 0 || end > std::numeric_limits<int>::max()) return false;
        maximum[axis] = static_cast<int>(end);
    }
    return true;
}
}
