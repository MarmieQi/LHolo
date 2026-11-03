// LHolo - Adapts active mesh storage to the resource-free CPU draw cache.
#pragma once
class Vec3;
class BlockPos;
namespace lholo::projection::detail {
struct ProjectionState;
void prepareProjectionDrawLists(ProjectionState&, Vec3 const& camera, BlockPos const& origin);
} // namespace lholo::projection::detail
