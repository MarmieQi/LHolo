// Regression tests for conservative visibility and voxel shell extraction.
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionShellRules.h"
#include "projection/core/ProjectionVisibilityRules.h"
#include "projection/core/ProjectionResidencyScan.h"

#include <bit>
#include <cstdio>
#include <limits>

namespace {
int checks{};
int failures{};
#define CULL_CHECK(condition)                                                                                \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(condition)) {                                                                                  \
            ++failures;                                                                                      \
            std::fprintf(stderr, "Culling FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                \
        }                                                                                                    \
    } while (false)

using namespace lholo::projection::detail;

void testFrustum() {
    ProjectionFrustum invalid;
    CULL_CHECK(invalid.isBoxVisible({100, 100, 100}, {101, 101, 101}));
    auto const identity = ProjectionFrustum::fromViewProjection(glm::mat4{1});
    CULL_CHECK(identity.isValid());
    CULL_CHECK(identity.isBoxVisible({-.5f, -.5f, -.5f}, {.5f, .5f, .5f}));
    CULL_CHECK(!identity.isBoxVisible({2, 0, 0}, {3, 1, 1}));
    CULL_CHECK(!identity.isBoxVisible({-3, 0, 0}, {-2, 1, 1}));
    CULL_CHECK(!identity.isBoxVisible({0, 2, 0}, {1, 3, 1}));
    CULL_CHECK(!identity.isBoxVisible({0, -3, 0}, {1, -2, 1}));
    CULL_CHECK(identity.isBoxVisible({1, 0, 0}, {2, 1, 1}));
    CULL_CHECK(identity.isBoxVisible({-2, -2, -2}, {2, 2, 2}));
    CULL_CHECK(identity.isBoxVisible({0, 0, 10000}, {.5f, .5f, 10001}));
    CULL_CHECK(identity.isBoxVisible({2, 0, 0}, {1, 1, 1})); // malformed => fail open
    CULL_CHECK(!ProjectionFrustum::fromViewProjection(glm::mat4{0}).isValid());
    glm::mat4 poisoned{1};
    poisoned[0][0] = std::numeric_limits<float>::quiet_NaN();
    CULL_CHECK(!ProjectionFrustum::fromViewProjection(poisoned).isValid());
    poisoned[0][0] = std::numeric_limits<float>::infinity();
    CULL_CHECK(!ProjectionFrustum::fromViewProjection(poisoned).isValid());

    // A known 90-degree perspective matrix. Side-plane tests must reject
    // objects behind the camera, independent of its depth range convention.
    glm::mat4 projection{0};
    projection[0][0] = projection[1][1] = 1;
    projection[2][2] = -1.002f;
    projection[2][3] = -1;
    projection[3][2] = -.2f;
    for (int convention = 0; convention < 3; ++convention) {
        projection[2][2] = convention == 0 ? -1.002f : convention == 1 ? -1.001f : .001f;
        auto const frustum = ProjectionFrustum::fromViewProjection(projection);
        CULL_CHECK(frustum.isBoxVisible({-1, -1, -6}, {1, 1, -4}));
        CULL_CHECK(!frustum.isBoxVisible({-1, -1, 4}, {1, 1, 6}));
        CULL_CHECK(!frustum.isBoxVisible({8, -1, -6}, {10, 1, -4}));
        CULL_CHECK(frustum.isBoxVisible({4, -1, -6}, {6, 1, -4}));
    }

    ProjectionBounds actual;
    actual.include({-3, 0, 0});
    actual.include({20, 1, 1});
    CULL_CHECK(projectionBoundsVisible(identity, actual, {2, 0, 0}));
    CULL_CHECK(projectionBoundsVisible(identity, ProjectionBounds{}, {1000, 0, 0}));
    ProjectionBounds prefetch;
    prefetch.include({5, 0, 0});
    prefetch.include({6, 1, 1});
    CULL_CHECK(!projectionBoundsVisible(identity, prefetch, {}));
    CULL_CHECK(projectionBoundsVisible(identity, prefetch, {}, 16));
}

void testTransformedBounds() {
    lholo::structure::LoadedStructure loaded;
    loaded.sizeX = 37;
    loaded.sizeY = 19;
    loaded.sizeZ = 70;
    for (int mirror = 0; mirror < 3; ++mirror)
        for (int rotation = 0; rotation < 4; ++rotation) {
            for (auto const &key : {SubChunkKey{-1, -1, -1}, SubChunkKey{0, 0, 0}, SubChunkKey{2, 1, 4}}) {
                auto const bounds = projectionSectionBounds(key, loaded, mirror, rotation);
                auto const [sx, sy, sz] = key;
                CULL_CHECK(bounds.maximum.x - bounds.minimum.x == 16);
                CULL_CHECK(bounds.maximum.y - bounds.minimum.y == 16);
                CULL_CHECK(bounds.maximum.z - bounds.minimum.z == 16);
                for (int x = 0; x < 16; ++x)
                    for (int y = 0; y < 16; ++y)
                        for (int z = 0; z < 16; ++z) {
                            auto const p = transformStructurePosition(
                                BlockPos{sx * 16 + x, sy * 16 + y, sz * 16 + z}, loaded, mirror, rotation);
                            CULL_CHECK(p.x >= bounds.minimum.x && p.x + 1 <= bounds.maximum.x &&
                                       p.y >= bounds.minimum.y && p.y + 1 <= bounds.maximum.y &&
                                       p.z >= bounds.minimum.z && p.z + 1 <= bounds.maximum.z);
                        }
            }
        }
    CULL_CHECK(projectionBuildRank(true, true, false) < projectionBuildRank(false, true, true));
    CULL_CHECK(projectionBuildRank(false, true, false) < projectionBuildRank(false, false, true));
    CULL_CHECK(projectionBuildRank(true, true, true) < projectionBuildRank(true, true, false));
}

void testRenderDistance() {
    ProjectionBounds const section{{0, -1000, 0}, {16, 1000, 16}, true};
    CULL_CHECK(projectionBoundsWithinDistance(section, {128, 0, 0}, 128));
    CULL_CHECK(!projectionBoundsWithinDistance(section, {128.25f, 0, 0}, 128));
    CULL_CHECK(projectionBoundsWithinDistance(section, {-144, 9000, 0}, 128));
    CULL_CHECK(!projectionBoundsWithinDistance(section, {-144.25f, 0, 0}, 128));
    CULL_CHECK(!projectionBoundsWithinDistance(section, {100, 0, 100}, 128));
    CULL_CHECK(projectionBoundsWithinDistance(section, {90, 10000, 90}, 128));
    CULL_CHECK(projectionBoundsWithinDistance(section, {-8, 0, -8}, 0));

    // Compare against a separate closest-point oracle across negative origins,
    // diagonal boundaries, altitude changes and several native view distances.
    for (int chunks : {2, 4, 8, 16, 32})
        for (int x = -10; x <= 10; ++x)
            for (int z = -10; z <= 10; ++z) {
                Vec3 const origin{x * 15.75f, x * 100.0f, z * 15.75f};
                double const closestX = std::clamp(-static_cast<double>(origin.x), 0.0, 16.0);
                double const closestZ = std::clamp(-static_cast<double>(origin.z), 0.0, 16.0);
                double const dx = closestX + origin.x;
                double const dz = closestZ + origin.z;
                float const radius = chunks * 16.0f;
                CULL_CHECK(projectionBoundsWithinDistance(section, origin, radius)
                    == (dx * dx + dz * dz <= static_cast<double>(radius) * radius));
            }

    ProjectionFrustum const noFrustum;
    auto near = classifyProjectionSection(noFrustum, section, section, {128, 0, 0}, 128);
    CULL_CHECK(near.renderVisible && near.buildVisible && near.prefetchVisible && near.withinBuildDistance);
    auto edge = classifyProjectionSection(noFrustum, section, section, {140, 0, 0}, 128);
    CULL_CHECK(!edge.renderVisible && !edge.buildVisible && edge.prefetchVisible && edge.withinBuildDistance);
    auto far = classifyProjectionSection(noFrustum, section, section, {145, 0, 0}, 128);
    CULL_CHECK(!far.renderVisible && !far.buildVisible && !far.prefetchVisible && !far.withinBuildDistance);
    CULL_CHECK(far.withinRetentionDistance); // Keep cached geometry in the outer buffer ring.
    CULL_CHECK(classifyProjectionSection(noFrustum, section, section, {160, 0, 0}, 128).withinRetentionDistance);
    CULL_CHECK(!classifyProjectionSection(noFrustum, section, section, {160.25f, 0, 0}, 128).withinRetentionDistance);
    CULL_CHECK(classifyProjectionSection(noFrustum, section, section, {-176, 9000, 0}, 128).withinRetentionDistance);
    CULL_CHECK(!classifyProjectionSection(noFrustum, section, section, {-176.25f, 0, 0}, 128).withinRetentionDistance);
    auto expanded = classifyProjectionSection(noFrustum, section, section, {145, 0, 0}, 256);
    CULL_CHECK(expanded.renderVisible && expanded.buildVisible && expanded.withinBuildDistance);
    auto unknownMesh = classifyProjectionSection(noFrustum, section, {}, {145, 0, 0}, 128);
    CULL_CHECK(!unknownMesh.renderVisible && !unknownMesh.withinBuildDistance);
    ProjectionBounds const overhang{{-4, 0, 0}, {16, 1, 16}, true};
    auto extendedMesh = classifyProjectionSection(noFrustum, section, overhang, {130, 0, 0}, 128);
    CULL_CHECK(extendedMesh.renderVisible && !extendedMesh.buildVisible);
    CULL_CHECK(classifyProjectionSection(noFrustum, section, overhang, {163, 0, 0}, 128).withinRetentionDistance);
    auto hidden = classifyProjectionSection(ProjectionFrustum::fromViewProjection(glm::mat4{1}), section, section,
                                             {32, 0, 0}, 128);
    CULL_CHECK(!hidden.renderVisible && !hidden.buildVisible && hidden.withinBuildDistance);
    CULL_CHECK(hidden.withinRetentionDistance); // Turning away does not evict geometry.
}

void testResidencyScan() {
    ProjectionViewKey before;
    before.renderDistance = 128;
    auto after = before;
    after.matrix[0] = 2;
    after.originY = 20;
    CULL_CHECK(!projectionResidencyViewChanged(before, after));
    after.originX = 1;
    CULL_CHECK(projectionResidencyViewChanged(before, after));
    after = before; after.renderDistance = 64;
    CULL_CHECK(projectionResidencyViewChanged(before, after));
    after = before; ++after.meshRevision;
    CULL_CHECK(projectionResidencyViewChanged(before, after));
    after = before; ++after.sections;
    CULL_CHECK(projectionResidencyViewChanged(before, after));

    ProjectionResidencyScan scan;
    CULL_CHECK(!scan.pending());
    scan.request(5);
    CULL_CHECK(scan.next() == 0 && scan.next() == 1);
    scan.request(5);
    for (auto expected : {2u, 3u, 4u, 0u, 1u}) CULL_CHECK(scan.next() == expected);
    CULL_CHECK(!scan.pending());
    scan.request(1);
    CULL_CHECK(scan.next() == 0 && !scan.pending());
    scan.request(0);
    CULL_CHECK(!scan.pending());

    // Renewing a large scan every frame must still visit every section.
    constexpr std::size_t count = 1025;
    std::array<bool, count> visited{};
    for (std::size_t frame = 0; frame < 9; ++frame) {
        scan.request(count);
        for (std::size_t work = 0; work < ProjectionResidencyProbesPerFrame; ++work) visited[scan.next()] = true;
    }
    CULL_CHECK(std::all_of(visited.begin(), visited.end(), [](bool v) { return v; }));
    std::size_t drained{};
    while (scan.pending()) { scan.next(); ++drained; }
    CULL_CHECK(drained == count - ProjectionResidencyProbesPerFrame);
}

void testBuildSelection() {
    std::array<ProjectionBuildCandidate, 4> candidates{{{true, false, false, false, true, 1},
                                                        {true, false, true, true, false, 100},
                                                        {true, false, true, true, false, 50},
                                                        {true, false, false, true, false, 1}}};
    auto select = [&](std::uint64_t turn, std::size_t cursor) {
        return selectProjectionBuild(candidates.size(), turn, cursor,
                                     [&](std::size_t i) { return candidates[i]; });
    };
    CULL_CHECK(select(0, 0) == 2); // visible initial beats hidden incremental
    candidates[2].inFlight = true;
    CULL_CHECK(select(0, 0) == 1);
    candidates[1].dirty = false;
    CULL_CHECK(select(0, 0) == 3); // prefetch beats background
    candidates[1].dirty = true;
    candidates[2].inFlight = false;
    candidates[1].incremental = true;
    CULL_CHECK(select(0, 0) == 1); // visible edit beats visible initial
    CULL_CHECK(select(7, 0) == 0); // fairness turn reaches hidden edit
    CULL_CHECK(select(7, 3) == 3); // round robin resumes from cursor
    candidates[3].inFlight = true;
    CULL_CHECK(select(7, 3) == 0);
    for (auto &candidate : candidates)
        candidate.dirty = false;
    CULL_CHECK(!select(0, 0));
    CULL_CHECK(!select(7, 0));
    CULL_CHECK(!selectProjectionBuild(0, 7, 0, [](std::size_t) { return ProjectionBuildCandidate{}; }));
}

void testShell() {
    int calls{};
    CULL_CHECK(!projectionSolidCellEnclosed(false, [&](int, int, int) {
        ++calls;
        return true;
    }));
    CULL_CHECK(calls == 0);
    CULL_CHECK(projectionSolidCellEnclosed(true, [&](int, int, int) {
        ++calls;
        return true;
    }));
    CULL_CHECK(calls == 6);
    calls = 0;
    CULL_CHECK(!projectionSolidCellEnclosed(true, [&](int, int, int) {
        ++calls;
        return false;
    }));
    CULL_CHECK(calls == 1);
    CULL_CHECK(projectionShellFaces(1, [](int, int, int) { return 1; }) == 0);
    CULL_CHECK(projectionShellFaces(1, [](int, int, int) { return 0; }) == 63);
    // Wrong-type > wrong-state > extra > missing: retain category boundaries.
    CULL_CHECK(projectionShellFaces(3, [](int, int, int) { return 2; }) == 63);
    CULL_CHECK(projectionShellFaces(1, [](int, int, int) { return 4; }) == 0);
    for (int gap = 0; gap < 6; ++gap) {
        int face{};
        auto const mask = projectionShellFaces(1, [&](int, int, int) { return face++ == gap ? 0 : 1; });
        CULL_CHECK(mask == (1u << gap));
    }
    // A dense cube spans 16^3 section borders. No artificial faces may appear
    // at those borders; cutting away a layer must expose exactly the new surface.
    for (int height : {32, 16, 1}) {
        unsigned faces{};
        unsigned exposedCells{};
        unsigned neighborQueries{};
        for (int x = 0; x < 32; ++x)
            for (int y = 0; y < height; ++y)
                for (int z = 0; z < 32; ++z) {
                    auto const mask = projectionShellFaces(1, [&](int dx, int dy, int dz) {
                        ++neighborQueries;
                        return x + dx >= 0 && x + dx < 32 && y + dy >= 0 && y + dy < height && z + dz >= 0 &&
                                       z + dz < 32
                                   ? 1
                                   : 0;
                    });
                    faces += std::popcount(mask);
                    exposedCells += mask != 0;
                }
        CULL_CHECK(faces == 2u * (32u * 32u + 2u * 32u * height));
        CULL_CHECK(neighborQueries == 6u * 32u * height * 32u);
        if (height == 32)
            CULL_CHECK(exposedCells == 32u * 32u * 32u - 30u * 30u * 30u);
    }
}
} // namespace

int runProjectionCullingTests() {
    testFrustum();
    testTransformedBounds();
    testRenderDistance();
    testResidencyScan();
    testBuildSelection();
    testShell();
    std::printf("ProjectionCullingTests: %d checks, %d failures\n", checks, failures);
    return failures;
}
