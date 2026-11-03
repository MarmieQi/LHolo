#include "projection/core/ProjectionRules.h"
#include "projection/correction/CorrectionScanProgress.h"
#include "projection/correction/CorrectionStateStore.h"
#include "structure/StructureLoader.h"

#include <array>
#include <algorithm>
#include <tuple>
#include <cstdio>
#include <future>
#include <optional>
#include <vector>

using namespace lholo::projection::detail;

template <class Entries>
std::optional<std::size_t> findCorrectionCellIndex(Entries const& entries, int x, int y, int z) {
    auto const position = std::tuple{x, y, z};
    auto const found =
        std::lower_bound(entries.begin(), entries.end(), position,
                         [](auto const& entry, auto const& key) { return std::tie(entry.x, entry.y, entry.z) < key; });
    if (found == entries.end() || std::tie(found->x, found->y, found->z) != position)
        return std::nullopt;
    return static_cast<std::size_t>(found - entries.begin());
}


int runCorrectionStateStoreTests() {
    int failures{};
    int checks{};
    auto check = [&](bool condition) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "Correction state test failed\n");
        }
    };
    constexpr std::size_t sectionCount = 256;
    constexpr std::size_t cellsPerSection = 4096;
    std::vector<std::vector<std::size_t>> sections(sectionCount);
    // Interleaving global indices exercises the real index-to-section contract,
    // not an accidental assumption that each section is a contiguous slice.
    for (std::size_t section = 0; section < sectionCount; ++section)
        for (std::size_t slot = 0; slot < cellsPerSection; ++slot)
            sections[section].push_back(slot * sectionCount + section);
    CorrectionStateStore store;
    store.initialize(sections, sectionCount * cellsPerSection);
    auto const centerCell = sections[7][123];
    auto const neighborCell = sections[1][4095];
    auto const unrelatedCell = sections[200][0];
    std::array<std::size_t, 9> neighborhood{7, 1, 3, 9, 2, 4, 5, 7, sectionCount};
    auto old = store.capture(neighborhood);
    constexpr std::size_t pageBytes = cellsPerSection + cellsPerSection / 8;
    check(old.retainedBytes() == 7 * pageBytes);
    check(store.section(centerCell) == 7);
    check(store.get(centerCell) == CorrectionState::Missing);
    check(!store.set(centerCell, CorrectionState::Missing));
    check(store.copiedBytes() == 0);
    // A write outside the retained neighborhood must not copy any page.
    store.set(unrelatedCell, CorrectionState::Correct);
    check(store.copiedBytes() == 0);
    store.set(centerCell, CorrectionState::WrongType);
    check(store.copiedBytes() == pageBytes);
    check(old.rawState(centerCell) == CorrectionState::Missing);
    store.set(centerCell, CorrectionState::WrongState);
    store.setVisible(centerCell, false);
    check(store.copiedBytes() == pageBytes);
    check(store.get(centerCell) == CorrectionState::WrongState);
    check(!store.visible(centerCell));
    auto hidden = store.capture(neighborhood);
    check(hidden.rawState(centerCell) == CorrectionState::WrongState);
    store.setVisible(centerCell, true);
    check(store.copiedBytes() == 2 * pageBytes);
    check(store.get(centerCell) == CorrectionState::WrongState);
    check(hidden.rawState(centerCell) == CorrectionState::WrongState);
    store.set(neighborCell, CorrectionState::Correct);
    check(store.copiedBytes() == 3 * pageBytes);
    check(old.rawState(neighborCell) == CorrectionState::Missing);
    auto changed = store.capture(neighborhood);
    check(changed.rawState(centerCell) == CorrectionState::WrongState);
    check(changed.rawState(neighborCell) == CorrectionState::Correct);
    // Transform reset and replacement never mutate an already published view.
    store.fill(CorrectionState::Missing);
    check(changed.rawState(centerCell) == CorrectionState::WrongState);
    check(changed.rawState(neighborCell) == CorrectionState::Correct);
    store.initialize({}, 0);
    check(changed.rawState(centerCell) == CorrectionState::WrongState);

    // Visibility never overwrites world truth; worker snapshots retain the
    // raw result even when active ownership changes.
    std::vector<std::vector<std::size_t>> small{{0}, {}, {1, 2, 3}};
    store.initialize(small, 4);
    std::array<std::size_t, 3> smallSections{0, 1, 2};
    for (auto state : {CorrectionState::Unknown, CorrectionState::Missing, CorrectionState::Correct,
                       CorrectionState::WrongType, CorrectionState::WrongState}) {
        for (bool visible : {false, true}) {
            store.set(3, state);
            store.setVisible(3, visible);
            auto snapshot = store.capture(smallSections);
            check(store.get(3) == state);
            check(store.visible(3) == visible);
            check(snapshot.rawState(3) == store.get(3));
        }
    }
    store.setVisible(3, true);
    store.set(3, CorrectionState::WrongType);
    auto beforeHide = store.capture(smallSections);
    store.setVisible(3, false);
    store.set(3,
              CorrectionState::Correct); // World event while the layer is hidden.
    store.setVisible(3, true);
    check(store.get(3) == CorrectionState::Correct);
    check(beforeHide.rawState(3) == CorrectionState::WrongType);

    // Concurrent worker reads observe one immutable version while the owner
    // changes truth, visibility and eventually replaces the entire layout.
    auto stable = store.capture(smallSections);
    std::promise<void> begin;
    auto started = begin.get_future();
    auto reader = std::async(std::launch::async, [snapshot = std::move(stable), signal = std::move(started)]() mutable {
        signal.wait();
        for (int iteration = 0; iteration < 20000; ++iteration)
            if (snapshot.rawState(3) != CorrectionState::Correct)
                return false;
        return true;
    });
    begin.set_value();
    for (int iteration = 0; iteration < 1000; ++iteration) {
        store.set(3, (iteration & 1) ? CorrectionState::Missing : CorrectionState::WrongState);
        store.setVisible(3, (iteration & 1) != 0);
    }
    store.initialize({}, 0);
    check(reader.get());

    CorrectionScanProgress coverage;
    coverage.reset(130);
    check(!coverage.complete());
    coverage.record(129);
    for (std::size_t i = 0; i < 129; ++i)
        coverage.record(129);
    check(!coverage.complete());
    for (std::size_t i = 128; i > 0; --i)
        coverage.record(i);
    check(!coverage.complete());
    coverage.record(0);
    check(coverage.complete());
    coverage.reset(1);
    check(!coverage.complete());
    coverage.record(0);
    check(coverage.complete());
    coverage.reset(0);
    check(coverage.complete());

    struct Cell {
        int x, y, z;
    };
    std::vector<Cell> entries{{-16, 0, 0}, {0, 0, 0}, {0, 1, 0}, {16, 0, 0}};
    // Full local lookup deliberately has no display-layer filter.
    check(findCorrectionCellIndex(entries, 0, 1, 0) == std::optional<std::size_t>{2});
    check(findCorrectionCellIndex(entries, -16, 0, 0) == std::optional<std::size_t>{0});
    check(findCorrectionCellIndex(entries, 16, 0, 0) == std::optional<std::size_t>{3});
    check(!findCorrectionCellIndex(entries, 0, 2, 0));
    check(!findCorrectionCellIndex(entries, -17, 0, 0));
    check(!findCorrectionCellIndex(entries, 17, 0, 0));
    // Exercise the production forward/inverse transform around negative,
    // non-section-aligned world origins before using the full local lookup.
    lholo::structure::LoadedStructure loaded;
    loaded.sizeX = 35;
    loaded.sizeY = 18;
    loaded.sizeZ = 19;
    entries = {{0, 0, 0}, {0, 1, 0}, {15, 15, 15}, {16, 16, 16}, {34, 17, 18}};
    for (int mirror = 0; mirror < 3; ++mirror) {
        for (int rotation = 0; rotation < 4; ++rotation) {
            for (std::size_t index = 0; index < entries.size(); ++index) {
                auto const& cell = entries[index];
                auto transformed =
                    transformStructurePosition(BlockPos{cell.x, cell.y, cell.z}, loaded, mirror, rotation);
                BlockPos world{transformed.x - 513, transformed.y - 31, transformed.z + 33};
                auto local = inverseTransformStructurePosition(BlockPos{world.x + 513, world.y + 31, world.z - 33},
                                                               loaded, mirror, rotation);
                check(findCorrectionCellIndex(entries, local.x, local.y, local.z) == std::optional<std::size_t>{index});
            }
        }
    }
    entries.clear();
    check(!findCorrectionCellIndex(entries, 0, 0, 0));
    std::printf("CorrectionStateStoreTests: %d checks, %d failures\n", checks, failures);
    return failures;
}
