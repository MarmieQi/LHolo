#pragma once

#include "projection/core/ProjectionRules.h"
#include "structure/LayerVisibility.h"
#include "structure/TaskProjectionView.h"

#include <array>
#include <optional>
#include <tuple>
#include <unordered_map>

namespace lholo::projection::detail {
class ProjectionWorldLookup;
// The entries are local immutable views, never other task views (no cycles).
struct TaskWorldLookups {
    std::shared_ptr<structure::TaskProjectionView const> placements;
    std::vector<std::shared_ptr<ProjectionWorldLookup const>> files;
    std::vector<std::shared_ptr<ExpectedBlockActorMap const>> actors;
};

class ProjectionWorldLookup {
public:
    using Key = std::tuple<int, int, int>;
    using BlockTransforms = std::unordered_map<Block const*, Block const*>;
    ProjectionWorldLookup() = default;
    ProjectionWorldLookup(std::shared_ptr<structure::LoadedStructure const> source, std::array<long long, 3> origin,
                          int mirror, int rotation, structure::LayerSettings layers, BlockTransforms blocks)
        : mSource(std::move(source)), mOrigin(origin), mMirror(mirror), mRotation(rotation), mLayers(layers),
          mBlocks(std::make_shared<BlockTransforms const>(std::move(blocks))) {}

    bool hasTransform(int mirror, int rotation) const { return mSource && mMirror == mirror && mRotation == rotation; }
    std::shared_ptr<ProjectionWorldLookup const> relocated(std::array<long long, 3> origin,
                                                           structure::LayerSettings layers) const {
        auto result = std::make_shared<ProjectionWorldLookup>(*this);
        result->mOrigin = origin;
        result->mLayers = layers;
        return result;
    }
    std::shared_ptr<ProjectionWorldLookup const> withTask(std::shared_ptr<TaskWorldLookups const> task,
                                                          std::size_t file) const {
        auto result = std::make_shared<ProjectionWorldLookup>(*this);
        result->mTask = std::move(task);
        result->mFile = file;
        return result;
    }
    std::shared_ptr<ProjectionWorldLookup const> forSection(structure::TaskCellBounds const &world) const {
        auto result = std::make_shared<ProjectionWorldLookup>(*this);
        if (mTask)
            result->mCandidates = mTask->placements->sectionCandidates(mFile, world);
        return result;
    }
    std::span<std::size_t const> candidateFiles() const {
        if (!mTask)
            return {};
        return mCandidates ? std::span<std::size_t const>{*mCandidates} : mTask->placements->neighbors(mFile);
    }
    auto const &task() const { return mTask; }
    std::size_t fileIndex() const { return mFile; }
    std::optional<structure::TaskCellOwner> ownerAt(Key const &key) const {
        auto [x, y, z] = key;
        if (mTask)
            return mTask->placements->ownerAt(candidateFiles(), {x, y, z});
        auto index = localIndexAt(key);
        return index ? std::optional{structure::TaskCellOwner{0, *index}} : std::nullopt;
    }
    std::optional<std::array<int, 3>> localCellAt(Key const &key) const {
        if (!mSource) return {};
        auto [x, y, z] = key;
        std::array<long long, 3> p{x - mOrigin[0], y - mOrigin[1], z - mOrigin[2]};
        bool const rotated = (mRotation & 1) != 0;
        if (p[0] < 0 || p[1] < 0 || p[2] < 0 ||
            p[0] >= (rotated ? mSource->sizeZ : mSource->sizeX) || p[1] >= mSource->sizeY ||
            p[2] >= (rotated ? mSource->sizeX : mSource->sizeZ)) return {};
        return structure::inverseTransformCell(static_cast<int>(p[0]), static_cast<int>(p[1]), static_cast<int>(p[2]),
                                               mSource->sizeX, mSource->sizeZ, mMirror, mRotation);
    }
    // Local ordinals can never name a foreign file's cell.
    std::optional<std::size_t> indexAt(Key const &key) const {
        auto owner = ownerAt(key);
        return owner && owner->file == mFile ? std::optional{owner->index} : std::nullopt;
    }
    bool owns(std::size_t index) const {
        if (mTask)
            return mTask->placements->owns(mFile, index, candidateFiles());
        auto cell = mSource->renderBlocks[index];
        return structure::layerCellVisible(mLayers, cell.x, cell.y, cell.materialIndex, cell.liquidMaterialIndex);
    }
    std::optional<std::size_t> airOwnerAt(Key const &key) const {
        auto [x, y, z] = key;
        return mTask->placements->airOwnerAt(candidateFiles(), {x, y, z});
    }
    bool ownsAir(Key const &key) const {
        if (!mTask) {
            if (mSource->omittedCells.empty()) return true;
            auto local = localCellAt(key);
            return local && !mSource->isOmittedCell((*local)[0], (*local)[1], (*local)[2]);
        }
        auto owner = airOwnerAt(key);
        return owner && *owner == mFile;
    }
    // Already transformed native states, including foreign liquid/body pairs.
    std::optional<structure::StructureBlock> cellAt(Key const &key) const {
        auto owner = ownerAt(key);
        if (!owner)
            return {};
        return mTask ? mTask->files[owner->file]->transformedCell(owner->index) : transformedCell(owner->index);
    }
    Block const *blockAt(Key const &key) const {
        auto cell = cellAt(key);
        return cell ? (cell->block ? cell->block : cell->liquid) : nullptr;
    }
    BlockActor const *taskActorAt(Key const &key) const {
        if (!mTask)
            return nullptr;
        auto owner = ownerAt(key);
        if (!owner)
            return nullptr;
        auto const &actors = *mTask->actors[owner->file];
        auto found = actors.find(key);
        return found == actors.end() ? nullptr : found->second.get();
    }

  private:
    structure::StructureBlock transformedCell(std::size_t index) const {
        auto cell = mSource->renderBlocks[index];
        if (cell.block)
            cell.block = mBlocks->at(cell.block);
        if (cell.liquid)
            cell.liquid = mBlocks->at(cell.liquid);
        return cell;
    }
    std::optional<std::size_t> localIndexAt(Key const &key) const {
        auto position = localCellAt(key);
        if (!position)
            return {};
        auto const &local = *position;
        auto index = mSource->renderBlocks.find(local[0], local[1], local[2]);
        if (!index) return {};
        auto cell = mSource->renderBlocks[*index];
        return structure::layerCellVisible(mLayers, cell.x, cell.y, cell.materialIndex, cell.liquidMaterialIndex)
                   ? index
                   : std::nullopt;
    }
    std::shared_ptr<structure::LoadedStructure const> mSource;
    std::array<long long, 3> mOrigin{};
    int mMirror{}, mRotation{};
    structure::LayerSettings mLayers;
    std::shared_ptr<BlockTransforms const> mBlocks;
    std::shared_ptr<TaskWorldLookups const> mTask;
    std::size_t mFile{};
    std::optional<std::vector<std::size_t>> mCandidates;
};
} // namespace lholo::projection::detail
