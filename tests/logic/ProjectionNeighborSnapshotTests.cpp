#include "projection/core/ProjectionRules.h"
#include "projection/correction/CorrectionStateStore.h"
#include "projection/runtime/TaskSectionRefresh.h"
#include "projection/world/ProjectionWorldLookup.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

int runProjectionNeighborSnapshotTests() {
    using namespace lholo::structure;
    using namespace lholo::projection::detail;
    int checks{}, failures{};
    auto check = [&](bool ok, char const *message) {
        ++checks;
        if (!ok) {
            if (failures < 12)
                std::printf("ProjectionNeighborSnapshotTests FAILED: %s\n", message);
            ++failures;
        }
    };
    auto stone = reinterpret_cast<Block const *>(0x1000);
    auto larger = std::make_shared<LoadedStructure>();
    larger->sizeX = larger->sizeY = larger->sizeZ = 48;
    std::map<SubChunkKey, std::size_t> pageIndices;
    std::vector<SubChunkKey> pageKeys;
    std::vector<std::vector<std::size_t>> layout;
    for (int x = 0; x < 48; ++x)
        for (int y = 0; y < 48; ++y)
            for (int z = 0; z < 48; ++z) {
                auto [it, added] = pageIndices.try_emplace({x / 16, y / 16, z / 16}, layout.size());
                if (added) {
                    pageKeys.push_back(it->first);
                    layout.emplace_back();
                }
                layout[it->second].push_back(larger->renderBlocks.size());
                larger->renderBlocks.push_back({x, y, z, stone});
            }
    CorrectionStateStore states;
    states.initialize(layout, larger->renderBlocks.size());
    for (std::size_t index = 0; index < larger->renderBlocks.size(); ++index)
        states.set(index, index % 2 ? CorrectionState::WrongState : CorrectionState::Missing);
    std::array<int, 3> directions[6]{{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    for (auto size : {std::array{16, 16, 16}, std::array{19, 17, 23}}) {
        auto small = std::make_shared<LoadedStructure>();
        small->sizeX = size[0];
        small->sizeY = size[1];
        small->sizeZ = size[2];
        std::array<std::array<int, 3>, 6> faces{
            {{0, 8, 8}, {size[0] - 1, 8, 8}, {8, 0, 8}, {8, size[1] - 1, 8}, {8, 8, 0}, {8, 8, size[2] - 1}}};
        for (auto p : faces)
            small->renderBlocks.push_back({p[0], p[1], p[2], stone});
        std::sort(small->renderBlocks.mutableValues().begin(), small->renderBlocks.mutableValues().end(),
                  [](auto const &a, auto const &b) { return std::array{a.x, a.y, a.z} < std::array{b.x, b.y, b.z}; });
        for (auto origin : {std::array{0, 0, 0}, std::array{-513, -31, 33}})
            for (int mirror = 0; mirror < 3; ++mirror)
                for (int rotation = 0; rotation < 4; ++rotation)
                    for (int otherMirror = 0; otherMirror < 3; ++otherMirror)
                        for (int otherRotation = 0; otherRotation < 4; ++otherRotation) {
                            std::vector<CompositionFile> files{
                                {"small", small, true, origin[0], origin[1], origin[2], rotation, mirror},
                                {"larger", larger, true, origin[0] - 16, origin[1] - 16, origin[2] - 16, otherRotation,
                                 otherMirror}};
                            auto task = std::make_shared<TaskWorldLookups>();
                            task->placements = std::make_shared<TaskProjectionView const>(files);
                            for (auto const &file : files)
                                task->files.push_back(std::make_shared<ProjectionWorldLookup>(
                                    file.loaded, std::array<long long, 3>{file.x, file.y, file.z}, file.mirror,
                                    file.rotation, file.slice, ProjectionWorldLookup::BlockTransforms{{stone, stone}}));
                            for (std::size_t face = 0; face < faces.size(); ++face) {
                                auto cell = faces[face];
                                auto delta = directions[face];
                                SubChunkKey key{cell[0] / 16, cell[1] / 16, cell[2] / 16};
                                // Match the production Worker capture scope; a queried
                                // face can lie outside the SMALL file but inside larger.
                                auto bounds = projectionSectionQueryWorldBounds(
                                    key, *small, {origin[0], origin[1], origin[2]}, mirror, rotation, 2);
                                auto lookup =
                                    task->files[0]->withTask(task, 0)->forSection({bounds.minimum, bounds.maximum});
                                auto p = transformCell(cell[0] + delta[0], cell[1] + delta[1], cell[2] + delta[2],
                                                       small->sizeX, small->sizeZ, mirror, rotation);
                                ProjectionWorldLookup::Key world{origin[0] + p[0], origin[1] + p[1], origin[2] + p[2]};
                                auto owner = lookup->ownerAt(world);
                                check(owner && owner->file == 1, "external shell face resolves the neighboring file");
                                if (!owner || owner->file != 1)
                                    continue;
                                auto local = task->placements->localIntersection(1, {bounds.minimum, bounds.maximum});
                                std::vector<std::size_t> pages;
                                visitTaskSections(local, pageKeys, pageIndices,
                                                  [&](auto page) { pages.push_back(page); });
                                bool captured =
                                    std::find(pages.begin(), pages.end(), states.section(owner->index)) != pages.end();
                                check(captured, "every resolved foreign face has its correction page in the snapshot");
                                // On the broken version don't dereference an omitted
                                // page: report the reproducible coverage error instead.
                                if (captured) {
                                    auto snapshot = states.capture(pages);
                                    check(snapshot.rawState(owner->index) == states.get(owner->index),
                                          "Worker reads the captured neighboring state");
                                }
                            }
                        }
    }
    // A file that merely touches the boundary must not disappear from the
    // Worker candidate list. The old clipped range omitted it entirely.
    auto touchingSource = std::make_shared<LoadedStructure>();
    touchingSource->sizeX = touchingSource->sizeY = touchingSource->sizeZ = 16;
    touchingSource->renderBlocks = {{15, 8, 8, stone}};
    for (int offset : {-17, 0, 17}) {
        auto task = std::make_shared<TaskWorldLookups>();
        std::vector<CompositionFile> files{{"A", touchingSource, true, offset, 0, 0},
                                           {"B", larger, true, offset + 16, 0, 0}};
        task->placements = std::make_shared<TaskProjectionView const>(files);
        for (auto const &file : files)
            task->files.push_back(std::make_shared<ProjectionWorldLookup>(
                file.loaded, std::array<long long, 3>{file.x, file.y, file.z}, 0, 0, LayerSettings{},
                ProjectionWorldLookup::BlockTransforms{{stone, stone}}));
        auto bounds = projectionSectionQueryWorldBounds({0, 0, 0}, *touchingSource, {offset, 0, 0}, 0, 0, 2);
        auto lookup = task->files[0]->withTask(task, 0)->forSection({bounds.minimum, bounds.maximum});
        auto owner = lookup->ownerAt({offset + 16, 8, 8});
        check(owner && owner->file == 1, "touching-file candidates survive the external Section halo");
        auto local = task->placements->localIntersection(1, {bounds.minimum, bounds.maximum});
        std::vector<std::size_t> pages;
        visitTaskSections(local, pageKeys, pageIndices, [&](auto page) { pages.push_back(page); });
        check(pages.size() <= 27, "the neighbor snapshot remains bounded to intersecting Sections");
        if (owner && std::find(pages.begin(), pages.end(), states.section(owner->index)) != pages.end()) {
            auto snapshot = states.capture(pages);
            auto expected = states.get(owner->index);
            states.set(owner->index, CorrectionState::Correct);
            check(snapshot.rawState(owner->index) == expected,
                  "an in-flight boundary snapshot survives later world changes");
        } else
            check(false, "touching foreign face is captured before Worker publication");
    }
    // Pending halo events can affect models just outside the file, while the
    // original cell-domain bounds must stay clipped for source/event scans.
    auto cellBounds = projectionSectionCellWorldBounds({0, 0, 0}, *touchingSource, {0, 0, 0}, 0, 0, 1);
    auto queryBounds = projectionSectionQueryWorldBounds({0, 0, 0}, *touchingSource, {0, 0, 0}, 0, 0, 1);
    check(!cellBounds.intersects({16, 8, 8}, {16, 8, 8}) && queryBounds.intersects({16, 8, 8}, {16, 8, 8}),
          "cell coverage and model query scopes have distinct outer-file boundaries");
    // A malformed snapshot must fail through the existing Worker exception
    // path in Release too; never dereference lower_bound(end) or return air.
    auto incomplete = states.capture(std::array<std::size_t, 1>{0});
    bool rejectedMissingPage = false;
    try {
        (void)incomplete.rawState(layout[1][0]);
    } catch (std::logic_error const &) {
        rejectedMissingPage = true;
    }
    check(rejectedMissingPage, "uncaptured page reads report an invariant failure in Release");
    std::printf("ProjectionNeighborSnapshotTests: %d checks, %d failures\n", checks, failures);
    return failures;
}
