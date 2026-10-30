#include "structure/write/local/BedrockTemplateBuilder.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/IntArrayTag.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"
#include <stdexcept>
#include <initializer_list>
#include <utility>

namespace lholo::structure::write {
namespace {
CompoundTag decode(std::string const& bytes) {
    auto tag = CompoundTag::fromBinaryNbt(bytes);
    if (!tag) throw std::runtime_error("Invalid write-plan NBT");
    return std::move(*tag);
}
// Only the fixed-size size/origin tags use List<Int>; block layers use IntArray.
ListTag intList(std::initializer_list<int> values) {
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
        if (!cell.blockEntityNbt.empty()) {
            CompoundTag position;
            position["block_entity_data"] = decode(cell.blockEntityNbt);
            positions[std::to_string(id)] = std::move(position);
        }
    }
    ListTag layers;
    layers.reserve(indices.secondary.empty() ? 1 : 2);
    layers.add(std::make_unique<IntArrayTag>(std::move(indices.primary)));
    if (!indices.secondary.empty()) layers.add(std::make_unique<IntArrayTag>(std::move(indices.secondary)));
    // Initializer-list map construction copies its const entries. Assemble
    // bottom-up with move assignment so dense arrays and NBT are not cloned.
    CompoundTag defaultPalette;
    defaultPalette["block_palette"] = std::move(palette);
    defaultPalette["block_position_data"] = std::move(positions);
    CompoundTag palettes;
    palettes["default"] = std::move(defaultPalette);
    CompoundTag structure;
    structure["block_indices"] = std::move(layers);
    structure["entities"] = ListTag{};
    structure["palette"] = std::move(palettes);
    CompoundTag root;
    root["format_version"] = 2;
    root["size"] = intList({sx, sy, sz});
    root["structure_world_origin"] = intList({plan.sourceWorldOrigin[0], plan.sourceWorldOrigin[1], plan.sourceWorldOrigin[2]});
    root["structure"] = std::move(structure);
    auto result = std::make_unique<StructureTemplate>("lholo:world_write", level.getUnknownBlockTypeRegistry());
    if (!result->load(root)) return {};
    return result;
}
}
