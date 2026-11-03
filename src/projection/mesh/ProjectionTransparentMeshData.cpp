#include "projection/mesh/ProjectionTransparentMeshData.h"
#include <limits>

namespace lholo::projection::detail {
ProjectionTransparentMeshBounds fillProjectionTransparentVertices(
    std::span<ProjectionTransparentQuad const* const> faces, std::span<std::uint32_t const> order,
    std::vector<glm::vec3>& positions, std::vector<glm::vec4>& normals,
    std::vector<std::uint32_t>& colors, std::vector<glm::vec2>& uv) {
    auto const count = order.size() * 4;
    positions.resize(count);
    normals.resize(count);
    colors.resize(count);
    uv.resize(count);
    constexpr auto maximum = std::numeric_limits<float>::max();
    constexpr auto minimum = std::numeric_limits<float>::lowest();
    ProjectionTransparentMeshBounds bounds{{glm::vec3{maximum}, glm::vec3{minimum}},
                                           {glm::vec2{maximum}, glm::vec2{minimum}}};
    std::size_t output{};
    for (auto index : order) {
        for (auto const& vertex : faces[index]->vertices) {
            glm::vec3 const position{vertex.position[0], vertex.position[1], vertex.position[2]};
            glm::vec2 const texture{vertex.uv[0], vertex.uv[1]};
            positions[output] = position;
            normals[output] = {vertex.normal[0], vertex.normal[1], vertex.normal[2], 0.0f};
            colors[output] = vertex.color;
            uv[output] = texture;
            bounds.positions.first = glm::min(bounds.positions.first, position);
            bounds.positions.second = glm::max(bounds.positions.second, position);
            bounds.uv.first = glm::min(bounds.uv.first, texture);
            bounds.uv.second = glm::max(bounds.uv.second, texture);
            ++output;
        }
    }
    return bounds;
}
} // namespace lholo::projection::detail
