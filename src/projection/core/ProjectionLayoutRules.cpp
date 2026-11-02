// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Minecraft-free projection layout rules. This translation unit only depends
// on header-only Minecraft enums and pure LHolo data types, so the logic
// tests can link it without the game runtime.

#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionVisibilityRules.h"
#include "structure/StructureTransform.h"

namespace lholo::projection::detail {

ProjectionBounds projectionSectionBounds(
    SubChunkKey const& key, structure::LoadedStructure const& loaded, int mirror, int rotation
) {
    auto const [sx, sy, sz] = key;
    ProjectionBounds bounds;
    for (int dx : {0, 15}) for (int dy : {0, 15}) for (int dz : {0, 15}) {
        auto const p = transformStructurePosition(
            BlockPos{sx * 16 + dx, sy * 16 + dy, sz * 16 + dz}, loaded, mirror, rotation
        );
        bounds.include({static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
    }
    bounds.maximum = {bounds.maximum.x + 1, bounds.maximum.y + 1, bounds.maximum.z + 1};
    return bounds;
}


RenderBucket renderBucketFor(BlockRenderLayer layer) {
    switch (layer) {
    case BlockRenderLayer::RenderlayerBlend:
    case BlockRenderLayer::RenderlayerBlendToOpaque:
        return RenderBucket::Blend;
    case BlockRenderLayer::RenderlayerOpaque:
    case BlockRenderLayer::RenderlayerSeasonsOpaque:
    case BlockRenderLayer::RenderlayerShiftOpaqueInternalOnly:
        return RenderBucket::Opaque;
    case BlockRenderLayer::RenderlayerAlphatestSingleSide:
    case BlockRenderLayer::RenderlayerAlphatestSingleSideToOpaque:
    case BlockRenderLayer::RenderlayerShiftAlphatestSingleSideInternalOnly:
    case BlockRenderLayer::RenderlayerShiftAlphatestSingleSideToOpaqueInternalOnly:
        return RenderBucket::AlphaOneSided;
    default:
        return RenderBucket::Alpha;
    }
}

Mirror getProjectionMirror(int mirrorMode) {
    switch (mirrorMode) {
    // LHolo's UI names the coordinate being flipped. Bedrock names Mirror by
    // the axis kept fixed: Mirror::Z flips X, while Mirror::X flips Z.
    case 1: return Mirror::Z;
    case 2: return Mirror::X;
    default: return Mirror::None;
    }
}

Rotation getProjectionRotation(int quarterTurns) {
    switch (quarterTurns & 3) {
    case 1: return Rotation::Clockwise90;
    case 2: return Rotation::Clockwise180;
    case 3: return Rotation::CounterClockwise90;
    default: return Rotation::None;
    }
}

BlockPos transformStructurePosition(
    structure::LoadedStructure::RenderBlock const& entry,
    structure::LoadedStructure const&              loaded,
    int                                             mirrorMode,
    int                                             rotation
) {
    return transformStructurePosition(
        BlockPos{entry.x, entry.y, entry.z}, loaded, mirrorMode, rotation
    );
}

BlockPos transformStructurePosition(
    BlockPos const&                   position,
    structure::LoadedStructure const& loaded,
    int                               mirrorMode,
    int                               rotation
) {
    auto p = structure::transformCell(position.x, position.y, position.z, loaded.sizeX, loaded.sizeZ, mirrorMode, rotation);
    return BlockPos{p[0], p[1], p[2]};
}

BlockPos inverseTransformStructurePosition(
    BlockPos const&                   position,
    structure::LoadedStructure const& loaded,
    int                               mirrorMode,
    int                               rotation
) {
    int x{};
    int z{};
    switch (rotation & 3) {
    case 1:
        x = position.z;
        z = loaded.sizeZ - 1 - position.x;
        break;
    case 2:
        x = loaded.sizeX - 1 - position.x;
        z = loaded.sizeZ - 1 - position.z;
        break;
    case 3:
        x = loaded.sizeX - 1 - position.z;
        z = position.x;
        break;
    default:
        x = position.x;
        z = position.z;
        break;
    }
    if (mirrorMode == 1) x = loaded.sizeX - 1 - x;
    if (mirrorMode == 2) z = loaded.sizeZ - 1 - z;
    return BlockPos{x, position.y, z};
}

std::optional<ProjectionCellBox> projectionSectionLocalIntersection(
    std::array<int, 3> const& section, structure::LoadedStructure const& loaded,
    BlockPos const& origin, int mirror, int rotation
) {
    bool rotated = (rotation & 1) != 0;
    std::array<int, 3> size{rotated ? loaded.sizeZ : loaded.sizeX, loaded.sizeY, rotated ? loaded.sizeX : loaded.sizeZ};
    std::array<long long, 3> offset{origin.x, origin.y, origin.z};
    std::array<int, 3> lo{}, hi{};
    for (int axis = 0; axis < 3; ++axis) {
        auto base = static_cast<long long>(section[axis]) * 16;
        auto minimum = std::max(base, offset[axis]);
        auto maximum = std::min(base + 15, offset[axis] + size[axis] - 1);
        if (minimum > maximum) return std::nullopt;
        lo[axis] = static_cast<int>(minimum - offset[axis]);
        hi[axis] = static_cast<int>(maximum - offset[axis]);
    }
    auto a = inverseTransformStructurePosition(BlockPos{lo[0], lo[1], lo[2]}, loaded, mirror, rotation);
    auto b = inverseTransformStructurePosition(BlockPos{hi[0], hi[1], hi[2]}, loaded, mirror, rotation);
    return ProjectionCellBox{{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)},
        {std::max(a.x, b.x) + 1, std::max(a.y, b.y) + 1, std::max(a.z, b.z) + 1}};
}

bool isStructureCellCovered(
    structure::LoadedStructure const& loaded,
    BlockPos const&                    position
) {
    for (auto const& region : loaded.regions) {
        if (position.x >= region.x && position.x < region.x + region.sizeX
            && position.y >= region.y && position.y < region.y + region.sizeY
            && position.z >= region.z && position.z < region.z + region.sizeZ) {
            return true;
        }
    }
    return false;
}

bool isLayerVisible(
    int layer,
    structure::LayerDisplayMode layerDisplayMode,
    int displayLayer,
    int materialIndex,
    int secondaryMaterialIndex,
    structure::LayerAxis layerAxis
) {
    if (layerAxis == structure::LayerAxis::Material) {
        // Full material view is still a full projection view. Extra world
        // blocks have no schematic material index, but correction must not
        // disappear merely because the UI grouping is set to materials.
        if (layerDisplayMode == structure::LayerDisplayMode::All) return true;
        auto const materialVisible = [&](int index) {
            if (index < 0) return false;
            switch (layerDisplayMode) {
            case structure::LayerDisplayMode::Single: return index == displayLayer;
            case structure::LayerDisplayMode::UpToCurrent: return index <= displayLayer;
            case structure::LayerDisplayMode::FromCurrent: return index >= displayLayer;
            default: return true;
            }
        };
        return materialVisible(materialIndex) || materialVisible(secondaryMaterialIndex);
    }
    switch (layerDisplayMode) {
    case structure::LayerDisplayMode::Single: return layer == displayLayer;
    case structure::LayerDisplayMode::UpToCurrent: return layer <= displayLayer;
    case structure::LayerDisplayMode::FromCurrent: return layer >= displayLayer;
    default: return true;
    }
}

} // namespace lholo::projection::detail
