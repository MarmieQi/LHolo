#pragma once
#include <array>
namespace lholo::structure {
// Mirror names refer to the coordinate flipped. Rotation is clockwise quarter turns.
std::array<int, 3> transformCell(int x, int y, int z, int sizeX, int sizeZ, int mirror, int rotation);
std::array<int, 3> inverseTransformCell(int x, int y, int z, int sizeX, int sizeZ, int mirror, int rotation);
} // namespace lholo::structure
