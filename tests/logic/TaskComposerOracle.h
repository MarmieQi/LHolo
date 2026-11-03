#pragma once
#include "structure/TaskComposer.h"
#include <functional>
namespace lholo::structure {
int coveringSource(LoadedStructure const &, int x, int y, int z);
using BlockTransform = std::function<Block const *(Block const *, int, int)>;
struct OracleLoadedStructure : LoadedStructure {
    struct FileBounds {
        int sourceIndex{-1};
        // Full-file box in composite-local space; maximum is exclusive.
        // Keep wide coordinates because slicing can leave bounds outside the
        // normalized occupied area. This is display data, not air coverage.
        std::array<long long, 3> minimum{}, maximum{};
    };
    std::vector<FileBounds> sourceBounds;
};
struct OracleComposition {
    std::shared_ptr<OracleLoadedStructure> loaded;
    std::array<int, 3> origin{};
};
OracleComposition composeTask(std::span<CompositionFile const>, BlockTransform const &);
} // namespace lholo::structure
