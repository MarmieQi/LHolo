#pragma once

#include "structure/StructureLoader.h"
#include "structure/MaterialUnits.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
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
    std::uint64_t missing{};
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

    void add(
        Block const* block,
        std::shared_ptr<CompoundTag const> nbt = {},
        bool addTotal = true,
        bool addMissing = false
    ) {
        if (!block || (!addTotal && !addMissing)) return;
        auto const [it, inserted] = indices.try_emplace(Key{block, nbt.get()}, sources.size());
        if (inserted) sources.push_back({block, std::move(nbt), 0, 0});
        auto& entry = sources[it->second];
        auto const maximum = std::numeric_limits<std::uint64_t>::max();
        if (addTotal && entry.count != maximum) ++entry.count;
        if (addMissing && entry.missing != maximum) ++entry.missing;
    }

    void addCount(
        Block const* block,
        std::shared_ptr<CompoundTag const> nbt,
        std::uint64_t totalDelta,
        std::uint64_t missingDelta = 0
    ) {
        if (!block || (totalDelta == 0 && missingDelta == 0)) return;
        auto const [it, inserted] = indices.try_emplace(Key{block, nbt.get()}, sources.size());
        if (inserted) sources.push_back({block, std::move(nbt), 0, 0});
        auto& entry = sources[it->second];
        auto const maximum = std::numeric_limits<std::uint64_t>::max();
        entry.count = totalDelta > maximum - entry.count ? maximum : entry.count + totalDelta;
        entry.missing = missingDelta > maximum - entry.missing ? maximum : entry.missing + missingDelta;
    }

    auto const& entries() const { return sources; }
};

struct RawMaterialCounts {
    MaterialCounts body;
    MaterialCounts liquid;
};

template <class Blocks, class IsIncluded, class IsMissing>
RawMaterialCounts collectRawMaterials(
    Blocks const& blocks,
    MaterialUnitOwners const& owners,
    IsIncluded included,
    IsMissing isMissing
) {
    RawMaterialCounts counts;
    counts.body.reserve(std::min<std::size_t>(blocks.size(), 4096));
    counts.liquid.reserve(std::min<std::size_t>(blocks.size(), 64));
    std::unordered_set<std::size_t> countedTotal;
    std::unordered_set<std::size_t> countedMissing;
    bool const hasGroups = !owners.empty();
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (!included(i)) continue;
        bool const cellMissing = isMissing(i);
        auto const owner = hasGroups ? owners.representative(i) : std::nullopt;
        bool const addTotal = !owner || countedTotal.insert(*owner).second;
        bool const addMissing = cellMissing && (!owner || countedMissing.insert(*owner).second);
        if (addTotal || addMissing) {
            auto const& source = blocks[owner.value_or(i)];
            counts.body.add(source.block, source.blockEntityNbt, addTotal, addMissing);
        }
        // Preserve liquid state counts per cell, independent of the body unit.
        // The game-thread resolver filters these through the native source test.
        counts.liquid.add(blocks[i].liquid, {}, true, cellMissing);
    }
    return counts;
}

template <class Blocks, class IsIncluded>
RawMaterialCounts collectRawMaterials(
    Blocks const& blocks,
    MaterialUnitOwners const& owners,
    IsIncluded included
) {
    return collectRawMaterials(blocks, owners, included, [](std::size_t) { return false; });
}

} // namespace lholo::structure::detail
