#pragma once

#include "structure/StructureLoader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

class Block;
class CompoundTag;

namespace lholo::structure::detail {

// Background counting must preserve every source's item data. Block pointers
// alone cannot distinguish beds, banner colors or banner patterns.
struct MaterialSourceCount {
    Block const* block{};
    std::shared_ptr<CompoundTag const> nbt;
    std::uint64_t count{};
};

class MaterialCounts {
    struct Key {
        Block const* block;
        CompoundTag const* nbt;
        bool operator==(Key const&) const = default;
    };
    struct Hash {
        std::size_t operator()(Key const& key) const {
            auto seed = std::hash<Block const*>{}(key.block);
            return seed ^ (std::hash<CompoundTag const*>{}(key.nbt) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
        }
    };
    std::unordered_map<Key, std::size_t, Hash> indices;
    std::vector<MaterialSourceCount> sources;

public:
    void reserve(std::size_t count) { indices.reserve(count); }

    void add(Block const* block, std::shared_ptr<CompoundTag const> nbt = {}) {
        if (!block) return;
        auto const [it, inserted] = indices.try_emplace(Key{block, nbt.get()}, sources.size());
        if (inserted) sources.push_back({block, std::move(nbt), 0});
        auto& count = sources[it->second].count;
        if (count != std::numeric_limits<std::uint64_t>::max()) ++count;
    }

    auto const& entries() const { return sources; }
};

struct RawMaterialCounts {
    MaterialCounts body;
    MaterialCounts liquid;
};

template <class IsIncluded>
RawMaterialCounts collectRawMaterials(
    std::span<LoadedStructure::RenderBlock const> blocks,
    std::span<std::size_t const> owners,
    IsIncluded included
) {
    RawMaterialCounts counts;
    counts.body.reserve(std::min<std::size_t>(blocks.size(), 4096));
    counts.liquid.reserve(std::min<std::size_t>(blocks.size(), 64));
    std::vector<unsigned char> counted(blocks.size());
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (!included(i)) continue;
        auto const owner = owners[i];
        if (!counted[owner]) {
            counted[owner] = 1;
            auto const& source = blocks[owner];
            counts.body.add(source.block, source.blockEntityNbt);
        }
        // Preserve liquid state counts per cell, independent of the body unit.
        // The game-thread resolver filters these through the native source test.
        counts.liquid.add(blocks[i].liquid);
    }
    return counts;
}

} // namespace lholo::structure::detail
