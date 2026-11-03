#pragma once
#include "structure/TaskComposer.h"
namespace lholo::structure {
// The original file data and transforms, with no block merging or dependency graph.
struct IndependentTaskFiles {
    std::vector<CompositionFile> files;
};
Composition composeTaskProjection(std::span<CompositionFile const>);
CompositionFile const* independentTaskFile(LoadedStructure const&, int source);
bool hasVisibleTaskFiles(LoadedStructure const&);
} // namespace lholo::structure
