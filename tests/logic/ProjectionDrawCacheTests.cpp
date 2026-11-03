#include "projection/core/ProjectionDrawCache.h"
#include "projection/core/ProjectionVisibilityRules.h"

#include <chrono>
#include <cstdio>
#include <numeric>

namespace {
using namespace lholo::projection::detail;
int checks{}, failures{};
#define DRAW_CHECK(condition)                                                                                \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(condition)) {                                                                                  \
            ++failures;                                                                                      \
            std::fprintf(stderr, "Draw cache FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);             \
        }                                                                                                    \
    } while (false)

struct ReferenceDrawLists {
    std::array<std::vector<std::size_t>, kProjectionDrawSlotCount> lists;
    std::vector<ProjectionTerrainDraw> terrain;
};

// Rebuild from authoritative input every time. Unlike the cache this holds no
// version history, so comparisons catch stale entries and missing invalidation.
ReferenceDrawLists reference(std::vector<ProjectionDrawRecord> const& records,
                             std::vector<unsigned char> const& visible, ProjectionDrawPosition eye) {
    ReferenceDrawLists result;
    auto distance = [&](std::size_t index) {
        auto const& p = records[index].center;
        auto const dx = p.x - eye.x, dy = p.y - eye.y, dz = p.z - eye.z;
        auto const squared = dx * dx + dy * dy + dz * dz;
        return std::isfinite(squared) ? squared : std::numeric_limits<float>::max();
    };
    auto farther = [&](std::size_t a, std::size_t b) {
        auto const da = distance(a), db = distance(b);
        return da != db ? da > db : a < b;
    };
    for (std::size_t index = 0; index < records.size(); ++index) {
        if (!visible[index])
            continue;
        auto const mask = records[index].slots;
        for (std::size_t slot = 0; slot < kProjectionDrawSlotCount; ++slot) {
            if (!(mask & (1u << slot)))
                continue;
            if (slot < 4) {
                if (slot == static_cast<std::size_t>(ProjectionDrawSlot::Blend))
                    result.terrain.push_back({index, slot});
                else
                    result.lists[slot].push_back(index);
            } else
                result.lists[slot].push_back(index);
        }
    }
    std::sort(result.terrain.begin(), result.terrain.end(), [&](auto const& a, auto const& b) {
        return a.section == b.section ? a.bucket < b.bucket : farther(a.section, b.section);
    });
    auto& liquid = result.lists[static_cast<std::size_t>(ProjectionDrawSlot::Liquid)];
    std::sort(liquid.begin(), liquid.end(), farther);
    return result;
}

std::vector<ProjectionDrawRecord> makeRecords(std::size_t count) {
    std::vector<ProjectionDrawRecord> records(count);
    for (std::size_t i = 0; i < count; ++i) {
        records[i] = {i,
                      {static_cast<float>(i % 64 * 16), static_cast<float>(i / 64 * 16),
                       static_cast<float>(i * 37 % 13 * 16)},
                      static_cast<std::uint16_t>(i % 7 == 0 ? 0 : ((i * 173 + 91) & 1023))};
    }
    return records;
}

void compare(ProjectionDrawCache const& cache, ReferenceDrawLists const& expected) {
    DRAW_CHECK(cache.transparentTerrain == expected.terrain);
    for (std::size_t slot = 0; slot < kProjectionDrawSlotCount; ++slot) {
        DRAW_CHECK(cache.lists[slot] == expected.lists[slot]);
    }
}

void testInvalidationAndLifetime() {
    auto records = makeRecords(1024);
    std::vector<unsigned char> visible(records.size(), 1);
    ProjectionDrawCache cache;
    std::uint64_t activation = 1, meshRevision = 1, visibilityRevision = 1;
    std::size_t reads{};
    ProjectionDrawPosition eye{17, 5, -3};
    auto prepare = [&] {
        cache.prepare(
            activation, meshRevision, visibilityRevision, records.size(), eye,
            [&](std::size_t i) {
                ++reads;
                return records[i];
            },
            [&](std::size_t i) { return visible[i]; });
    };
    prepare();
    compare(cache, reference(records, visible, eye));
    DRAW_CHECK(reads == records.size());
    DRAW_CHECK(cache.lists[static_cast<std::size_t>(ProjectionDrawSlot::Blend)].capacity() == 0);
    auto const* terrainStorage = cache.transparentTerrain.data();
    std::array<std::size_t const*, kProjectionDrawSlotCount> listStorage{};
    for (std::size_t i = 0; i < listStorage.size(); ++i)
        listStorage[i] = cache.lists[i].data();

    // Fast turns change only visibility. Distances and scene records stay
    // cached and the maximum-residency reservation avoids turn-time allocation.
    for (int frame = 0; frame < 120; ++frame) {
        for (std::size_t i = 0; i < visible.size(); ++i)
            visible[i] = (i + frame * 13) % 7 < 3;
        ++visibilityRevision;
        prepare();
        compare(cache, reference(records, visible, eye));
        DRAW_CHECK(cache.transparentTerrain.data() == terrainStorage);
        for (std::size_t i = 0; i < listStorage.size(); ++i)
            DRAW_CHECK(cache.lists[i].data() == listStorage[i]);
    }
    DRAW_CHECK(cache.orderBuilds == 1);
    DRAW_CHECK(cache.recordBuilds == 1);
    DRAW_CHECK(reads == records.size());
    auto const builds = cache.listBuilds;
    prepare(); // alpha pass must reuse the opaque pass's completed lists
    DRAW_CHECK(cache.listBuilds == builds);

    eye.x += 100;
    prepare();
    compare(cache, reference(records, visible, eye));
    DRAW_CHECK(cache.orderBuilds == 2 && cache.recordBuilds == 1);

    // Continuous movement and a narrow view use exact visible-only sorting.
    // The second render pass must neither allocate nor sort a second time.
    for (int frame = 0; frame < 40; ++frame) {
        eye.x += 0.25f;
        eye.z -= 0.125f;
        ++visibilityRevision;
        for (std::size_t i = 0; i < visible.size(); ++i)
            visible[i] = (i + frame) % 31 == 0;
        prepare();
        compare(cache, reference(records, visible, eye));
        auto const orders = cache.orderBuilds;
        prepare();
        DRAW_CHECK(cache.orderBuilds == orders);
    }
    // Stop moving, then sweep through new directions: promote once to a
    // reusable global order, without retaining an old view's missing entries.
    auto const beforeTurns = cache.orderBuilds;
    for (int frame = 0; frame < 20; ++frame) {
        ++visibilityRevision;
        for (std::size_t i = 0; i < visible.size(); ++i)
            visible[i] = (i + frame * 7) % 5 < 2;
        prepare();
        compare(cache, reference(records, visible, eye));
    }
    DRAW_CHECK(cache.orderBuilds == beforeTurns + 1);

    // Replacement, destruction and center changes must be visible even when
    // the camera and section count do not change.
    records[1].slots = 0;
    records[2].slots = 1023;
    records[2].center = {50000, -200, 4};
    ++meshRevision;
    prepare();
    compare(cache, reference(records, visible, eye));
    DRAW_CHECK(cache.recordBuilds == 2);

    records.push_back({records.size(), {-100, 2, 3}, 1023});
    visible.push_back(1);
    prepare();
    compare(cache, reference(records, visible, eye));
    DRAW_CHECK(cache.recordBuilds == 3); // new correction section

    // New world/dimension/activation: same numeric IDs and revision, different
    // contents. A cache must never leak draws from the previous activation.
    ++activation;
    records[3].slots = 1023;
    prepare();
    compare(cache, reference(records, visible, eye));
    // Exercise generation invalidation independently of section-count changes.
    ++activation;
    records = makeRecords(4);
    visible.assign(4, 1);
    prepare();
    compare(cache, reference(records, visible, eye));
    for (auto& record : records)
        record.slots = 0;
    ++meshRevision;
    prepare();
    DRAW_CHECK(cache.residentCount() == 0 && cache.transparentTerrain.empty());
    for (auto const& list : cache.lists)
        DRAW_CHECK(list.empty());

    // Session teardown owns the cache; it can be destroyed before/after source
    // storage without touching any game resource or callback.
    cache = ProjectionDrawCache{};
    records.clear();
    visible.clear();
    DRAW_CHECK(cache.orderBuilds == 0 && cache.recordBuilds == 0 && cache.listBuilds == 0);
    prepare();
    DRAW_CHECK(cache.residentCount() == 0);
}

void testMaterialPassRouting() {
    std::vector<ProjectionDrawRecord> records{
        {0, {100, 0, 0}, projectionDrawBit(ProjectionDrawSlot::Opaque)},
        {1, {200, 0, 0}, projectionDrawBit(ProjectionDrawSlot::Alpha)},
        {2, {300, 0, 0}, projectionDrawBit(ProjectionDrawSlot::AlphaOneSided)},
        {3, {1, 0, 0}, projectionDrawBit(ProjectionDrawSlot::Blend)},
        {4, {2, 0, 0}, projectionDrawBit(ProjectionDrawSlot::Liquid)},
        {5, {3, 0, 0}, projectionDrawBit(ProjectionDrawSlot::Blend)},
    };
    ProjectionDrawCache cache;
    cache.prepare(1, 1, 1, records.size(), {},
        [&](std::size_t i) { return records[i]; }, [](std::size_t) { return true; });
    // Far opaque/cutout meshes must stay in their own passes. Only blend
    // terrain and liquids need distance ordering, each in its existing list.
    for (std::size_t slot = 0; slot < 3; ++slot) {
        DRAW_CHECK(cache.lists[slot] == std::vector<std::size_t>{slot});
    }
    std::vector<ProjectionTerrainDraw> const terrain{{5, 3}, {3, 3}};
    DRAW_CHECK(cache.transparentTerrain == terrain);
    DRAW_CHECK(cache.lists[static_cast<std::size_t>(ProjectionDrawSlot::Liquid)]
        == std::vector<std::size_t>{4});
}

void testBadDistancesAndViewKeys() {
    ProjectionDrawCache cache;
    std::vector<ProjectionDrawRecord> records{{0, {1, 0, 0}, 8},
                                              {1, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, 8},
                                              {2, {std::numeric_limits<float>::infinity(), 0, 0}, 8},
                                              {3, {-1, 0, 0}, 8}};
    std::vector<unsigned char> visible(4, 1);
    cache.prepare(
        1, 1, 1, records.size(), {}, [&](std::size_t i) { return records[i]; },
        [](std::size_t) { return true; });
    compare(cache, reference(records, visible, {}));
    ProjectionViewKey key;
    auto changed = key;
    DRAW_CHECK(changed == key);
    changed.originX = 1;
    DRAW_CHECK(changed != key);
    changed = key;
    changed.renderDistance = 128;
    DRAW_CHECK(changed != key);
    changed = key;
    changed.matrix[5] = 1;
    DRAW_CHECK(changed != key);
    changed = key;
    changed.meshRevision = 1;
    DRAW_CHECK(changed != key);
    changed = key;
    changed.activation = 1;
    DRAW_CHECK(changed != key);
    changed = key;
    changed.sections = 1;
    DRAW_CHECK(changed != key);
    changed = key;
    changed.hasMatrix = true;
    DRAW_CHECK(changed != key);
}

void testProgressivePublishing() {
    auto records = makeRecords(128);
    for (auto& record : records)
        record.slots = 0;
    std::vector<unsigned char> visible(records.size(), 1);
    ProjectionDrawCache cache;
    std::size_t lastCapacity{}, capacityChanges{};
    for (std::size_t uploaded = 0; uploaded < records.size(); ++uploaded) {
        records[uploaded].slots = 1023;
        cache.prepare(
            1, uploaded + 1, uploaded + 1, records.size(), {},
            [&](std::size_t i) { return records[i]; }, [](std::size_t) { return true; });
        compare(cache, reference(records, visible, {}));
        if (cache.transparentTerrain.capacity() != lastCapacity)
            ++capacityChanges;
        lastCapacity = cache.transparentTerrain.capacity();
    }
    // Adding one mesh per frame must not force an allocation on every frame.
    DRAW_CHECK(capacityChanges < 16);
    auto const capacity = cache.transparentTerrain.capacity();
    for (auto& record : records)
        record.slots = 0;
    cache.prepare(
        1, 1000, 1000, records.size(), {}, [&](std::size_t i) { return records[i]; },
        [](std::size_t) { return true; });
    DRAW_CHECK(cache.transparentTerrain.empty());
    DRAW_CHECK(cache.transparentTerrain.capacity() == capacity);
}
} // namespace

int runProjectionDrawCacheTests() {
    testInvalidationAndLifetime();
    testMaterialPassRouting();
    testBadDistancesAndViewKeys();
    testProgressivePublishing();
    std::printf("ProjectionDrawCacheTests: %d checks, %d failures\n", checks, failures);
    return failures;
}

void benchmarkProjectionDrawCache() {
    constexpr std::size_t count = 16384, frames = 240;
    auto records = makeRecords(count);
    std::vector<unsigned char> visible(count);
    auto turn = [&](std::size_t frame) {
        for (std::size_t i = 0; i < count; ++i)
            visible[i] = (i + frame * 13) % 7 < 3;
    };
    for (int scenario = 0; scenario < 3; ++scenario) {
        std::size_t checksum{};
        auto position = [&](std::size_t frame) {
            return scenario == 0 ? ProjectionDrawPosition{}
                                 : ProjectionDrawPosition{static_cast<float>(frame) * 0.25f, 2,
                                                          static_cast<float>(frame) * 0.125f};
        };
        auto updateView = [&](std::size_t frame) {
            if (scenario != 2) {
                turn(frame);
                return;
            }
            for (std::size_t i = 0; i < count; ++i)
                visible[i] = (i + frame * 7) % 31 == 0;
        };
        auto started = std::chrono::steady_clock::now();
        for (std::size_t frame = 0; frame < frames; ++frame) {
            updateView(frame);
            auto lists = reference(records, visible, position(frame));
            checksum += lists.terrain.size();
        }
        auto const baseline = std::chrono::steady_clock::now() - started;
        ProjectionDrawCache cache;
        started = std::chrono::steady_clock::now();
        for (std::size_t frame = 0; frame < frames; ++frame) {
            updateView(frame);
            cache.prepare(
                1, 1, frame, count, position(frame), [&](std::size_t i) { return records[i]; },
                [&](std::size_t i) { return visible[i]; });
            // Model both engine passes, which must share the prepared lists.
            cache.prepare(
                1, 1, frame, count, position(frame), [&](std::size_t i) { return records[i]; },
                [&](std::size_t i) { return visible[i]; });
            checksum += cache.transparentTerrain.size();
        }
        auto const cached = std::chrono::steady_clock::now() - started;
        std::printf("Draw-list CPU benchmark ONLY (no Minecraft/GPU), scenario %d: %zu sections, %zu frames; "
                    "rebuild %.3f ms, cache %.3f ms; record/order builds %llu/%llu; checksum %zu\n",
                    scenario, count, frames, std::chrono::duration<double, std::milli>(baseline).count(),
                    std::chrono::duration<double, std::milli>(cached).count(),
                    static_cast<unsigned long long>(cache.recordBuilds),
                    static_cast<unsigned long long>(cache.orderBuilds), checksum);
    }
}
