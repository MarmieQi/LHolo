#include "structure/MaterialCounts.h"
#include "structure/MaterialUnits.h"
#include <cstdio>
#include <memory>

int runMaterialCountsTests() {
    int failures{};
    auto check = [&](bool value, char const* message) {
        if (!value) {
            ++failures;
            std::fprintf(stderr, "MaterialCountsTests: %s\n", message);
        }
    };
    // Opaque pointers only: this worker path must not access Minecraft objects.
    int blockToken{};
    auto const* block = reinterpret_cast<Block const*>(&blockToken);
    auto source = [] {
        auto token = std::make_shared<int>();
        return std::shared_ptr<CompoundTag const>{token, reinterpret_cast<CompoundTag const*>(token.get())};
    };
    auto ordinary = source(), ominous = source();
    lholo::structure::detail::MaterialCounts counts;
    counts.add(block, ordinary);
    counts.add(block, ominous);
    counts.add(block, ominous);
    auto const& entries = counts.entries();
    check(entries.size() == 2, "same block retains different banner NBT");
    if (entries.size() == 2)
        check(entries[0].nbt == ordinary && entries[0].count == 1
            && entries[1].nbt == ominous && entries[1].count == 2,
            "each variant keeps its own source and count");
    counts.add(nullptr, ordinary);
    check(entries.size() == 2, "null block contributes nothing");

    using namespace lholo::structure;
    using namespace lholo::structure::detail;
    using lholo::block::PlacementPart;
    int tokens[5]{}, typeToken{};
    auto ptr = [&](int index) { return reinterpret_cast<Block const*>(&tokens[index]); };
    auto const* type = reinterpret_cast<BlockType const*>(&typeToken);
    auto parts = [&](Block const* value) -> PlacementPart {
        if (value == ptr(0)) return {type, {0, 0, 0}, 0};
        if (value == ptr(1)) return {type, {0, -1, 0}, 1};
        if (value == ptr(2)) return {type, {0, -2, 0}, 2};
        return {};
    };
    // Upper first also verifies that the start supplies the item NBT.
    std::vector<LoadedStructure::RenderBlock> blocks{
        {0, 1, 0, ptr(1), ptr(4), ominous},
        {0, 0, 0, ptr(0), ptr(4), ordinary},
        {2, 0, 0, ptr(0)}, {2, 1, 0, ptr(1)},
        {4, 1, 0, ptr(1)}, // cropped upper half
        {6, 0, 0, ptr(3)}, {7, 0, 0, ptr(3)} // independent cells, e.g. double chest
    };
    auto owners = buildMaterialUnits(blocks, parts);
    auto all = [](std::size_t) { return true; };
    auto total = [](MaterialCounts const& value) {
        std::uint64_t sum{};
        for (auto const& entry : value.entries()) sum += entry.count;
        return sum;
    };
    auto raw = collectRawMaterials(blocks, owners, all);
    check(total(raw.body) == 5, "two doors, cropped half and two independent blocks need five items");
    check(total(raw.liquid) == 2, "multi-block grouping preserves both fluid cells");
    check(raw.body.entries()[0].nbt == ordinary, "start NBT wins regardless of traversal order");
    auto upperOnly = collectRawMaterials(blocks, owners, [](std::size_t i) { return i == 0; });
    check(total(upperOnly.body) == 1 && upperOnly.body.entries()[0].nbt == ordinary,
        "visible or missing upper half still needs one item with the start's identity");
    check(total(collectRawMaterials(blocks, owners, [](std::size_t) { return false; }).body) == 0,
        "fully correct or hidden units need nothing");
    blocks = {{0, 0, 0, ptr(0)}, {0, 1, 0, ptr(1)}, {0, 2, 0, ptr(2)}};
    check(total(collectRawMaterials(blocks, buildMaterialUnits(blocks, parts), all).body) == 1,
        "three-part native placement counts once");
    blocks[1].sourceIndex = 1;
    check(total(collectRawMaterials(blocks, buildMaterialUnits(blocks, parts), all).body) == 2,
        "task sources never merge placement units");
    for (auto const step : {std::array{1, 0, 0}, std::array{-1, 0, 0},
                            std::array{0, 0, 1}, std::array{0, 0, -1}}) {
        blocks = {{3, 0, 3, ptr(0)}, {3 + step[0], 0, 3 + step[2], ptr(1)}};
        auto bedParts = [&](Block const* value) -> PlacementPart {
            return {type, value == ptr(0) ? std::array{0, 0, 0} : std::array{-step[0], 0, -step[2]},
                value == ptr(0) ? 0 : 1};
        };
        check(total(collectRawMaterials(blocks, buildMaterialUnits(blocks, bedParts), all).body) == 1,
            "bed halves count once in every horizontal direction");
    }
    std::printf("MaterialCountsTests: %d failures\n", failures);
    return failures;
}
