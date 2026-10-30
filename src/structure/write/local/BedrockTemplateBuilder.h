#pragma once
#include "structure/write/StructureWriteTypes.h"
#include <memory>
#include <limits>
#include <stdexcept>
class Level;
class StructureTemplate;
namespace lholo::structure::write {
struct TemplateBlockIndices { std::vector<int> primary, secondary; };
// Format-1 NBT lists encode their element count as a signed 32-bit integer.
inline std::size_t nativeTemplateVolume(std::array<int, 3> const& size) {
    std::size_t volume = 1;
    for (int extent : size) {
        if (extent <= 0 || volume > static_cast<std::size_t>(std::numeric_limits<int>::max()) / extent)
            throw std::length_error("Structure exceeds the native template index capacity");
        volume *= extent;
    }
    return volume;
}
// Native palette slot zero is air; absent source cells remain -1.
inline TemplateBlockIndices makeTemplateIndices(StructureWritePlan const& plan) {
    auto const sy = plan.size[1], sz = plan.size[2];
    auto const volume = nativeTemplateVolume(plan.size);
    TemplateBlockIndices result{std::vector<int>(volume, -1), std::vector<int>(volume, -1)};
    auto index = [=](int x, int y, int z) { return (static_cast<std::size_t>(x) * sy + y) * sz + z; };
    if (!plan.skipAir) {
        for (auto const& region : plan.regions)
            for (int x = region.minimum[0]; x < region.minimum[0] + region.size[0]; ++x)
                for (int y = region.minimum[1]; y < region.minimum[1] + region.size[1]; ++y)
                    for (int z = region.minimum[2]; z < region.minimum[2] + region.size[2]; ++z)
                        result.primary[index(x, y, z)] = 0;
    }
    for (auto omitted : plan.omittedCells) result.primary[omitted] = -1;
    for (auto const& cell : plan.cells) {
        auto const id = index(cell.position[0], cell.position[1], cell.position[2]);
        result.primary[id] = cell.block >= 0 ? cell.block + 1 : cell.liquid + 1;
        if (cell.block >= 0 && cell.liquid >= 0) result.secondary[id] = cell.liquid + 1;
    }
    return result;
}
std::unique_ptr<StructureTemplate> buildBedrockTemplate(StructureWritePlan const&, Level&);
}
