#pragma once
#include "structure/StructureLoader.h"
#include "structure/LayerSettings.h"
#include <array>
#include <span>
namespace lholo::structure {
struct CompositionFile {
    std::string id;
    std::shared_ptr<LoadedStructure const> loaded;
    bool visible{true};
    int x{}, y{}, z{}, rotation{}, mirror{};
    LayerSettings slice;
};
struct Composition {
    std::shared_ptr<LoadedStructure> loaded;
    std::array<int, 3> origin{};
};
// Normalize loaded source-file slices once, before publishing the task view.
// Unloaded files retain their settings until their dimensions are available.
void normalizeCompositionLayers(std::span<CompositionFile> files);
} // namespace lholo::structure
