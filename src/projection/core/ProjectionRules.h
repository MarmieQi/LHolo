// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Pure projection rules. Functions in this module do not read projection
// runtime state and do not own Minecraft or rendering resources.

#pragma once

#include "structure/LayerDisplayTypes.h"

#include <string_view>
#include <optional>

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionWorldBounds.h"
#include "structure/StructureLoader.h"

#include "mc/util/Mirror.h"
#include "mc/util/Rotation.h"
#include "mc/world/level/block/BlockRenderLayer.h"

class Block;
class BlockSource;
class IConstBlockSource;
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

// Recomputes neighbor-owned states using native rules: WallBlock helpers for
// legacy wall arms/posts, and connectionUpdate for blocks with the native
// BlockConnectionComponent (including stair corners and fence/pane arms).
// Returns the immutable palette block normalized for the neighborhood
// the caller sees at `position` (the projected virtual
// world while a tessellation scope is active, the real world otherwise), or
// `block` itself when it is not a connection block. The vanilla update also
// applies its result to the region, so the call runs with region writes
// suppressed (see ScopedRegionWriteSuppression) and only its return value
// is used.
Block const& withNeighborConnections(
    Block const&    block,
    BlockSource&    region,
    BlockPos const& position
);

bool projectionStatesMatch(Block const& expected, Block const& actual);
// World-aware correction additionally accepts equivalent native corner-stair
// shapes while retaining all states other than the two corner representations.
bool projectionStatesMatch(
    Block const& expected, Block const& actual, IConstBlockSource const& region, BlockPos const& position
);

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

struct ProjectionCellBox { std::array<int, 3> minimum{}, maximum{}; };
// Exact integer bounds for event/mesh coordination, including an optional
// local halo. Float mesh bounds lose precision near world coordinate limits.
ProjectionWorldBounds projectionSectionCellWorldBounds(SubChunkKey const& key,
    structure::LoadedStructure const& loaded, std::array<long long, 3> const& origin,
    int mirror, int rotation, int halo = 0);
// Model queries extend past the file boundary. Clip the Section's own cells
// first, then expand in WORLD space so neighboring files remain in scope.
ProjectionWorldBounds projectionSectionQueryWorldBounds(SubChunkKey const& key,
    structure::LoadedStructure const& loaded, std::array<long long, 3> const& origin,
    int mirror, int rotation, int halo);
// World subchunk intersected with the transformed structure, returned in local
// cells (maximum exclusive). A local 16^3 tile need not align to the world grid.
std::optional<ProjectionCellBox> projectionSectionLocalIntersection(
    std::array<int, 3> const& section, structure::LoadedStructure const& loaded,
    BlockPos const& origin, int mirror, int rotation, int padding = 0);

bool isLayerVisible(
    int layer,
    structure::LayerDisplayMode layerDisplayMode,
    int displayLayer,
    int materialIndex = -1,
    int secondaryMaterialIndex = -1,
    structure::LayerAxis layerAxis = structure::LayerAxis::Y
);

} // namespace lholo::projection::detail
