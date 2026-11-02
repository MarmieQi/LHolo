#include "projection/correction/CorrectionWorkQueues.h"
#include "projection/core/ProjectionRules.h"

#include <chrono>
#include <cstdio>
#include <limits>
#include <map>

using namespace lholo::projection::detail;

int runCorrectionBulkEventTests() {
    int checks{}, failures{};
    auto check = [&](bool condition) {
        ++checks;
        if (!condition) {
            ++failures;
            if (failures < 12) std::fprintf(stderr, "Correction bulk event check %d failed\n", checks);
        }
    };

    // Compare mixed enqueue/consume/transfer operations with an independent
    // coordinate map, including lower bits arriving after the cursor advanced.
    CorrectionEventQueue queue;
    std::map<CorrectionPosition, std::uint64_t> reference;
    std::uint32_t random = 701;
    auto next = [&] { random = random * 1664525U + 1013904223U; return random; };
    for (int operation = 0; operation < 30000; ++operation) {
        CorrectionPosition position{static_cast<int>(next() % 65) - 32, static_cast<int>(next() % 33) - 16,
                                    static_cast<int>(next() % 65) - 32};
        auto time = operation % 3 ? static_cast<std::uint64_t>(next()) : 0;
        queue.push(position, time);
        reference[position] = std::max(reference[position], time);
        if (operation % 7 == 0) {
            auto event = *queue.front();
            check(reference.contains(event.position));
            check(reference[event.position] == event.destroyedAt);
            reference.erase(event.position);
            queue.pop();
        }
        if (operation % 11 == 0 && !queue.empty()) {
            auto batch = queue.takeSection();
            queue.push(std::move(*batch));
        }
        check(queue.size() == reference.size());
    }
    while (auto event = queue.front()) {
        check(reference.contains(event->position));
        check(reference[event->position] == event->destroyedAt);
        reference.erase(event->position);
        queue.pop();
    }
    check(reference.empty());
    check(queue.size() == 0 && queue.sections() == 0);

    // Batch merge preserves the newest destruction time and coordinate union.
    queue.push({1, 2, 3}, 90);
    CorrectionEventQueue incoming;
    incoming.push({1, 2, 3}, 50);
    incoming.push({1, 2, 4}, 120);
    queue.push(*incoming.takeSection());
    check(queue.size() == 2 && queue.sections() == 1);
    check(queue.front()->destroyedAt == 90);
    queue.pop();
    check(queue.front()->position == CorrectionPosition{1, 2, 4} && queue.front()->destroyedAt == 120);
    queue.push({1, 2, 3}, 0); // A consumed coordinate may legitimately arrive again.
    check(queue.front()->position == CorrectionPosition{1, 2, 3} && queue.front()->destroyedAt == 0);
    queue.clear();

    // A producer repeatedly refilling the low bit cannot hold other tiles
    // behind it forever, even while the original high bit is still pending.
    queue.push({0, 0, 0}, 0);
    queue.push({15, 15, 15}, 0);
    queue.push({32, 0, 0}, 0);
    int serviced{};
    while (queue.front()->position != CorrectionPosition{32, 0, 0} && serviced < 5000) {
        auto event = *queue.front();
        queue.pop();
        queue.push(event.position, event.destroyedAt);
        ++serviced;
    }
    check(serviced == 4096);
    check(queue.front()->position == CorrectionPosition{32, 0, 0});
    queue.clear();

    CorrectionEventBatch dense;
    dense.section = {-1, -2, 1};
    dense.cells.box({0, 0, 0}, {16, 16, 16}, true);
    for (std::uint16_t cell = 0; cell < 4096; cell += 31) dense.destroyed.emplace(cell, cell + 100);
    for (int sample = 0; sample < 80; ++sample) {
        std::array<long long, 3> lo{}, hi{};
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = static_cast<long long>(dense.section[axis]) * 16 + static_cast<int>(next() % 32) - 8;
            hi[axis] = lo[axis] + next() % 24;
        }
        auto clipped = dense.clipped(lo, hi);
        auto source = dense;
        std::uint64_t const* retained = clipped.destroyed.empty() ? nullptr
            : &source.destroyed.at(clipped.destroyed.begin()->first);
        auto moved = std::move(source).clipped(lo, hi);
        check(moved.cells.words == clipped.cells.words && moved.destroyed == clipped.destroyed);
        if (retained) check(retained == &moved.destroyed.begin()->second);
        CorrectionEventQueue routed;
        routed.push(clipped);
        std::size_t count{};
        for (int x = 0; x < 16; ++x) for (int y = 0; y < 16; ++y) for (int z = 0; z < 16; ++z) {
            auto cell = correctionCellSlot(x, y, z);
            std::array<long long, 3> p{-16 + x, -32 + y, 16 + z};
            bool inside = p[0] >= lo[0] && p[0] < hi[0] && p[1] >= lo[1] && p[1] < hi[1]
                && p[2] >= lo[2] && p[2] < hi[2];
            check(clipped.cells.contains(cell) == inside);
            count += inside;
            check(clipped.destroyed.contains(cell) == (inside && dense.destroyed.contains(cell)));
        }
        check(clipped.size() == count);
        check(dense.intersects(lo, hi) == (count != 0));
        check(routed.intersects(lo, hi) == (count != 0));
    }

    // Integer bounds and the one-cell dependency halo work for all transforms,
    // misaligned negative anchors, partial tiles, and extreme world positions.
    lholo::structure::LoadedStructure loaded;
    loaded.sizeX = 35; loaded.sizeY = 19; loaded.sizeZ = 39;
    for (int mirror = 0; mirror < 3; ++mirror) for (int rotation = 0; rotation < 4; ++rotation)
        for (auto origin : {std::array<long long, 3>{-23, -47, 37},
                             std::array<long long, 3>{2147483600LL, -2147483648LL, -2147483648LL}}) {
            for (auto key : {SubChunkKey{0, 0, 0}, SubChunkKey{1, 0, 1}, SubChunkKey{2, 1, 2}}) {
                auto [sx, sy, sz] = key;
                auto bounds = projectionSectionCellWorldBounds(key, loaded, origin, mirror, rotation, 1);
                for (int x = 0; x < loaded.sizeX; ++x)
                    for (int y = 0; y < loaded.sizeY; ++y)
                        for (int z = 0; z < loaded.sizeZ; ++z) {
                            bool inside = x >= sx * 16 - 1 && x < sx * 16 + 17
                                && y >= sy * 16 - 1 && y < sy * 16 + 17 && z >= sz * 16 - 1 && z < sz * 16 + 17;
                            auto transformed = transformStructurePosition(BlockPos{x, y, z}, loaded, mirror, rotation);
                            std::array<long long, 3> p{origin[0] + transformed.x, origin[1] + transformed.y,
                                                       origin[2] + transformed.z};
                            check(bounds.intersects(p, p) == inside);
                        }
            }
        }
    // A neighbor's unfinished border blocks this mesh; unrelated positions in
    // the same world tile do not. Final consumption releases it immediately.
    loaded.sizeX = loaded.sizeY = loaded.sizeZ = 32;
    auto halo = projectionSectionCellWorldBounds({0, 0, 0}, loaded, {3, -7, 5}, 0, 0, 1);
    queue.push({20, -5, 6}, 0);
    check(!queue.intersects(halo.minimum, halo.maximum));
    queue.push({19, -5, 6}, 0);
    check(queue.intersects(halo.minimum, halo.maximum));
    while (queue.front()->position != CorrectionPosition{19, -5, 6}) queue.pop();
    queue.pop();
    check(!queue.intersects(halo.minimum, halo.maximum));
    queue.clear();
    for (auto value : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        queue.push({value, value, value}, 15);
        std::array<long long, 3> lo{value, value, value}, hi{lo[0] + 1, lo[1] + 1, lo[2] + 1};
        check(queue.intersects(lo, hi));
        auto batch = queue.takeSection()->clipped(lo, hi);
        queue.push(std::move(batch));
        check(queue.front()->position == CorrectionPosition{value, value, value});
        check(queue.front()->destroyedAt == 15);
        queue.clear();
    }

    // Half-million-cell placement: count transfer operations rather than
    // claiming a hardware-dependent FPS gain from a microbenchmark.
    using Clock = std::chrono::steady_clock;
    auto started = Clock::now();
    for (int x = -64; x < 64; ++x) for (int y = -32; y < 32; ++y) for (int z = -32; z < 32; ++z) {
        queue.push({x, y, z}, 0);
        queue.push({x, y, z}, 0);
    }
    check(queue.size() == 524288 && queue.sections() == 128);
    std::size_t transfers{}, cells{};
    while (auto batch = queue.takeSection()) { ++transfers; cells += batch->size(); }
    check(transfers == 128 && cells == 524288);
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
    std::printf("Bulk event operations: 524288 cells (duplicates merged), %zu tile transfers, %lld us enqueue+transfer\n",
                transfers, static_cast<long long>(micros));

    // A dense tile requires 128 comparison quanta, but exposes only one ready
    // mesh revision after its backlog is consumed, with no arbitrary debounce.
    dense = {};
    dense.cells.box({0, 0, 0}, {16, 16, 16}, true);
    queue.push(dense);
    int ready{};
    for (int quantum = 0; quantum < 128; ++quantum) {
        for (int cell = 0; cell < 32; ++cell) queue.pop();
        if (!queue.intersects({0, 0, 0}, {16, 16, 16})) ++ready;
    }
    check(ready == 1);
    std::printf("CorrectionBulkEventTests: %d checks, %d failures\n", checks, failures);
    return failures;
}
