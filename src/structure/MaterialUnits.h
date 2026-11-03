#pragma once

#include "block/BlockPlacementRules.h"
#include "structure/StructureLoader.h"

#include <cstddef>
#include <map>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace lholo::structure::detail {

class MaterialUnitOwners {
    std::unordered_map<std::size_t, std::size_t> mGroups;
public:
    std::optional<std::size_t> representative(std::size_t index) const {
        auto found = mGroups.find(index);
        return found == mGroups.end() ? std::nullopt : std::optional{found->second};
    }
    bool empty() const { return mGroups.empty(); }
    void group(std::size_t index, std::size_t representative) { mGroups[index] = representative; }
};

// Every cell maps to a representative source for one placement item. Resolve
// native data once per palette state before handing this snapshot to a worker.
template <class Blocks, class ResolvePart>
MaterialUnitOwners buildMaterialUnits(
    Blocks const& blocks, ResolvePart resolvePart
) {
    using Key = std::tuple<long long, long long, long long, BlockType const*, int, int>;
    std::map<Key, std::size_t> units;
    std::unordered_map<Block const*, block::PlacementPart> palette;
    MaterialUnitOwners owners;
    std::vector<std::size_t> representatives;
    std::vector<std::pair<std::size_t, std::size_t>> members;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        auto const& entry = blocks[i];
        auto const part = [&] {
            if (!entry.block) return block::PlacementPart{};
            auto [found, inserted] = palette.try_emplace(entry.block);
            if (inserted) found->second = resolvePart(entry.block);
            return found->second;
        }();
        if (!part.type) {
            continue;
        }
        Key const key{static_cast<long long>(entry.x) + part.toStart[0],
            static_cast<long long>(entry.y) + part.toStart[1],
            static_cast<long long>(entry.z) + part.toStart[2],
            part.type, entry.sourceIndex, part.direction};
        auto const [found, inserted] = units.try_emplace(key, representatives.size());
        if (inserted) representatives.push_back(i);
        // Prefer the start's NBT (e.g. bed color), even if it is hidden or correct.
        // A cropped structure without its start keeps its available part.
        else if (part.index == 0) representatives[found->second] = i;
        members.emplace_back(i, found->second);
    }
    std::vector<std::size_t> sizes(representatives.size());
    for (auto const& [index, unit] : members) { (void)index; ++sizes[unit]; }
    for (auto const& [index, unit] : members)
        if (sizes[unit] > 1) owners.group(index, representatives[unit]);
    return owners;
}

} // namespace lholo::structure::detail
