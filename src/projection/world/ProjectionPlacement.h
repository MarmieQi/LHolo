// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Rebuilds the immutable projected-world view after placement, transform, or
// layer changes. Rendering and worker scheduling remain outside this module.

#pragma once

#include "structure/LayerDisplayTypes.h"
#include <cstddef>
#include <span>
#include <unordered_set>

class BlockActor;
class BlockActorRenderDispatcher;
class BlockSource;
class LegacyStructureSettings;

namespace lholo::projection::detail {

struct ProjectionState;

struct ProjectionPlacementSettings {
    int  mirrorMode{};
    int  rotationTurns{};
    int  offsetX{};
    int  offsetY{};
    int  offsetZ{};
    structure::LayerDisplayMode layerDisplayMode{structure::LayerDisplayMode::All};
    int  displayLayer{};
    structure::LayerAxis layerAxis{structure::LayerAxis::Y};
    bool identityTransform{};
    bool resetCorrectionScan{};
};

void pairProjectedChests(BlockSource &, ProjectionState &,
                         std::unordered_set<BlockActor const *> const *created = nullptr);
void rebuildProjectedBlockActors(ProjectionState &, BlockActorRenderDispatcher &, LegacyStructureSettings const &,
                                 ProjectionPlacementSettings const &, std::span<std::size_t const>, bool replaceAll);

void rebuildProjectionPlacement(
    ProjectionState&                   state,
    BlockSource&                       region,
    BlockActorRenderDispatcher&        dispatcher,
    LegacyStructureSettings const&     transformSettings,
    ProjectionPlacementSettings const& settings
);

} // namespace lholo::projection::detail
