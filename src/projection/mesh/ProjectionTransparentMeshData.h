// LHolo - Batch packing of the already ordered transparent vertex stream.
#pragma once

#include "projection/core/ProjectionTransparentGeometry.h"
#include <glm/glm.hpp>
#include <span>
#include <utility>

namespace lholo::projection::detail {
struct ProjectionTransparentMeshBounds {
    std::pair<glm::vec3, glm::vec3> positions;
    std::pair<glm::vec2, glm::vec2> uv;
};

// The caller supplies the existing face order and owns native format/count
// initialization. This operation only fills CPU streams and their exact bounds.
ProjectionTransparentMeshBounds fillProjectionTransparentVertices(
    std::span<ProjectionTransparentQuad const* const> faces, std::span<std::uint32_t const> order,
    std::vector<glm::vec3>& positions, std::vector<glm::vec4>& normals,
    std::vector<std::uint32_t>& colors, std::vector<glm::vec2>& uv);
} // namespace lholo::projection::detail
