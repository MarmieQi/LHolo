#include "structure/write/command/SetBlockCommandBuilder.h"
#include "structure/write/StructureWriteIndices.h"
#include "structure/StructureTransform.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/states/VanillaBlockStateTransformUtils.h"
#include "mc/util/Mirror.h"
#include "mc/util/Rotation.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/dimension/VanillaDimensions.h"
#include "nlohmann/json.hpp"
#include <map>
#include <algorithm>
#include <stdexcept>

namespace lholo::structure::write::command {
Plan makeCommandPlan(StructureWritePlan const& source) {
    Plan plan;
    plan.dimension = source.target.dimension;
    auto const dimension = DimensionType{source.target.dimension};
    if (dimension == VanillaDimensions::Overworld()) plan.dimensionName = "overworld";
    else if (dimension == VanillaDimensions::Nether()) plan.dimensionName = "nether";
    else if (dimension == VanillaDimensions::TheEnd()) plan.dimensionName = "the_end";
    else throw std::runtime_error("Unsupported command write dimension");
    plan.palette.push_back("minecraft:air"); plan.expected.push_back(0);
    for (auto const& bytes : source.palette) {
        auto tag = CompoundTag::fromBinaryNbt(bytes);
        auto block = tag ? Block::tryGetFromRegistry(*tag) : optional_ref<Block const>{};
        if (!block) throw std::invalid_argument("writeWorld.commandUnsupported");
        auto const* transformed = VanillaBlockStateTransformUtils::transformBlock(*block,
            static_cast<Rotation>(source.target.rotation), source.target.mirror == 1 ? Mirror::Z
                : source.target.mirror == 2 ? Mirror::X : Mirror::None);
        if (!transformed) throw std::invalid_argument("writeWorld.commandUnsupported");
        auto const& data = transformed->mSerializationId.get();
        std::string command = transformed->getTypeName();
        if (data.contains("states")) {
            std::string states;
            for (auto const& [name, value] : data["states"].get<CompoundTag>()) {
                if (!states.empty()) states += ',';
                states += nlohmann::json(name).dump() + '=';
                if (value.hold<ByteTag>()) {
                    auto v = value.get<ByteTag>().data;
                    if (v != 0 && v != 1) throw std::invalid_argument("writeWorld.commandUnsupported");
                    states += v ? "true" : "false";
                } else if (value.hold<IntTag>()) states += std::to_string(value.get<IntTag>().data);
                else if (value.hold<StringTag>()) states += nlohmann::json(std::string(value.get<StringTag>())).dump();
                else throw std::invalid_argument("writeWorld.commandUnsupported");
            }
            if (!states.empty()) command += " [" + states + ']';
        }
        plan.palette.push_back(std::move(command));
        plan.expected.push_back(transformed->mSerializationIdHash);
    }
    auto grid = makeWriteIndices(source, false);
    std::map<std::array<int, 2>, std::vector<Target>> chunks;
    auto const sy = source.size[1], sz = source.size[2];
    for (std::size_t id = 0; id < grid.primary.size(); ++id) {
        if (grid.primary[id] < 0) continue;
        auto p = structure::transformCell(static_cast<int>(id / sz / sy), static_cast<int>((id / sz) % sy),
            static_cast<int>(id % sz), source.size[0], source.size[2], source.target.mirror, source.target.rotation);
        for (int axis = 0; axis < 3; ++axis) p[axis] += source.target.position[axis];
        chunks[{chunkCoordinate(p[0]), chunkCoordinate(p[2])}].push_back({p, grid.primary[id]});
    }
    // Supports and lower halves are placed before blocks above them within a chunk.
    for (auto& [position, targets] : chunks) {
        std::stable_sort(targets.begin(), targets.end(), [](Target const& a, Target const& b) {
            return a.position[1] < b.position[1];
        });
        plan.chunks.push_back({position, std::move(targets)});
    }
    return plan;
}
}
