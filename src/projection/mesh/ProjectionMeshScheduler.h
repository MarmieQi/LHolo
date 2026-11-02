// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Render-thread scheduling for projection section CPU builds. Selection,
// snapshot preparation and synchronous fallback live here; geometry generation
// remains in ProjectionSectionBuilder and GPU upload in ProjectionMeshUpload.

#pragma once

#include "projection/mesh/ProjectionSectionBuilder.h"

class BlockSource;
class BlockTessellator;
class Tessellator;
class Vec3;

namespace lholo::projection::detail {

struct ProjectionState;

bool projectionSectionHasPendingCorrection(ProjectionState const& state, std::size_t section,
                                           ProjectionSectionBuildSettings const& settings);

void scheduleProjectionMeshBuild(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    BlockSource&                          region,
    Vec3 const&                           cameraPosition,
    ProjectionSectionBuildSettings const& settings
);

void buildNextProjectionSectionSynchronously(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    BlockTessellator&                     blockTessellator,
    BlockSource&                          region,
    Vec3 const&                           cameraPosition,
    ProjectionSectionBuildSettings const& settings
);

} // namespace lholo::projection::detail
