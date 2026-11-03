#pragma once

#include "structure/StructureLoader.h"
#include "structure/StructureTransform.h"
#include "structure/LayerSettings.h"
#include "projection/core/ProjectionRules.h"

#include <array>
#include <optional>
#include <tuple>
#include <unordered_map>

namespace lholo::projection::detail {

// One immutable placement view, shared by the active projection and workers.
// Coordinates are inverted on demand; moving a structure never duplicates a
// hash node for each of its cells. Native state transforms are resolved once
// per distinct Block on the game thread, never inside worker queries.
class ProjectionWorldLookup {
public:
    using Key = std::tuple<int, int, int>;
    using BlockTransforms = std::unordered_map<Block const*, Block const*>;
    ProjectionWorldLookup() = default;
    ProjectionWorldLookup(std::shared_ptr<structure::LoadedStructure const> source,
                          std::array<long long, 3> origin, int mirror, int rotation,
                          structure::LayerSettings layers,
                          BlockTransforms blocks)
        : mSource(std::move(source)), mOrigin(origin), mMirror(mirror), mRotation(rotation),
          mLayers(layers), mBlocks(std::move(blocks)) {}

    std::optional<std::size_t> indexAt(Key const& key) const {
        if (!mSource) return {};
        auto [x, y, z] = key;
        std::array<long long, 3> p{x - mOrigin[0], y - mOrigin[1], z - mOrigin[2]};
        bool const rotated = (mRotation & 1) != 0;
        if (p[0] < 0 || p[1] < 0 || p[2] < 0 ||
            p[0] >= (rotated ? mSource->sizeZ : mSource->sizeX) || p[1] >= mSource->sizeY ||
            p[2] >= (rotated ? mSource->sizeX : mSource->sizeZ)) return {};
        auto local = structure::inverseTransformCell(static_cast<int>(p[0]), static_cast<int>(p[1]),
            static_cast<int>(p[2]), mSource->sizeX, mSource->sizeZ, mMirror, mRotation);
        auto index = mSource->renderBlocks.find(local[0], local[1], local[2]);
        if (!index) return {};
        auto cell = mSource->renderBlocks[*index];
        return isLayerVisible(mLayers.axis == structure::LayerAxis::X ? cell.x : cell.y,
            mLayers.mode, mLayers.layer, cell.materialIndex, cell.liquidMaterialIndex, mLayers.axis)
            ? index : std::nullopt;
    }
    Block const* blockAt(Key const& key) const {
        auto index = indexAt(key);
        if (!index) return nullptr;
        auto cell = mSource->renderBlocks[*index];
        auto value = cell.block ? cell.block : cell.liquid;
        // Placement resolves every source palette state before publication.
        // A missing transform is an invariant failure, never schematic air.
        return mBlocks.at(value);
    }
private:
    std::shared_ptr<structure::LoadedStructure const> mSource;
    std::array<long long, 3> mOrigin{};
    int mMirror{}, mRotation{};
    structure::LayerSettings mLayers;
    BlockTransforms mBlocks;
};
} // namespace lholo::projection::detail
