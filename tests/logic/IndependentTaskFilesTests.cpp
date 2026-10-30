#include "structure/IndependentTaskFiles.h"
#include "projection/mesh/ProjectionBuildCompletionQueue.h"
#include "projection/runtime/IndependentFileFrame.h"
#include "projection/runtime/ProjectionDimensionRules.h"
#include <array>
#include <cstdio>
#include <limits>

int runIndependentTaskFilesTests() {
    using namespace lholo::structure;
    int failures{};
    auto check = [&](bool ok, char const* message) {
        if (!ok) {
            ++failures;
            std::printf("IndependentTaskFilesTests FAILED: %s\n", message);
        }
    };
    // Multiple CPU builds can reach a file before its next upload pass. A
    // one-result budget must never discard the rest and strand in-flight bits.
    struct Completion { int section; std::unique_ptr<int> mesh; };
    std::deque<Completion> completed;
    for (int index = 0; index < 3; ++index)
        completed.push_back({index, std::make_unique<int>(index + 10)});
    std::array<bool, 3> inFlight{true, true, true};
    auto first = lholo::projection::detail::takeProjectionBuildResults(completed, 1);
    check(first.size() == 1 && first[0].section == 0 && *first[0].mesh == 10
        && completed.size() == 2 && completed.front().section == 1,
        "upload budget keeps unconsumed file completions and native mesh ownership");
    inFlight[first[0].section] = false;
    check(lholo::projection::detail::takeProjectionBuildResults(completed, 0).empty()
        && completed.size() == 2, "exhausted upload budget preserves queued results");
    for (int expected = 1; expected < 3; ++expected) {
        auto result = lholo::projection::detail::takeProjectionBuildResults(completed, 1);
        check(result.size() == 1 && result[0].section == expected && *result[0].mesh == expected + 10,
            "later frames consume each pending completion exactly once in order");
        inFlight[result[0].section] = false;
    }
    check(completed.empty() && !inFlight[0] && !inFlight[1] && !inFlight[2],
        "all completed sections leave in-flight state instead of becoming permanently missing");

    // Reproduce contention: all files stay dirty and the worker can accept
    // only the first submission in each frame. Fixed file order starves 1/2.
    std::size_t cursor{};
    std::array<int, 3> visited{}, accepted{};
    for (int frame = 0; frame < 6; ++frame) {
        bool busy{};
        lholo::projection::detail::forEachIndependentProjectionFile(3, cursor, [&](std::size_t file) {
            ++visited[file];
            if (!busy) { ++accepted[file]; busy = true; }
        });
    }
    check(visited == std::array<int, 3>{6, 6, 6}, "fair submission never skips a file's frame or live movement");
    check(accepted == std::array<int, 3>{2, 2, 2}, "continuous correction work cannot starve later files");
    cursor = 7;
    int visits{};
    lholo::projection::detail::forEachIndependentProjectionFile(1, cursor, [&](std::size_t file) {
        check(file == 0, "shrinking file list normalizes its cursor"); ++visits;
    });
    check(visits == 1 && cursor == 0, "single-file visit remains once per frame");
    lholo::projection::detail::forEachIndependentProjectionFile(0, cursor, [&](std::size_t) { ++visits; });
    check(visits == 1 && cursor == 0, "empty file list performs no work");
    auto raw = std::make_shared<LoadedStructure>();
    raw->sizeX = 3;
    raw->sizeY = 2;
    raw->sizeZ = 6;
    raw->regions.push_back({0, 0, 0, 3, 2, 6});
    auto stone = reinterpret_cast<Block const*>(0x1000);
    raw->renderBlocks = {{0, 0, 0, stone}, {2, 1, 5, stone}};
    raw->primaryBlocks = 2;
    int transformed{};
    auto transform = [&](Block const* b, int, int) {
        if (b)
            ++transformed;
        return b;
    };
    std::vector<CompositionFile> files{{"A", raw, true}, {"B", raw, true, 30}};
    auto independent = composeTaskProjection(files, transform);
    check(hasVisibleTaskFiles(*independent.loaded), "independent membership does not depend on merged cell count");
    auto outsideDimension = files;
    for (auto& file : outsideDimension) file.visible = false;
    auto emptyView = composeTaskProjection(outsideDimension, transform);
    check(!hasVisibleTaskFiles(*emptyView.loaded), "other-dimension cached files do not count as visible");
    auto missingFiles = files;
    for (auto& file : missingFiles) file.loaded.reset();
    check(!hasVisibleTaskFiles(*composeTaskProjection(missingFiles, transform).loaded),
          "visible but unloaded files do not count as an active projection");
    {
        using namespace lholo::projection::detail;
        auto const originalGeneration = independent.loaded->generation;
        check(classifyDimensionActivation(false, 0, 0, originalGeneration, 0) == DimensionActivationStatus::Ready,
              "initial task load is not a dimension restoration");
        check(classifyDimensionActivation(true, originalGeneration, 0, originalGeneration, 1) == DimensionActivationStatus::Deferred,
              "old view cannot resume in a different dimension");
        check(classifyDimensionActivation(true, originalGeneration, 0, emptyView.loaded->generation, 1) == DimensionActivationStatus::Rebuilding,
              "new empty view completes transition without restoring visible files");
        auto returned = composeTaskProjection(files, transform);
        check(classifyDimensionActivation(true, emptyView.loaded->generation, 1, returned.loaded->generation, 0)
                  == DimensionActivationStatus::Rebuilding && hasVisibleTaskFiles(*returned.loaded),
              "returning task generation retains restoration status and visible membership");
        check(classifyDimensionActivation(true, emptyView.loaded->generation, 1, returned.loaded->generation, 0)
                  == DimensionActivationStatus::Rebuilding,
              "an unsuccessful activation can retry without losing restoration status");
        check(classifyDimensionActivation(false, emptyView.loaded->generation, 1, returned.loaded->generation, 0)
                  == DimensionActivationStatus::Ready,
              "success or explicit close clears the transition and avoids repeated restore hints");
        check(classifyDimensionActivation(true, originalGeneration, 0, originalGeneration, 0) == DimensionActivationStatus::Resuming,
              "legacy single-file resume still restores the retained anchor");
    }
    check(independent.loaded->independentFiles != nullptr, "disjoint AABBs select original per-file data");
    check(transformed == 0 && independent.loaded->renderBlocks.empty(),
          "disjoint branch never merges or transforms blocks");
    check(independentTaskFile(*independent.loaded, 0)->loaded == raw &&
              independentTaskFile(*independent.loaded, 1)->loaded == raw,
          "duplicate path entries share raw data but retain file identity");
    auto moving = files[0];
    moving.x = 5;
    check(independentTaskPlacement(moving, files), "live movement in free space needs no composition");
    moving.x = 29;
    check(!independentTaskPlacement(moving, files),
          "live movement into another file leaves independent branch");
    files[0].x = -400;
    auto moved = composeTaskProjection(files, transform);
    check(moved.origin == std::array<int, 3>{0, 0, 0}, "moving A preserves task anchor");
    check(independentTaskFile(*moved.loaded, 1)->x == 30 &&
              independentTaskFile(*moved.loaded, 1)->loaded ==
                  independentTaskFile(*independent.loaded, 1)->loaded,
          "moving A preserves B data and world offset");
    check(transformed == 0, "XYZ edits of disjoint files have zero block transformation work");
    files[0].x = 0;
    files[1].x = 4;
    check(composeTaskProjection(files, transform).loaded->independentFiles != nullptr,
          "one empty block between boxes stays independent");
    files[1].x = 3;
    check(!composeTaskProjection(files, transform).loaded->independentFiles,
          "touching faces retain shared neighbor models");
    files[1].x = 2;
    auto overlap = composeTaskProjection(files, transform);
    auto oracle = composeTask(files, transform);
    check(hasVisibleTaskFiles(*overlap.loaded), "merged task reports visible files");
    auto emptySliceFiles = files;
    for (auto& file : emptySliceFiles) file.slice = {LayerAxis::Y, LayerDisplayMode::Single, 100};
    auto emptySlice = composeTask(emptySliceFiles, transform);
    check(emptySlice.loaded->renderBlocks.empty() && hasVisibleTaskFiles(*emptySlice.loaded),
          "empty display slice does not mean files disappeared from dimension");
    check(!hasVisibleTaskFiles(*composeTask({}, transform).loaded), "empty merged view has no visible files");
    check(!overlap.loaded->independentFiles &&
              overlap.loaded->renderBlocks.size() == oracle.loaded->renderBlocks.size(),
          "overlap uses established composition");
    files[1].x = 0;
    files[1].y = 3;
    check(composeTaskProjection(files, transform).loaded->independentFiles != nullptr,
          "separation on Y is sufficient");
    files[1].y = 0;
    files[1].z = 7;
    check(composeTaskProjection(files, transform).loaded->independentFiles != nullptr,
          "separation on Z is sufficient");
    files[1].z = 0;
    files[1].x = 5;
    check(composeTaskProjection(files, transform).loaded->independentFiles != nullptr,
          "unrotated width is three");
    files[0].rotation = 1;
    check(!composeTaskProjection(files, transform).loaded->independentFiles,
          "90-degree rotation swaps width/depth before AABB test");
    files[0].rotation = 2;
    files[0].mirror = 2;
    check(composeTaskProjection(files, transform).loaded->independentFiles != nullptr,
          "180-degree rotation and mirror preserve extents");
    files[1].x = 0;
    files[1].visible = false;
    auto hidden = composeTaskProjection(files, transform);
    check(hidden.loaded->independentFiles != nullptr && hidden.loaded->sourceFiles.size() == 2,
          "hidden file retains identity without participating in overlap");
    files[0].visible = false;
    check(composeTaskProjection(files, transform).loaded->independentFiles != nullptr,
          "all hidden remains an empty independent task");
    check(!independentTaskFile(*hidden.loaded, -1) && !independentTaskFile(*hidden.loaded, 2),
          "source bounds validated");
    files[0].visible = true;
    files[0].x = std::numeric_limits<int>::max();
    bool rejected{};
    try {
        composeTaskProjection(files, transform);
    } catch (...) {
        rejected = true;
    }
    check(rejected, "overflowing transformed extent rejected");
    std::printf("IndependentTaskFilesTests: %d failures\n", failures);
    return failures;
}
