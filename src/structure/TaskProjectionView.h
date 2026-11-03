#pragma once

#include "structure/StructureTransform.h"
#include "structure/TaskComposer.h"

#include <algorithm>
#include <optional>
#include <string_view>

namespace lholo::structure {

// Half-open cell bounds. This metadata never contains an expanded blueprint.
struct TaskCellBounds {
    std::array<long long, 3> minimum{}, maximum{};
    bool empty() const;
    TaskCellBounds expanded(int margin) const;
    TaskCellBounds intersect(TaskCellBounds const &) const;
    bool contains(std::array<long long, 3> const &) const;
    bool operator==(TaskCellBounds const &) const = default;
};

struct TaskCellOwner {
    std::size_t file{}, index{};
    bool operator==(TaskCellOwner const &) const = default;
};

// Immutable task-space placement/ownership. Source data always stays in its
// original file. Callers query only AABB candidates of their own file, then
// the source's sparse coordinate index. Task order is the sole winner rule.
class TaskProjectionView {
  public:
    explicit TaskProjectionView(std::span<CompositionFile const>);
    std::span<CompositionFile const> files() const { return mFiles; }
    std::optional<std::size_t> fileIndex(std::string_view id) const;
    std::span<std::size_t const> neighbors(std::size_t file) const { return mNeighbors[file]; }
    std::vector<std::size_t> sectionCandidates(std::size_t file, TaskCellBounds const &world) const;
    std::array<long long, 3> worldCell(std::size_t file, StructureBlock const &) const;
    std::optional<std::array<int, 3>> localCell(std::size_t file, std::array<long long, 3> const &) const;
    TaskCellBounds localIntersection(std::size_t file, TaskCellBounds const &world) const;
    std::optional<std::size_t> indexAt(std::size_t file, std::array<long long, 3> const &) const;
    std::optional<TaskCellOwner> ownerAt(std::size_t queryingFile, std::array<long long, 3> const &) const;
    std::optional<TaskCellOwner> ownerAt(std::span<std::size_t const> candidates,
                                         std::array<long long, 3> const &) const;
    bool owns(std::size_t file, std::size_t index) const;
    bool owns(std::size_t file, std::size_t index, std::span<std::size_t const> candidates) const;
    // Unique ownership of source air; another file's expected cell is never extra.
    std::optional<std::size_t> airOwnerAt(std::size_t file, std::array<long long, 3> const &) const;
    std::optional<std::size_t> airOwnerAt(std::span<std::size_t const> candidates,
                                          std::array<long long, 3> const &) const;
    bool ownsAir(std::size_t file, std::array<long long, 3> const &) const;
    // Old and new interaction volumes, clipped to the current target. No swept
    // volume, and no invalidation of a distant unchanged file on task publication.
    std::vector<TaskCellBounds> changedBounds(std::size_t file, TaskProjectionView const *previous) const;
    bool matches(std::span<CompositionFile const>) const;

  private:
    bool covers(std::size_t file, std::array<long long, 3> const &) const;
    std::vector<CompositionFile> mFiles;
    std::vector<TaskCellBounds> mBounds;
    std::vector<std::vector<std::size_t>> mNeighbors;
};

bool sameTaskPlacement(CompositionFile const &, CompositionFile const &);

} // namespace lholo::structure
