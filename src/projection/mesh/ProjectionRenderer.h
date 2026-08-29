// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Submission of already-built projection meshes. CPU construction, upload and
// lifecycle recovery remain outside this module.

#pragma once

class BaseActorRenderContext;
class BlockPos;
class BlockSource;
class IClientInstance;
class Vec3;

namespace lholo::projection::detail {

struct ProjectionState;

void submitProjectedBlockActorPass(
    ProjectionState&        state,
    BaseActorRenderContext& renderContext,
    BlockSource&            region,
    Vec3 const&             camera,
    bool                    renderAlphaLayer
);

void submitProjectionMeshPass(
    ProjectionState&        state,
    BaseActorRenderContext& renderContext,
    IClientInstance&        client,
    BlockPos const&         renderOrigin,
    Vec3 const&             camera,
    float                   structureOpacity,
    bool                    renderAlphaLayer,
    bool                    structureBoundsEnabled,
    bool                    correctionSeeThrough,
    bool                    missingSeeThrough,
    bool                    projectionSeeThrough
);

} // namespace lholo::projection::detail
