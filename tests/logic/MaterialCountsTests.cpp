#include "structure/MaterialCounts.h"
#include "structure/MaterialDeltaState.h"
#include "structure/MaterialUnits.h"
#include <cstdio>
#include <map>
#include <memory>
#include <random>

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
    auto totalMissing = [](MaterialCounts const& value) {
        std::uint64_t sum{};
        for (auto const& entry : value.entries()) sum += entry.missing;
        return sum;
    };
    // Door 0 (indices 0,1): only upper half missing -> 1 missing; Door 1 (indices 2,3): both placed -> 0 missing;
    // cropped half (index 4): missing -> 1; chests (5,6): index 5 missing, index 6 placed -> 1.
    auto withMissing = collectRawMaterials(
        blocks, owners, all,
        [](std::size_t i) { return i == 0 || i == 4 || i == 5; }
    );
    check(total(withMissing.body) == 5 && totalMissing(withMissing.body) == 3,
        "combined pass counts total and missing independently across multi-block units");
    check(total(withMissing.liquid) == 2 && totalMissing(withMissing.liquid) == 1,
        "combined pass counts liquid missing cells independently");
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

    // Randomized oracle test: incremental MaterialDeltaState must match full
    // collectRawMaterials after every single flip across multi-block units,
    // waterlogged cells, and filtered-out layers.
    {
        std::vector<LoadedStructure::RenderBlock> oracleBlocks{
            {0, 1, 0, ptr(1), ptr(4), ominous},
            {0, 0, 0, ptr(0), ptr(4), ordinary},
            {2, 0, 0, ptr(0)}, {2, 1, 0, ptr(1)},
            {4, 1, 0, ptr(1)},
            {6, 0, 0, ptr(3), ptr(4)}, {7, 0, 0, ptr(3)},
            {8, 0, 0, nullptr, ptr(4)},
            {9, 2, 0, ptr(3)}, // excluded by filter
        };
        auto const oracleOwners = buildMaterialUnits(oracleBlocks, parts);
        auto const filter = [](std::size_t i) { return i != 8; };
        std::vector<unsigned char> correct(oracleBlocks.size(), 0);
        correct[2] = 1;
        correct[6] = 1;

        MaterialDeltaState delta;
        delta.build(oracleBlocks, oracleOwners, correct, filter);

        auto toMap = [](MaterialCounts const& counts) {
            std::map<std::pair<Block const*, CompoundTag const*>, std::uint64_t> out;
            for (auto const& e : counts.entries()) {
                if (e.count > 0) out[{e.block, e.nbt.get()}] += e.count;
            }
            return out;
        };
        auto toFullMap = [](MaterialCounts const& counts) {
            std::map<std::pair<Block const*, CompoundTag const*>, std::pair<std::uint64_t, std::uint64_t>> out;
            for (auto const& e : counts.entries()) {
                if (e.count > 0) {
                    auto& slot = out[{e.block, e.nbt.get()}];
                    slot.first += e.count;
                    slot.second += e.missing;
                }
            }
            return out;
        };
        auto assertMatchesOracle = [&](char const* label) {
            auto const expected = collectRawMaterials(oracleBlocks, oracleOwners, [&](std::size_t i) {
                return filter(i) && correct[i] == 0;
            });
            auto const actual = delta.exportCounts();
            check(toMap(actual.body) == toMap(expected.body) && toMap(actual.liquid) == toMap(expected.liquid), label);

            auto const expectedFull = collectRawMaterials(
                oracleBlocks, oracleOwners, filter,
                [&](std::size_t i) { return correct[i] == 0; }
            );
            auto const actualFull = delta.exportFullCounts();
            check(
                toFullMap(actualFull.body) == toFullMap(expectedFull.body)
                    && toFullMap(actualFull.liquid) == toFullMap(expectedFull.liquid),
                label
            );
        };
        assertMatchesOracle("delta baseline matches full scan");

        std::mt19937 rng{42};
        std::uniform_int_distribution<std::size_t> pickIndex(0, oracleBlocks.size() - 1);
        std::uniform_int_distribution<int> pickState(0, 1);
        for (int step = 0; step < 500; ++step) {
            auto const idx = pickIndex(rng);
            bool const next = pickState(rng) != 0;
            correct[idx] = next ? 1 : 0;
            delta.applyFlip(idx, next);
            assertMatchesOracle("incremental flip matches full scan");
        }
    }
    std::printf("MaterialCountsTests: %d failures\n", failures);
    return failures;
}
