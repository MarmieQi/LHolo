#include "structure/TaskProjectionView.h"
#include "structure/LayerVisibility.h"

namespace lholo::structure {
bool TaskCellBounds::empty() const {
    for (int a = 0; a < 3; ++a)
        if (minimum[a] >= maximum[a])
            return true;
    return false;
}
TaskCellBounds TaskCellBounds::expanded(int margin) const {
    if (empty())
        return {};
    auto result = *this;
    for (int a = 0; a < 3; ++a) {
        result.minimum[a] -= margin;
        result.maximum[a] += margin;
    }
    return result;
}
TaskCellBounds TaskCellBounds::intersect(TaskCellBounds const &other) const {
    TaskCellBounds result;
    for (int a = 0; a < 3; ++a) {
        result.minimum[a] = std::max(minimum[a], other.minimum[a]);
        result.maximum[a] = std::min(maximum[a], other.maximum[a]);
    }
    return result;
}
bool TaskCellBounds::contains(std::array<long long, 3> const &p) const {
    for (int a = 0; a < 3; ++a)
        if (p[a] < minimum[a] || p[a] >= maximum[a])
            return false;
    return true;
}
bool sameTaskPlacement(CompositionFile const &a, CompositionFile const &b) {
    return a.id == b.id && a.loaded == b.loaded && a.visible == b.visible && a.x == b.x && a.y == b.y && a.z == b.z &&
           a.rotation == b.rotation && a.mirror == b.mirror && a.slice == b.slice;
}
TaskProjectionView::TaskProjectionView(std::span<CompositionFile const> files) : mFiles(files.begin(), files.end()) {
    mBounds.resize(files.size());
    mNeighbors.resize(files.size());
    for (std::size_t i = 0; i < files.size(); ++i) {
        auto const &f = files[i];
        if (!f.visible || !f.loaded)
            continue;
        bool r = (f.rotation & 1) != 0;
        mBounds[i] = {{f.x, f.y, f.z},
                      {static_cast<long long>(f.x) + (r ? f.loaded->sizeZ : f.loaded->sizeX),
                       static_cast<long long>(f.y) + f.loaded->sizeY,
                       static_cast<long long>(f.z) + (r ? f.loaded->sizeX : f.loaded->sizeZ)}};
    }
    for (std::size_t i = 0; i < files.size(); ++i)
        for (std::size_t j = 0; j < files.size(); ++j)
            // Native model and liquid sampling use a two-cell neighborhood.
            // Biome samples read the real world and do not create file dependencies.
            if (!mBounds[i].expanded(2).intersect(mBounds[j]).empty())
                mNeighbors[i].push_back(j);
}
std::vector<std::size_t> TaskProjectionView::sectionCandidates(std::size_t file, TaskCellBounds const &world) const {
    std::vector<std::size_t> result;
    for (auto candidate : mNeighbors[file])
        if (!world.intersect(mBounds[candidate]).empty())
            result.push_back(candidate);
    return result;
}
std::optional<std::size_t> TaskProjectionView::fileIndex(std::string_view id) const {
    for (std::size_t i = 0; i < mFiles.size(); ++i)
        if (mFiles[i].id == id)
            return i;
    return {};
}
std::array<long long, 3> TaskProjectionView::worldCell(std::size_t file, StructureBlock const &cell) const {
    auto const &f = mFiles[file];
    auto p = transformCell(cell.x, cell.y, cell.z, f.loaded->sizeX, f.loaded->sizeZ, f.mirror, f.rotation);
    return {static_cast<long long>(p[0]) + f.x, static_cast<long long>(p[1]) + f.y, static_cast<long long>(p[2]) + f.z};
}
std::optional<std::array<int, 3>> TaskProjectionView::localCell(std::size_t file,
                                                                std::array<long long, 3> const &p) const {
    if (!mBounds[file].contains(p))
        return {};
    auto const &f = mFiles[file];
    return inverseTransformCell(static_cast<int>(p[0] - f.x), static_cast<int>(p[1] - f.y),
                                static_cast<int>(p[2] - f.z), f.loaded->sizeX, f.loaded->sizeZ, f.mirror, f.rotation);
}
TaskCellBounds TaskProjectionView::localIntersection(std::size_t file, TaskCellBounds const &world) const {
    auto box = mBounds[file].intersect(world);
    if (box.empty())
        return {};
    TaskCellBounds local;
    bool first = true;
    for (int corner = 0; corner < 8; ++corner) {
        std::array<long long, 3> p;
        for (int a = 0; a < 3; ++a)
            p[a] = corner & (1 << a) ? box.maximum[a] - 1 : box.minimum[a];
        auto q = *localCell(file, p);
        for (int a = 0; a < 3; ++a) {
            local.minimum[a] = first ? q[a] : std::min(local.minimum[a], static_cast<long long>(q[a]));
            local.maximum[a] = first ? q[a] + 1LL : std::max(local.maximum[a], q[a] + 1LL);
        }
        first = false;
    }
    return local;
}
std::optional<std::size_t> TaskProjectionView::indexAt(std::size_t file, std::array<long long, 3> const &p) const {
    auto local = localCell(file, p);
    if (!local)
        return {};
    auto const &f = mFiles[file];
    auto index = f.loaded->renderBlocks.find((*local)[0], (*local)[1], (*local)[2]);
    if (!index)
        return {};
    auto cell = f.loaded->renderBlocks[*index];
    if (!cell.block && !cell.liquid)
        return {};
    return layerCellVisible(f.slice, cell.x, cell.y, cell.materialIndex, cell.liquidMaterialIndex) ? index
                                                                                                   : std::nullopt;
}
std::optional<TaskCellOwner> TaskProjectionView::ownerAt(std::size_t file, std::array<long long, 3> const &p) const {
    return ownerAt(mNeighbors[file], p);
}
std::optional<TaskCellOwner> TaskProjectionView::ownerAt(std::span<std::size_t const> candidates,
                                                         std::array<long long, 3> const &p) const {
    for (auto candidate : candidates)
        if (auto index = indexAt(candidate, p))
            return TaskCellOwner{candidate, *index};
    return {};
}
bool TaskProjectionView::owns(std::size_t file, std::size_t index) const {
    return owns(file, index, mNeighbors[file]);
}
bool TaskProjectionView::owns(std::size_t file, std::size_t index, std::span<std::size_t const> candidates) const {
    auto const &cell = mFiles[file].loaded->renderBlocks[index];
    if ((!cell.block && !cell.liquid) || !mFiles[file].visible ||
        !layerCellVisible(mFiles[file].slice, cell.x, cell.y, cell.materialIndex, cell.liquidMaterialIndex))
        return false;
    // No coordinate search in the querying file; the caller already has its ordinal.
    if (candidates.empty() || candidates.front() >= file)
        return true;
    auto world = worldCell(file, cell);
    for (auto other : candidates) {
        if (other >= file)
            break;
        if (indexAt(other, world))
            return false;
    }
    return true;
}
bool TaskProjectionView::covers(std::size_t file, std::array<long long, 3> const &p) const {
    auto local = localCell(file, p);
    if (!local || !layerCellVisible(mFiles[file].slice, (*local)[0], (*local)[1]))
        return false;
    if (mFiles[file].loaded->isOmittedCell((*local)[0], (*local)[1], (*local)[2]))
        return false;
    for (auto const &b : mFiles[file].loaded->regions)
        if ((*local)[0] >= b.x && (*local)[0] < static_cast<long long>(b.x) + b.sizeX && (*local)[1] >= b.y &&
            (*local)[1] < static_cast<long long>(b.y) + b.sizeY && (*local)[2] >= b.z &&
            (*local)[2] < static_cast<long long>(b.z) + b.sizeZ)
            return true;
    return false;
}
std::optional<std::size_t> TaskProjectionView::airOwnerAt(std::size_t file, std::array<long long, 3> const &p) const {
    return airOwnerAt(mNeighbors[file], p);
}
std::optional<std::size_t> TaskProjectionView::airOwnerAt(std::span<std::size_t const> candidates,
                                                          std::array<long long, 3> const &p) const {
    if (ownerAt(candidates, p))
        return {};
    for (auto other : candidates)
        if (covers(other, p))
            return other;
    return {};
}
bool TaskProjectionView::ownsAir(std::size_t file, std::array<long long, 3> const &p) const {
    auto owner = airOwnerAt(file, p);
    return owner && *owner == file;
}
std::vector<TaskCellBounds> TaskProjectionView::changedBounds(std::size_t file, TaskProjectionView const *old) const {
    std::vector<TaskCellBounds> boxes;
    auto before = old ? old->fileIndex(mFiles[file].id) : std::nullopt;
    if (!before || mFiles[file].loaded != old->mFiles[*before].loaded ||
        mFiles[file].visible != old->mFiles[*before].visible || mFiles[file].slice != old->mFiles[*before].slice) {
        if (!mBounds[file].empty())
            boxes.push_back(mBounds[file]);
        return boxes;
    }
    if (!sameTaskPlacement(mFiles[file], old->mFiles[*before])) {
        // Local model geometry follows a translation. Only previous/current
        // cross-file neighborhoods change ownership or projected connections.
        // Map the OLD intersection through local cells into the NEW placement.
        auto add = [&](TaskCellBounds box) {
            if (!box.empty() && std::find(boxes.begin(), boxes.end(), box) == boxes.end())
                boxes.push_back(box);
        };
        for (std::size_t j = 0; j < old->mFiles.size(); ++j) {
            if (j == *before)
                continue;
            auto local = old->localIntersection(*before, old->mBounds[j].expanded(2));
            if (local.empty())
                continue;
            TaskCellBounds mapped;
            bool first = true;
            for (int corner = 0; corner < 8; ++corner) {
                StructureBlock cell;
                cell.x = static_cast<int>((corner & 1) ? local.maximum[0] - 1 : local.minimum[0]);
                cell.y = static_cast<int>((corner & 2) ? local.maximum[1] - 1 : local.minimum[1]);
                cell.z = static_cast<int>((corner & 4) ? local.maximum[2] - 1 : local.minimum[2]);
                auto p = worldCell(file, cell);
                for (int axis = 0; axis < 3; ++axis) {
                    mapped.minimum[axis] = first ? p[axis] : std::min(mapped.minimum[axis], p[axis]);
                    mapped.maximum[axis] = first ? p[axis] + 1 : std::max(mapped.maximum[axis], p[axis] + 1);
                }
                first = false;
            }
            add(mapped);
        }
        for (std::size_t j = 0; j < mFiles.size(); ++j)
            if (j != file)
                add(mBounds[file].intersect(mBounds[j].expanded(2)));
        return boxes;
    }
    auto append = [&](TaskCellBounds const &box) {
        auto clipped = mBounds[file].intersect(box.expanded(2));
        if (!clipped.empty() && std::find(boxes.begin(), boxes.end(), clipped) == boxes.end())
            boxes.push_back(clipped);
    };
    for (std::size_t j = 0; j < mFiles.size(); ++j) {
        if (j == file)
            continue;
        auto previous = old->fileIndex(mFiles[j].id);
        if (!previous || !sameTaskPlacement(mFiles[j], old->mFiles[*previous]) ||
            ((j < file) != (*previous < *before))) {
            append(mBounds[j]);
            if (previous)
                append(old->mBounds[*previous]);
        }
    }
    for (std::size_t j = 0; j < old->mFiles.size(); ++j)
        if (!fileIndex(old->mFiles[j].id))
            append(old->mBounds[j]);
    return boxes;
}
bool TaskProjectionView::matches(std::span<CompositionFile const> other) const {
    if (mFiles.size() != other.size())
        return false;
    for (std::size_t i = 0; i < mFiles.size(); ++i)
        if (!sameTaskPlacement(mFiles[i], other[i]))
            return false;
    return true;
}
} // namespace lholo::structure
