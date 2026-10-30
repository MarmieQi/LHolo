#include "structure/IndependentTaskFiles.h"
#include "structure/StructureGeneration.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace lholo::structure {
bool hasVisibleTaskFiles(LoadedStructure const& loaded) {
    if (loaded.independentFiles)
        return std::any_of(loaded.independentFiles->files.begin(), loaded.independentFiles->files.end(),
            [](auto const& file) { return file.visible && file.loaded; });
    // Full-file bounds are retained even when the display slice is empty.
    return !loaded.sourceBounds.empty();
}
namespace {
std::array<long long, 6> bounds(CompositionFile const& f) {
    bool rotated = (f.rotation & 1) != 0;
    return {f.x,
            f.y,
            f.z,
            static_cast<long long>(f.x) + (rotated ? f.loaded->sizeZ : f.loaded->sizeX),
            static_cast<long long>(f.y) + f.loaded->sizeY,
            static_cast<long long>(f.z) + (rotated ? f.loaded->sizeX : f.loaded->sizeZ)};
}
bool touchesOrOverlaps(std::array<long long, 6> const& a, std::array<long long, 6> const& b) {
    // Half-open file bounds: touching faces need the shared neighbor lookup.
    for (int axis = 0; axis < 3; ++axis)
        if (a[axis + 3] + 1 <= b[axis] || b[axis + 3] + 1 <= a[axis])
            return false;
    return true;
}
} // namespace
bool independentTaskPlacement(CompositionFile const& file, std::span<CompositionFile const> others) {
    auto a = bounds(file);
    for (auto const& other : others) {
        if (other.id == file.id || !other.visible || !other.loaded)
            continue;
        if (touchesOrOverlaps(a, bounds(other)))
            return false;
    }
    return true;
}
Composition composeTaskProjection(std::span<CompositionFile const> files, BlockTransform const& transform) {
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (!files[i].visible || !files[i].loaded)
            continue;
        auto a = bounds(files[i]);
        for (std::size_t j = 0; j < i; ++j) {
            if (!files[j].visible || !files[j].loaded)
                continue;
            if (touchesOrOverlaps(a, bounds(files[j])))
                return composeTask(files, transform);
        }
    }
    auto loaded = std::make_shared<LoadedStructure>();
    auto independent = std::make_shared<IndependentTaskFiles>();
    independent->files.assign(files.begin(), files.end());
    loaded->independentFiles = std::move(independent);
    loaded->composed = true;
    loaded->generation = nextStructureGeneration();
    for (auto const& file : files) {
        loaded->sourceFiles.push_back(file.id);
        if (!file.visible || !file.loaded)
            continue;
        loaded->primaryBlocks += file.loaded->primaryBlocks;
        loaded->secondaryBlocks += file.loaded->secondaryBlocks;
        auto box = bounds(file);
        for (int axis = 0; axis < 3; ++axis)
            if (box[axis + 3] - 1 > std::numeric_limits<int>::max())
                throw std::runtime_error("Task coordinates exceed supported range");
    }
    return {std::move(loaded), {0, 0, 0}};
}
CompositionFile const* independentTaskFile(LoadedStructure const& loaded, int source) {
    if (!loaded.independentFiles || source < 0 ||
        static_cast<std::size_t>(source) >= loaded.independentFiles->files.size())
        return nullptr;
    return &loaded.independentFiles->files[source];
}
} // namespace lholo::structure
