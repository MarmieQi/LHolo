// LHolo - Transparent CPU capture and render-thread submission.
#pragma once

#include "projection/core/ProjectionTransparentGeometry.h"

class BaseActorRenderContext;
namespace mce { class MeshData; }

namespace lholo::projection::detail {
struct ProjectionState;

// Capture before Tessellator::end/upload can release CPU fields. Unsupported
// topology returns false so its original native mesh remains available.
bool captureProjectionTransparency(ProjectionTransparentGeometry& output, mce::MeshData const& data);
mce::Mesh* prepareProjectionTransparency(ProjectionState& state, BaseActorRenderContext& context);
} // namespace lholo::projection::detail
