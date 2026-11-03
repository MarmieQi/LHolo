// LHolo - Deterministic back-to-front ordering of transparent faces.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace lholo::projection::detail {

struct ProjectionTransparentFaceKey {
    std::array<float, 3> center{};
    std::uint64_t id{};
    std::array<float, 3> normal{};
    bool singleSided{};
};

// Only a complete unit-square boundary can hide a neighboring cube face.
inline std::optional<std::array<int, 3>> projectionUnitFaceNeighbor(
    std::array<std::array<float, 3>, 4> const& vertices, std::array<float, 3> const& cell
) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        float const plane = vertices[0][axis];
        if (plane != cell[axis] && plane != cell[axis] + 1.0f) continue;
        auto const a = (axis + 1) % 3, b = (axis + 2) % 3;
        unsigned corners{};
        bool valid = true;
        for (auto const& vertex : vertices) {
            if (vertex[axis] != plane
                || (vertex[a] != cell[a] && vertex[a] != cell[a] + 1.0f)
                || (vertex[b] != cell[b] && vertex[b] != cell[b] + 1.0f)) { valid = false; break; }
            auto const corner = (vertex[a] == cell[a] + 1.0f ? 1U : 0U)
                | (vertex[b] == cell[b] + 1.0f ? 2U : 0U);
            corners |= 1U << corner;
        }
        if (valid && corners == 15U) {
            std::array<int, 3> offset{};
            offset[axis] = plane == cell[axis] ? -1 : 1;
            return offset;
        }
    }
    return std::nullopt;
}

inline std::vector<std::uint32_t> projectionTransparentOrder(
    std::span<ProjectionTransparentFaceKey const> faces, std::array<float, 3> const& axis,
    std::optional<std::array<float, 3>> const& eye = std::nullopt
) {
    std::vector<float> depths;
    depths.reserve(faces.size());
    for (auto const& face : faces) {
        auto const& p = face.center;
        float const depth = p[0] * axis[0] + p[1] * axis[1] + p[2] * axis[2];
        depths.push_back(std::isfinite(depth) ? depth : -std::numeric_limits<float>::infinity());
    }
    std::vector<std::uint32_t> order;
    order.reserve(faces.size());
    for (std::uint32_t index = 0; index < faces.size(); ++index) {
        auto const& face = faces[index];
        if (eye && face.singleSided) {
            float const side = face.normal[0] * ((*eye)[0] - face.center[0])
                + face.normal[1] * ((*eye)[1] - face.center[1])
                + face.normal[2] * ((*eye)[2] - face.center[2]);
            if (!(side > 0.0f)) continue;
        }
        order.push_back(index);
    }
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
        if (depths[a] != depths[b]) return depths[a] > depths[b];
        return faces[a].id != faces[b].id ? faces[a].id < faces[b].id : a < b;
    });
    return order;
}

} // namespace lholo::projection::detail
