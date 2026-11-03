// LHolo - Retained transparent geometry and its render-thread submission cache.
#pragma once

#include "projection/core/ProjectionTransparentOrder.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"

#include <array>
#include <memory>
#include <vector>

namespace lholo::projection::detail {

struct ProjectionTransparentVertex {
    std::array<float, 3> position{}, normal{};
    std::array<float, 2> uv{};
    std::uint32_t color{0xFFFFFFFFU};
};
struct ProjectionTransparentQuad {
    std::array<ProjectionTransparentVertex, 4> vertices;
    std::array<float, 3> center{};
    // Nonzero for faces with explicit single-sided visibility: complete glass
    // cube faces and liquid bottoms. Independent of shader lighting normals.
    std::array<float, 3> facingNormal{};
    bool hidden{};
};
struct ProjectionTransparentGeometry {
    std::vector<ProjectionTransparentQuad> quads;
};
struct ProjectionTransparentSection {
    ProjectionTransparentGeometry terrain, liquid;
};

struct ProjectionTransparentCache {
    // These owners keep every cached face pointer alive across worker uploads.
    std::vector<std::shared_ptr<ProjectionTransparentSection const>> sources;
    std::vector<std::size_t> sections, candidateSections;
    std::vector<ProjectionTransparentQuad const*> faces;
    std::vector<ProjectionTransparentFaceKey> keys;
    std::vector<std::uint32_t> order;
    std::vector<std::uint32_t> singleSidedFaces;
    std::array<float, 3> axis{};
    std::array<float, 3> eye{};
    bool hasAxis{};
    std::unique_ptr<mce::Mesh> mesh;
};

} // namespace lholo::projection::detail
