// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Submission of the correction overlay through the immediate mesh path.
// CPU construction and upload stay in the shared section pipeline; this
// module only draws the uploaded per-color batches.

#pragma once

class BaseActorRenderContext;
class IClientInstance;

namespace lholo::projection::detail {

struct ProjectionState;

// Draws every uploaded per-color correction section mesh through the vanilla
// immediate path, coloring each batch through the render context's shared
// shader color (the same mechanism the vanilla block outline uses). Must run
// on the render thread inside the frame-building window, after the vanilla
// $renderBlockEntities call, in the alpha layer pass.
void submitCorrectionOverlayPass(
    ProjectionState&        state,
    BaseActorRenderContext& renderContext,
    IClientInstance&        client
);

} // namespace lholo::projection::detail
