// Construction-time support queries and conservative native bottom filtering.
#pragma once

#include "projection/core/ProjectionTransparentGeometry.h"
#include <span>

class BlockPos;
class BlockSource;
namespace mce { class MeshData; }

namespace lholo::projection::detail {
struct ProjectionState;

struct ProjectionBottomRange {
    std::size_t first{}, last{};
    std::array<float, 3> cell{};
};

bool projectionFaceCovered(ProjectionState const& state, BlockSource& region,
    BlockPos const& neighbor, bool& worldDependent);
bool hasProjectionBottomFace(mce::MeshData const& data, std::size_t first,
    std::array<float, 3> const& cell);
void hideProjectionBottomFaces(ProjectionTransparentGeometry& geometry,
    std::span<ProjectionBottomRange const> ranges);
// Only unindexed quad streams with consistent attributes are modified.
void cullProjectionBottomFaces(mce::MeshData& data, std::span<ProjectionBottomRange const> ranges);
} // namespace lholo::projection::detail
