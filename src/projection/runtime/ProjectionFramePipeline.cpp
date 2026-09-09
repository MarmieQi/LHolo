// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/runtime/ProjectionFramePipeline.h"

#include "projection/correction/ProjectionCorrectionTracker.h"
#include "projection/mesh/ProjectionMeshScheduler.h"
#include "projection/mesh/ProjectionMeshUpload.h"
#include "projection/runtime/ProjectionProgress.h"
#include "projection/core/ProjectionState.h"
#include "structure/StructureLoader.h"

#include <memory>

#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

namespace lholo::projection::detail {
namespace {

void ensureStructureBoundsMesh(
    ProjectionState& state,
    Tessellator&     tessellator,
    int              rotationTurns
) {
    // The bounds are only 24 line vertices and are intentionally generated
    // once on the render thread; section BlockTessellator work stays async.
    if (!state.asyncMeshBuildingEnabled || state.structureBoundsMesh) return;

    auto const rotated = rotationTurns == 1 || rotationTurns == 3;
    auto const width = static_cast<float>(
        rotated ? state.structure->sizeZ : state.structure->sizeX
    );
    auto const height = static_cast<float>(state.structure->sizeY);
    auto const depth = static_cast<float>(
        rotated ? state.structure->sizeX : state.structure->sizeZ
    );
    constexpr float expansion = 0.01f;
    float const x0 = -expansion, y0 = -expansion, z0 = -expansion;
    float const x1 = width + expansion, y1 = height + expansion, z1 = depth + expansion;
    tessellator.begin(
        Tessellator::DebugContextCallback{}, mce::PrimitiveMode::LineList, 24, false
    );
    tessellator.colorABGR(static_cast<int>(0xFFFFD633U));
    auto addBoundsEdge = [&](Vec3 const& first, Vec3 const& second) {
        tessellator.vertex(first);
        tessellator.vertex(second);
    };
    addBoundsEdge({x0,y0,z0},{x1,y0,z0}); addBoundsEdge({x1,y0,z0},{x1,y1,z0});
    addBoundsEdge({x1,y1,z0},{x0,y1,z0}); addBoundsEdge({x0,y1,z0},{x0,y0,z0});
    addBoundsEdge({x0,y0,z1},{x1,y0,z1}); addBoundsEdge({x1,y0,z1},{x1,y1,z1});
    addBoundsEdge({x1,y1,z1},{x0,y1,z1}); addBoundsEdge({x0,y1,z1},{x0,y0,z1});
    addBoundsEdge({x0,y0,z0},{x0,y0,z1}); addBoundsEdge({x1,y0,z0},{x1,y0,z1});
    addBoundsEdge({x1,y1,z0},{x1,y1,z1}); addBoundsEdge({x0,y1,z0},{x0,y1,z1});
    state.structureBoundsMesh = std::make_unique<mce::Mesh>(tessellator.end(
        Tessellator::UploadMode::Buffered,
        "LHoloStructureBounds",
        Tessellator::SupplementaryFieldAutoGenerationMode::None
    ));
}

} // namespace

void processProjectionOpaqueFrame(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    BlockSource&                          region,
    Vec3 const&                           cameraPosition,
    LegacyStructureSettings const&        transformSettings,
    ProjectionSectionBuildSettings const& buildSettings,
    int                                   layerDisplayMode,
    int                                   displayLayer,
    int                                   layerAxis
) {
    auto& blockTessellator = *state.blockTessellator;
    blockTessellator.setRegion(region);

    auto const correctionChanges = updateCorrectionTracker(
        state,
        region,
        transformSettings,
        buildSettings.mirrorMode,
        buildSettings.rotationTurns,
        buildSettings.offsetX,
        buildSettings.offsetY,
        buildSettings.offsetZ,
        layerDisplayMode,
        displayLayer,
        layerAxis
    );
    if (correctionChanges.overall) {
        publishPlacedProgress(state.progressCorrectCount);
    }
    if (correctionChanges.visible) {
        publishVisiblePlacedProgress(state.progressVisibleCorrectCount);
    }
    if (correctionChanges.errors) {
        publishErrorProgress(
            state.progressWrongTypeCount,
            state.progressWrongStateCount,
            state.progressExtraCount
        );
    }

    if (tessellator.isTessellating()) tessellator.cancel();

    // Consume completed CPU data only in the opaque render pass.
    uploadCompletedProjectionMeshes(state, tessellator);
    ensureStructureBoundsMesh(state, tessellator, buildSettings.rotationTurns);
    scheduleProjectionMeshBuild(
        state, tessellator, region, cameraPosition, buildSettings
    );
    buildNextProjectionSectionSynchronously(
        state, tessellator, blockTessellator, region, buildSettings
    );
}

} // namespace lholo::projection::detail
