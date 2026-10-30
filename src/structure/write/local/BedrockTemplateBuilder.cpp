#include "structure/write/local/BedrockTemplateBuilder.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"
#include <stdexcept>

namespace lholo::structure::write {
namespace {
CompoundTag decode(std::string const& bytes) {
    auto tag = CompoundTag::fromBinaryNbt(bytes);
    if (!tag) throw std::runtime_error("Invalid write-plan NBT");
    return std::move(*tag);
}
ListTag intList(std::vector<int> const& values) {
    ListTag result;
    result.reserve(values.size());
    for (int value : values) result.add(std::make_unique<IntTag>(value));
    return result;
}
}
std::unique_ptr<StructureTemplate> buildBedrockTemplate(StructureWritePlan const& plan, Level& level) {
    auto const sx = plan.size[0], sy = plan.size[1], sz = plan.size[2];
    auto indices = makeTemplateIndices(plan);
    auto index = [=](int x, int y, int z) { return (static_cast<std::size_t>(x) * sy + y) * sz + z; };
    ListTag palette;
    auto air = Block::tryGetFromRegistry(HashedString{"minecraft:air"});
    palette.add(std::make_unique<CompoundTag>(air->mSerializationId.get()));
    for (auto const& block : plan.palette) palette.add(std::make_unique<CompoundTag>(decode(block)));
    CompoundTag positions;
    for (auto const& cell : plan.cells) {
        auto const id = index(cell.position[0], cell.position[1], cell.position[2]);
        if (!cell.blockEntityNbt.empty()) positions[std::to_string(id)] = CompoundTag{
            {"block_entity_data", decode(cell.blockEntityNbt)}};
    }
    ListTag layers;
    layers.add(std::make_unique<ListTag>(intList(indices.primary)));
    layers.add(std::make_unique<ListTag>(intList(indices.secondary)));
    CompoundTag root{
        {"format_version", 1},
        {"size", intList({sx, sy, sz})},
        {"structure_world_origin", intList({plan.sourceWorldOrigin[0], plan.sourceWorldOrigin[1], plan.sourceWorldOrigin[2]})},
        {"structure", CompoundTag{
            {"block_indices", std::move(layers)}, {"entities", ListTag{}},
            {"palette", CompoundTag{{"default", CompoundTag{
                {"block_palette", std::move(palette)}, {"block_position_data", std::move(positions)}}}}}}}
    };
    auto result = std::make_unique<StructureTemplate>("lholo:world_write", level.getUnknownBlockTypeRegistry());
    if (!result->load(root)) return {};
    return result;
}
}
