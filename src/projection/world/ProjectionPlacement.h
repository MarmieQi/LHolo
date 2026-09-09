// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Rebuilds the immutable projected-world view after placement, transform, or
// layer changes. Rendering and worker scheduling remain outside this module.

#pragma once

#include "structure/LayerDisplayTypes.h"

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
};

void rebuildProjectionPlacement(
    ProjectionState&                   state,
    BlockSource&                       region,
    BlockActorRenderDispatcher&        dispatcher,
    LegacyStructureSettings const&     transformSettings,
    ProjectionPlacementSettings const& settings
);

} // namespace lholo::projection::detail
