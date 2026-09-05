// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Pure projection rules. Functions in this module do not read projection
// runtime state and do not own Minecraft or rendering resources.

#pragma once

#include <string_view>

#include "projection/core/ProjectionInternalTypes.h"
#include "structure/StructureLoader.h"

#include "mc/util/Mirror.h"
#include "mc/util/Rotation.h"
#include "mc/world/level/block/BlockRenderLayer.h"

class Block;
class LegacyStructureSettings;

namespace lholo::projection::detail {

RenderBucket renderBucketFor(BlockRenderLayer layer);

Mirror   getProjectionMirror(int mirrorMode);
Rotation getProjectionRotation(int quarterTurns);

Block const* transformExpectedBlock(
    Block const*                   block,
    LegacyStructureSettings const& settings,
    bool                           identityTransform
);

bool projectionStatesMatch(Block const& expected, Block const& actual);

[[nodiscard]] constexpr bool isVanillaSaplingType(std::string_view blockTypeName) {
    return blockTypeName.starts_with("minecraft:") && blockTypeName.ends_with("_sapling");
}

int  blockFrontFace(Block const& block);

BlockPos transformStructurePosition(
    structure::LoadedStructure::RenderBlock const& entry,
    structure::LoadedStructure const&              loaded,
    int                                             mirrorMode,
    int                                             rotation
);

BlockPos transformStructurePosition(
    BlockPos const&                           position,
    structure::LoadedStructure const&         loaded,
    int                                       mirrorMode,
    int                                       rotation
);

BlockPos inverseTransformStructurePosition(
    BlockPos const&                           position,
    structure::LoadedStructure const&         loaded,
    int                                       mirrorMode,
    int                                       rotation
);

bool isStructureCellCovered(
    structure::LoadedStructure const& loaded,
    BlockPos const&                    position
);

bool isLayerVisible(
    int layer,
    int layerDisplayMode,
    int displayLayer,
    int materialIndex = -1,
    int secondaryMaterialIndex = -1,
    int layerAxis = 0
);

} // namespace lholo::projection::detail
