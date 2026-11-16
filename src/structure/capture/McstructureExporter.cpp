#include "structure/capture/McstructureExporter.h"
#include "structure/capture/CaptureBounds.h"

#include "mc/deps/core/file/Path.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/nbt/IntArrayTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/levelgen/structure/StructureManager.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"
#include <fstream>
#include <unordered_map>

namespace lholo::structure::capture {

bool exportMcstructure(StructureTemplate const& structure, std::filesystem::path const& output) {
    return StructureManager::exportStructure(structure, Core::Path{output});
}

bool exportCustomMcstructure(CustomStructure const& draft, std::filesystem::path const& output) try {
    auto const volume = customStructureVolume(draft.size);
    if (!volume) return false;
    auto air = Block::tryGetFromRegistry(HashedString{"minecraft:air"});
    if (!air) return false;

    ListTag blocks;
    blocks.add(std::make_unique<CompoundTag>(air->mSerializationId.get()));
    std::unordered_map<std::string, int> paletteIndices;
    for (auto const& layer : draft.layers) {
        auto const& id = layer.block;
        if (paletteIndices.try_emplace(id, static_cast<int>(blocks.size())).second) {
            auto block = Block::tryGetFromRegistry(HashedString{id});
            if (!block || block->getTypeName() != id) return false;
            blocks.add(std::make_unique<CompoundTag>(block->mSerializationId.get()));
        }
    }
    std::vector<int> indices(*volume, 0);
    for (auto const& layer : draft.layers)
        fillCustomLayers(indices, draft.size, layer, paletteIndices.at(layer.block));
    ListTag layers;
    layers.add(std::make_unique<IntArrayTag>(std::move(indices)));
    CompoundTag palette;
    palette["block_palette"] = std::move(blocks);
    palette["block_position_data"] = CompoundTag{};
    CompoundTag palettes;
    palettes["default"] = std::move(palette);
    CompoundTag structure;
    structure["block_indices"] = std::move(layers);
    structure["entities"] = ListTag{};
    structure["palette"] = std::move(palettes);

    ListTag dimensions, origin;
    for (int extent : draft.size) {
        dimensions.add(std::make_unique<IntTag>(extent));
        origin.add(std::make_unique<IntTag>(0));
    }
    CompoundTag root;
    root["format_version"] = 2;
    root["size"] = std::move(dimensions);
    root["structure_world_origin"] = std::move(origin);
    root["structure"] = std::move(structure);
    auto const bytes = root.toBinaryNbt();
    if (bytes.size() > kMaximumMcstructureFileSize) return false;
    std::ofstream file(output, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    return static_cast<bool>(file);
} catch (std::exception const&) {
    return false;
}

} // namespace lholo::structure::capture
