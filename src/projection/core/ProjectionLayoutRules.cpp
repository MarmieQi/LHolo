// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Minecraft-free projection layout rules. This translation unit only depends
// on header-only Minecraft enums and pure LHolo data types, so the logic
// tests can link it without the game runtime.

#include "projection/core/ProjectionRules.h"

namespace lholo::projection::detail {

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
    int x = position.x;
    int z = position.z;
    if (mirrorMode == 1) x = loaded.sizeX - 1 - x;
    if (mirrorMode == 2) z = loaded.sizeZ - 1 - z;
    switch (rotation) {
    case 1: return BlockPos{loaded.sizeZ - 1 - z, position.y, x};
    case 2: return BlockPos{loaded.sizeX - 1 - x, position.y, loaded.sizeZ - 1 - z};
    case 3: return BlockPos{z, position.y, loaded.sizeX - 1 - x};
    default: return BlockPos{x, position.y, z};
    }
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
    int layerDisplayMode,
    int displayLayer,
    int materialIndex,
    int secondaryMaterialIndex,
    int layerAxis
) {
    if (layerAxis == 2) {
        auto const materialVisible = [&](int index) {
            if (index < 0) return false;
            switch (layerDisplayMode) {
            case 1: return index == displayLayer;
            case 2: return index <= displayLayer;
            case 3: return index >= displayLayer;
            default: return true;
            }
        };
        return materialVisible(materialIndex) || materialVisible(secondaryMaterialIndex);
    }
    switch (layerDisplayMode) {
    case 1: return layer == displayLayer;
    case 2: return layer <= displayLayer;
    case 3: return layer >= displayLayer;
    default: return true;
    }
}

} // namespace lholo::projection::detail
