#include "structure/StructureTransform.h"
namespace lholo::structure {
std::array<int, 3> transformCell(int x, int y, int z, int sizeX, int sizeZ, int mirror, int rotation) {
    if (mirror == 1)
        x = sizeX - 1 - x;
    if (mirror == 2)
        z = sizeZ - 1 - z;
    switch (rotation) {
    case 1:
        return {sizeZ - 1 - z, y, x};
    case 2:
        return {sizeX - 1 - x, y, sizeZ - 1 - z};
    case 3:
        return {z, y, sizeX - 1 - x};
    default:
        return {x, y, z};
    }
}
std::array<int, 3> inverseTransformCell(int x, int y, int z, int sizeX, int sizeZ, int mirror, int rotation) {
    int localX = x, localZ = z;
    switch (rotation & 3) {
    case 1: localX = z; localZ = sizeZ - 1 - x; break;
    case 2: localX = sizeX - 1 - x; localZ = sizeZ - 1 - z; break;
    case 3: localX = sizeX - 1 - z; localZ = x; break;
    }
    if (mirror == 1) localX = sizeX - 1 - localX;
    if (mirror == 2) localZ = sizeZ - 1 - localZ;
    return {localX, y, localZ};
}
} // namespace lholo::structure
