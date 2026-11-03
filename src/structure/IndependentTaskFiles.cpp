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
    return false;
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
} // namespace
Composition composeTaskProjection(std::span<CompositionFile const> files) {
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
