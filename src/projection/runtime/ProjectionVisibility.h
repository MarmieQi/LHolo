// LHolo - Camera adaptation; pure visibility rules stay in core/.
#pragma once
class BaseActorRenderContext;
class Vec3;
namespace lholo::projection::detail {
struct ProjectionState;
void updateProjectionVisibility(ProjectionState &, BaseActorRenderContext &, Vec3 const &camera, float renderDistance);
} // namespace lholo::projection::detail
