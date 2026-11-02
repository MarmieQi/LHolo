// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Opaque-pass projection build pipeline. Settings reconciliation and placement
// rebuild occur before this boundary; final mesh submission occurs after it.

#pragma once

#include "projection/mesh/ProjectionSectionBuilder.h"
#include "structure/LayerDisplayTypes.h"
#include <span>
#include <memory>

class BlockSource;
class BaseActorRenderContext;
class LegacyStructureSettings;
class Tessellator;
class Vec3;

namespace lholo::projection::detail {

struct ProjectionState;

struct ProjectionFrameTarget {
    ProjectionState* state{};
    ProjectionSectionBuildSettings build;
    structure::LayerDisplayMode layerDisplayMode{};
    int displayLayer{};
    structure::LayerAxis layerAxis{};
    std::unique_ptr<LegacyStructureSettings> transform;
};

// Targets have already reconciled placement. World events, correction work,
// uploads and mesh dispatch are coordinated once across all active files.
void processProjectionOpaqueFrames(ProjectionState& root, std::span<ProjectionFrameTarget const> targets,
    BaseActorRenderContext& renderContext, Vec3 const& cameraPosition);

} // namespace lholo::projection::detail
