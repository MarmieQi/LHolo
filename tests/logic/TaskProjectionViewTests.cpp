#include "TaskComposerOracle.h"
#include "projection/correction/CorrectionStateStore.h"
#include "projection/runtime/TaskSectionRefresh.h"
#include "projection/world/ProjectionWorldLookup.h"
#include "structure/IndependentTaskFiles.h"
#include "structure/TaskProjectionView.h"

#include <cstdio>
#include <map>
#include <random>
#include <set>

int runTaskProjectionViewTests() {
    using namespace lholo::structure;
    using namespace lholo::projection::detail;
    int failures{}, checks{};
    auto check = [&](bool value, char const *name) {
        ++checks;
        if (!value) {
            if (failures < 15)
                std::printf("TaskProjectionViewTests FAILED: %s\n", name);
            ++failures;
        }
    };
    auto stone = reinterpret_cast<Block const *>(0x1000);
    auto glass = reinterpret_cast<Block const *>(0x2000);
    auto water = reinterpret_cast<Block const *>(0x3000);
    auto nbtBacking = std::make_shared<int>(123);
    auto nbt = std::shared_ptr<CompoundTag const>(nbtBacking, reinterpret_cast<CompoundTag const *>(nbtBacking.get()));
    auto raw = std::make_shared<LoadedStructure>();
    raw->sizeX = 20;
    raw->sizeY = 3;
    raw->sizeZ = 17;
    raw->materialCount = 3;
    raw->regions = {{0, 0, 0, 8, 3, 17}, {12, 0, 0, 8, 3, 17}};
    for (int x = 0; x < 20; ++x)
        for (int y = 0; y < 3; ++y)
            for (int z = 0; z < 17; ++z)
                if ((x + y + z) % 4 == 0 && (x < 8 || x >= 12))
                    raw->renderBlocks.push_back(
                        {x, y, z, (x % 2) ? stone : glass, (z % 3) ? nullptr : water, nbt, x % 3, z % 3});
    auto transform = [](Block const *b, int, int) { return b; };
    std::mt19937 rng(12817);
    for (int scenario = 0; scenario < 144; ++scenario) {
        std::vector<CompositionFile> files{{"A", raw, true, -19, 11, -20, scenario % 4, (scenario / 4) % 3},
                                           {"B", raw, true, -15, 12, -17, (scenario / 12) % 4, (scenario / 48) % 3},
                                           {"C", raw, true, 500, 10, 500}};
        files[0].slice = {static_cast<LayerAxis>((scenario / 3) % 3), static_cast<LayerDisplayMode>((scenario / 9) % 4),
                          1};
        if (scenario % 11 == 0)
            files[0].visible = false;
        if (scenario % 7 == 0)
            std::swap(files[0], files[1]);
        TaskProjectionView view(files);
        auto oracle = composeTask(files, transform);
        auto root = composeTaskProjection(files);
        check(root.loaded->independentFiles && root.loaded->renderBlocks.empty(),
              "even rotated/overlapping tasks never merge cells");
        for (std::size_t i = 0; i < files.size(); ++i)
            check(root.loaded->independentFiles->files[i].loaded == raw, "source storage is shared across entries");
        std::map<std::array<long long, 3>, StructureBlock> expected;
        for (auto cell : oracle.loaded->renderBlocks)
            expected.emplace(std::array<long long, 3>{cell.x + oracle.origin[0], cell.y + oracle.origin[1],
                                                      cell.z + oracle.origin[2]},
                             cell);
        for (std::size_t i = 0; i < files.size(); ++i) {
            if (!files[i].visible)
                continue;
            for (std::size_t index = 0; index < raw->renderBlocks.size(); ++index) {
                auto world = view.worldCell(i, raw->renderBlocks[index]);
                auto owner = view.ownerAt(i, world);
                auto found = expected.find(world);
                check(static_cast<bool>(owner) == (found != expected.end()),
                      "effective cells agree with former composer");
                if (owner && found != expected.end()) {
                    auto winner = files[owner->file].loaded->renderBlocks[owner->index];
                    check(owner->file == static_cast<std::size_t>(found->second.sourceIndex),
                          "order/visibility/slice resolve the same winning file");
                    check(winner.block == found->second.block && winner.liquid == found->second.liquid &&
                              winner.blockEntityNbt == found->second.blockEntityNbt,
                          "body/liquid/NBT stay with one owner");
                }
                check(view.owns(i, index) == (owner && owner->file == i && owner->index == index),
                      "ordinal ownership matches world lookup");
                auto airOwner = view.airOwnerAt(i, world);
                auto oracleAir = found == expected.end()
                                     ? coveringSource(*oracle.loaded, static_cast<int>(world[0] - oracle.origin[0]),
                                                      static_cast<int>(world[1] - oracle.origin[1]),
                                                      static_cast<int>(world[2] - oracle.origin[2]))
                                     : -1;
                check(airOwner ? oracleAir == static_cast<int>(*airOwner) : oracleAir < 0,
                      "air ownership agrees with the former composer's sliced source regions");
            }
            // World-to-local Section mapping must work at negative, unaligned
            // offsets and for reversed/rotated non-multiple-of-16 dimensions.
            for (int probe = 0; probe < 10; ++probe) {
                auto cell = raw->renderBlocks[rng() % raw->renderBlocks.size()];
                auto p = view.worldCell(i, cell);
                TaskCellBounds world{{p[0] - 2, p[1] - 1, p[2] - 3}, {p[0] + 3, p[1] + 2, p[2] + 4}};
                auto local = view.localIntersection(i, world);
                for (int x = 0; x < 20; ++x)
                    for (int y = 0; y < 3; ++y)
                        for (int z = 0; z < 17; ++z)
                            check(local.contains({x, y, z}) ==
                                      world.contains(view.worldCell(i, StructureBlock{x, y, z})),
                                  "world boxes invert exactly to local cells");
            }
        }
    }
    // Every ownership flip, including the MOVING file's old overlap, must
    // lie inside its own mapped dirty volumes after arbitrary pose changes.
    for (int step = 0; step < 60; ++step) {
        std::vector<CompositionFile> previous{{"A", raw, true, -3, 0, -2, step % 4, (step / 4) % 3},
                                              {"B", raw, true, 0, 0, 0},
                                              {"C", raw, true, 1000, 0, 1000}};
        TaskProjectionView oldView(previous);
        auto current = previous;
        current[0].x = step % 2 ? 15 : -25;
        current[0].z = step % 3 ? 4 : -23;
        current[0].rotation = (step + 1) % 4;
        current[0].mirror = (step / 7) % 3;
        TaskProjectionView newView(current);
        for (std::size_t file = 0; file < 2; ++file) {
            auto boxes = newView.changedBounds(file, &oldView);
            for (std::size_t index = 0; index < raw->renderBlocks.size(); ++index) {
                if (oldView.owns(file, index) == newView.owns(file, index))
                    continue;
                auto world = newView.worldCell(file, raw->renderBlocks[index]);
                check(std::any_of(boxes.begin(), boxes.end(), [&](auto const &box) { return box.contains(world); }),
                      "both files' old/new ownership flips are contained in local invalidation volumes");
            }
        }
        check(newView.changedBounds(2, &oldView).empty(),
              "third file stays untouched through all rotations and mirrors");
    }
    // Old and new contact regions are both needed to restore an uncovered B.
    auto tiny = std::make_shared<LoadedStructure>();
    tiny->sizeX = tiny->sizeY = tiny->sizeZ = 2;
    tiny->regions = {{0, 0, 0, 2, 2, 2}};
    tiny->renderBlocks = {{0, 0, 0, stone}};
    std::vector<CompositionFile> files{
        {"A", tiny, true, -1, 0, 0}, {"B", raw, true, -19, 0, -1}, {"C", raw, true, 1000, 0, 1000}};
    TaskProjectionView before(files);
    check(before.matches(files), "stable metadata matches without reconstructing candidate tables");
    files[0].x = -17;
    check(!before.matches(files), "live placement edits publish a new view");
    TaskProjectionView after(files);
    auto changed = after.changedBounds(1, &before);
    auto covered = [&](std::array<long long, 3> const &p) {
        return std::any_of(changed.begin(), changed.end(), [&](auto const &b) { return b.contains(p); });
    };
    check(covered({-1, 0, 0}) && covered({-17, 0, 0}), "detach restores old overlap and updates new overlap");
    check(!covered({-10, 0, 0}), "instantaneous movement does not invalidate the swept volume");
    check(after.changedBounds(2, &before).empty(), "unrelated C has no affected bounds on A movement");
    check(after.changedBounds(1, &after).empty(), "same placement publication never invalidates meshes");
    files[0].visible = false;
    TaskProjectionView hidden(files);
    check(!hidden.changedBounds(1, &after).empty(), "hiding a file restores previous ownership");
    files[0].visible = true;
    std::swap(files[0], files[1]);
    TaskProjectionView reordered(files);
    check(!reordered.changedBounds(0, &after).empty(), "reordering interacting files invalidates winners");
    check(reordered.changedBounds(2, &after).empty(), "reordering does not invalidate distant files");
    // Execute the same Section visitor used by runtime revision invalidation.
    std::vector<std::tuple<int, int, int>> keys;
    std::map<std::tuple<int, int, int>, std::size_t> indices;
    for (int x = 0; x < 100; ++x)
        for (int z = 0; z < 100; ++z) {
            indices.emplace(std::tuple{x, 0, z}, keys.size());
            keys.emplace_back(x, 0, z);
        }
    std::vector<int> revisions(keys.size(), 7);
    visitTaskSections(TaskCellBounds{{17, 0, 33}, {19, 2, 35}}, keys, indices,
                      [&](std::size_t section) { ++revisions[section]; });
    std::size_t dirty{};
    for (auto revision : revisions)
        dirty += revision != 7;
    check(dirty == 1 && revisions[indices.at({1, 0, 2})] == 8,
          "small contact bumps exactly one existing Section revision");
    auto count = keys.size();
    std::size_t sparseVisits{};
    visitTaskSections(TaskCellBounds{{0, 0, 0}, {2000000000, 2000000000, 2000000000}}, keys, indices,
                      [&](std::size_t) { ++sparseVisits; });
    check(sparseVisits == count && keys.size() == count,
          "huge empty bounds use existing sparse Sections without overflow or insertion");
    // Moving an isolated source changes no cross-file model dependencies.
    std::vector<CompositionFile> isolated{{"A", tiny, true}, {"B", raw, true, 1000}};
    TaskProjectionView isolatedBefore(isolated);
    isolated[0].x = 1;
    TaskProjectionView isolatedAfter(isolated);
    check(isolatedAfter.changedBounds(0, &isolatedBefore).empty(),
          "isolated translation preserves all local model geometry");
    // Air ownership follows actual regions, rather than a giant union AABB.
    std::vector<CompositionFile> airFiles{{"A", tiny, true}, {"B", raw, true}};
    TaskProjectionView air(airFiles);
    check(!air.ownsAir(0, {0, 0, 0}) && !air.ownsAir(1, {0, 0, 0}), "expected block in any file is never extra");
    check(air.ownsAir(0, {1, 1, 1}) && !air.ownsAir(1, {1, 1, 1}), "overlapping air has one covering owner");
    check(!air.ownsAir(1, {10, 1, 1}), "gaps between source regions are outside the correction domain");
    auto slicedAirFiles = airFiles;
    slicedAirFiles[0].slice = {LayerAxis::Y, LayerDisplayMode::Single, 0};
    TaskProjectionView slicedAir(slicedAirFiles);
    check(slicedAir.ownsAir(1, {1, 1, 1}), "a cropped-away high-priority air region cannot hide lower-file extras");
    auto airOnly = std::make_shared<LoadedStructure>();
    airOnly->sizeX = 2;
    airOnly->sizeY = airOnly->sizeZ = 1;
    airOnly->volume = 2;
    airOnly->regions.push_back({0, 0, 0, 2, 1, 1});
    airOnly->omittedCells = {1};
    check(!airOnly->hasProjectionContent(), "omitted cells alone do not make a projectable structure");
    airOnly->hasAirCells = true;
    check(airOnly->hasProjectionContent(), "explicit air is projectable without model blocks");
    std::vector<CompositionFile> airOnlyFiles{{"air", airOnly, true}};
    TaskProjectionView airOnlyTask(airOnlyFiles);
    ProjectionWorldLookup airOnlyLookup(airOnly, {}, 0, 0, {}, {});
    check(airOnlyTask.ownsAir(0, {0, 0, 0}) && airOnlyLookup.ownsAir({0, 0, 0}),
          "explicit air is covered in task and standalone projections");
    check(!airOnlyTask.ownsAir(0, {1, 0, 0}) && !airOnlyLookup.ownsAir({1, 0, 0}),
          "omitted cells are excluded in task and standalone projections");
    // Native view returns foreign blocks but never foreign local ordinals.
    auto task = std::make_shared<TaskWorldLookups>();
    task->placements = std::make_shared<TaskProjectionView const>(airFiles);
    task->files = {std::make_shared<ProjectionWorldLookup>(tiny, std::array<long long, 3>{}, 0, 0, LayerSettings{},
                                                           ProjectionWorldLookup::BlockTransforms{{stone, glass}}),
                   std::make_shared<ProjectionWorldLookup>(
                       raw, std::array<long long, 3>{}, 0, 0, LayerSettings{},
                       ProjectionWorldLookup::BlockTransforms{{stone, stone}, {glass, glass}, {water, water}})};
    task->actors.resize(2);
    auto candidates = task->placements->sectionCandidates(1, {{16, 0, 0}, {20, 3, 17}});
    check(candidates == std::vector<std::size_t>{1},
          "a distant Section queries no foreign source despite file-wide AABB overlap");
    auto b = task->files[1]->withTask(task, 1);
    check(!b->indexAt({0, 0, 0}) && b->blockAt({0, 0, 0}) == glass,
          "foreign block lookup cannot masquerade as a local ordinal");
    auto sectionView = b->forSection({{16, 0, 0}, {20, 3, 17}});
    check(sectionView->indexAt({16, 0, 0}) == b->indexAt({16, 0, 0}) &&
              sectionView->blockAt({16, 0, 0}) == b->blockAt({16, 0, 0}),
          "Section-local candidate restriction preserves effective cells");
    auto old = b;
    auto movedFiles = airFiles;
    movedFiles[0].x = 100;
    auto moved = std::make_shared<TaskWorldLookups>(*task);
    moved->placements = std::make_shared<TaskProjectionView const>(movedFiles);
    check(moved->files[1]->withTask(moved, 1)->indexAt({0, 0, 0}).has_value() && !old->indexAt({0, 0, 0}),
          "old worker placement stays immutable after moving A away");
    // Source air has no ordinal, but standalone extra-shell queries still
    // need its inverse local coordinate under every pose and negative origin.
    for (int mirror = 0; mirror < 3; ++mirror)
        for (int rotation = 0; rotation < 4; ++rotation) {
            ProjectionWorldLookup standalone(raw, {-513, -31, 33}, mirror, rotation, LayerSettings{},
                                             {{stone, stone}, {glass, glass}, {water, water}});
            auto p = transformCell(1, 1, 1, raw->sizeX, raw->sizeZ, mirror, rotation);
            ProjectionWorldLookup::Key key{p[0] - 513, p[1] - 31, p[2] + 33};
            check(standalone.localCellAt(key) == std::optional{std::array{1, 1, 1}} && !standalone.indexAt(key),
                  "standalone source-air coordinates remain queryable without fabricating a block index");
            check(!standalone.localCellAt({-100000, -100000, -100000}), "out-of-bounds extra queries remain empty");
        }
    // Epoch reset preserves old worker state and avoids an all-cell fill.
    CorrectionStateStore store;
    std::vector<std::vector<std::size_t>> sections{{0, 1}, {2}};
    store.initialize(sections, 3);
    store.set(0, CorrectionState::Correct);
    auto snapshot = store.capture(std::array<std::size_t, 1>{0});
    check(store.sectionHasResolvedCells(0) && !store.sectionHasResolvedCells(1),
          "only Sections with resolved world facts need model restoration on move");
    store.fill(CorrectionState::Missing);
    check(!store.sectionHasResolvedCells(0), "reset epoch clears resolved summary without scanning cells");
    check(store.get(0) == CorrectionState::Missing && snapshot.rawState(0) == CorrectionState::Correct,
          "placement epoch isolates previous correction pages");
    store.set(1, CorrectionState::WrongType);
    check(store.get(0) == CorrectionState::Missing && store.get(1) == CorrectionState::WrongType &&
              snapshot.rawState(0) == CorrectionState::Correct,
          "first changed page lazily initializes only its own states");
    TaskSectionRefresh pending;
    pending.request(1);
    pending.request(1);
    pending.advance(1, 3);
    check(pending.section() == 1 && pending.offset() == 1, "section refresh coalesces and resumes cell work");
    pending.request(1);
    check(pending.offset() == 0, "new edits restart partial ownership work against the latest view");
    pending.advance(3, 3);
    check(pending.empty(), "completed ownership work leaves no periodic scans");
    TaskAirRefresh airWork;
    airWork.request({{-17, -1, 15}, {-15, 1, 17}});
    std::set<std::array<int, 3>> tiles;
    while (!airWork.empty())
        tiles.insert(airWork.take());
    check(tiles.size() == 8 && tiles.contains({-2, -1, 0}) && tiles.contains({-1, 0, 1}),
          "negative unaligned air volumes enumerate world Sections with floor division");
    std::printf("TaskProjectionViewTests: %d checks, %d failures\n", checks, failures);
    return failures;
}
