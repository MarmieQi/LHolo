// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Opaque-pass projection build pipeline. Settings reconciliation and placement
// rebuild occur before this boundary; final mesh submission occurs after it.

#pragma once

#include "projection/mesh/ProjectionSectionBuilder.h"
#include "structure/LayerDisplayTypes.h"

class BlockSource;
class LegacyStructureSettings;
class Tessellator;
class Vec3;

namespace lholo::projection::detail {

struct ProjectionState;

void processProjectionOpaqueFrame(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    BlockSource&                          region,
    Vec3 const&                           cameraPosition,
    LegacyStructureSettings const&        transformSettings,
    ProjectionSectionBuildSettings const& buildSettings,
    structure::LayerDisplayMode           layerDisplayMode,
    int                                   displayLayer,
    structure::LayerAxis                  layerAxis
);

} // namespace lholo::projection::detail
