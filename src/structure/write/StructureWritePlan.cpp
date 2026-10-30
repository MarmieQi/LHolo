#include "structure/write/StructureWritePlan.h"
#include "structure/StructureLoader.h"
#include "mc/world/level/block/Block.h"
#include <unordered_map>

namespace lholo::structure::write {
StructureWritePlan makeWritePlan(LoadedStructure const& source, WriteTarget target) {
    StructureWritePlan plan;
    plan.target = target;
    plan.size = {source.sizeX, source.sizeY, source.sizeZ};
    plan.sourceWorldOrigin = source.sourceWorldOrigin;
    plan.omittedCells = source.omittedCells;
    for (auto const& box : source.regions)
        plan.regions.push_back({{box.x, box.y, box.z}, {box.sizeX, box.sizeY, box.sizeZ}});
    std::unordered_map<Block const*, int> palette;
    auto index = [&](Block const* block) {
        if (!block) return -1;
        auto [it, added] = palette.try_emplace(block, static_cast<int>(plan.palette.size()));
        if (added) plan.palette.push_back(block->mSerializationId->toBinaryNbt());
        return it->second;
    };
    plan.cells.reserve(source.renderBlocks.size());
    for (auto const& cell : source.renderBlocks)
        plan.cells.push_back({{cell.x, cell.y, cell.z}, index(cell.block), index(cell.liquid),
            cell.blockEntityNbt ? cell.blockEntityNbt->toBinaryNbt() : std::string{}});
    return plan;
}
}
