#pragma once
namespace lholo::projection::detail {
struct ProjectionState;
// Called once per opaque update after visibility and completed-result routing.
void trimProjectionMeshResidency(ProjectionState& root);
} // namespace lholo::projection::detail
