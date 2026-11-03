#pragma once
#include <cstddef>
class Vec3;
namespace lholo::projection::detail {
struct ProjectionState;
void recordProjectionResidentBytes(ProjectionState&, std::size_t section);
bool canBuildResidentSection(ProjectionState const&, std::size_t section, Vec3 const& camera);
void trimProjectionMeshResidency(ProjectionState& root, Vec3 const& camera);
} // namespace lholo::projection::detail
