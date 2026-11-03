#include "projection/mesh/ProjectionTransparency.h"
#include "projection/core/ProjectionState.h"

#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/deps/renderer/Camera.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lholo::projection::detail {

bool captureProjectionTransparency(ProjectionTransparentGeometry& output, mce::MeshData const& data) {
    output.quads.clear();
    auto const& positions = data.mPositions.get();
    auto const& colors = data.mColors.get();
    auto const& normals = data.mNormals.get();
    auto const& uv = data.mTextureUVs[0].get();
    if (data.mMode != mce::PrimitiveMode::QuadList || !data.mIndices.get().empty()
        || positions.size() % 4 != 0 || uv.size() != positions.size()
        || (!colors.empty() && colors.size() != positions.size())
        || (!normals.empty() && normals.size() != positions.size())) return false;
    for (auto const& p : positions)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    output.quads.reserve(positions.size() / 4);
    for (std::size_t first = 0; first < positions.size(); first += 4) {
        ProjectionTransparentQuad quad;
        auto normal = glm::cross(positions[first + 1] - positions[first], positions[first + 2] - positions[first]);
        float const length = glm::length(normal);
        if (length > 0.0f) normal /= length;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            auto const index = first + corner;
            auto const& p = positions[index];
            auto const n = normals.empty() ? normal : glm::vec3{normals[index]};
            quad.vertices[corner] = {{p.x, p.y, p.z}, {n.x, n.y, n.z},
                {uv[index].x, uv[index].y}, colors.empty() ? 0xFFFFFFFFU : colors[index]};
            for (std::size_t axis = 0; axis < 3; ++axis)
                quad.center[axis] += quad.vertices[corner].position[axis] * 0.25f;
        }
        output.quads.push_back(quad);
    }
    return true;
}

mce::Mesh* prepareProjectionTransparency(ProjectionState& state, BaseActorRenderContext& context) {
    auto& cache = state.transparentCache;
    auto& candidates = cache.candidateSections;
    candidates.clear();
    for (auto const& draw : state.drawCache.transparentTerrain) candidates.push_back(draw.section);
    for (auto section : state.drawCache.lists[static_cast<std::size_t>(ProjectionDrawSlot::Liquid)])
        candidates.push_back(section);
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](auto section) {
        return section >= state.transparentSections.size() || !state.transparentSections[section];
    }), candidates.end());
    bool changed = candidates != cache.sections;
    if (!changed) {
        for (std::size_t i = 0; i < candidates.size(); ++i)
            changed = changed || cache.sources[i] != state.transparentSections[candidates[i]];
    }
    if (changed) {
        cache.faces.clear();
        cache.keys.clear();
        cache.sources.clear();
        cache.singleSidedFaces.clear();
        cache.sections = candidates;
        for (auto section : candidates) {
            auto source = state.transparentSections[section];
            auto append = [&](ProjectionTransparentGeometry const& geometry, bool liquid) {
                for (std::size_t i = 0; i < geometry.quads.size(); ++i) {
                    auto const& quad = geometry.quads[i];
                    if (quad.hidden) continue;
                    auto const id = (static_cast<std::uint64_t>(section) << 32)
                        | (static_cast<std::uint64_t>(liquid) << 31) | i;
                    cache.faces.push_back(&quad);
                    bool const singleSided = quad.facingNormal != std::array<float, 3>{};
                    cache.keys.push_back({quad.center, id, quad.facingNormal, singleSided});
                    if (singleSided) cache.singleSidedFaces.push_back(static_cast<std::uint32_t>(cache.keys.size() - 1));
                }
            };
            append(source->terrain, false);
            append(source->liquid, true);
            cache.sources.push_back(std::move(source));
        }
    }
    if (cache.faces.empty()) {
        cache.mesh.reset();
        cache.order.clear();
        cache.hasAxis = false;
        return nullptr;
    }
    auto const& camera = context.mScreenContext.camera;
    auto const clip = camera.getProjectionMatrix() * *camera.viewMatrixStack->top()._m
        * *camera.worldMatrixStack->top()._m;
    // Clip W is view depth for the perspective world camera. Its translation
    // is common to all faces, so only these coefficients affect their order.
    std::array<float, 3> const axis{clip[0][3], clip[1][3], clip[2][3]};
    std::array<float, 3> eye{};
    if (!cache.singleSidedFaces.empty()) {
        auto const inverse = glm::inverse(*camera.viewMatrixStack->top()._m * *camera.worldMatrixStack->top()._m);
        eye = {inverse[3].x, inverse[3].y, inverse[3].z};
    }
    if (!changed && cache.hasAxis && cache.axis == axis) {
        bool const sameVisibility = cache.eye == eye || std::all_of(cache.singleSidedFaces.begin(), cache.singleSidedFaces.end(),
            [&](auto index) {
                auto const& key = cache.keys[index];
                return projectionFaceVisible(key, eye) == projectionFaceVisible(key, cache.eye);
            });
        if (sameVisibility) {
            cache.eye = eye;
            return cache.mesh.get();
        }
    }
    if (cache.faces.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() / 4))
        throw std::length_error("Projection transparent vertex count exceeds the engine limit");
    auto order = projectionTransparentOrder(cache.keys, axis, eye);
    cache.axis = axis;
    cache.eye = eye;
    cache.hasAxis = true;
    if (!changed && cache.mesh && order == cache.order) return cache.mesh.get();
    if (order.empty()) {
        cache.mesh.reset();
        cache.order.clear();
        return nullptr;
    }

    // The SDK does not export an index-only update API. Rebuild only this
    // visible transparent submission when its actual order/geometry changes;
    // never re-tessellate blocks or rebuild opaque meshes for camera movement.
    Tessellator builder{context.mScreenContext.tessellator.mBufferResourceService};
    builder.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList,
        static_cast<int>(order.size() * 4), false);
    builder.mHasNormals = true;
    for (auto index : order) {
        for (auto const& vertex : cache.faces[index]->vertices) {
            builder.mNextColor.get() = vertex.color;
            builder.mNextNormal.get() = glm::vec4{vertex.normal[0], vertex.normal[1], vertex.normal[2], 0.0f};
            builder.mNextUV[0].get() = glm::vec2{vertex.uv[0], vertex.uv[1]};
            builder.vertex(vertex.position[0], vertex.position[1], vertex.position[2]);
        }
    }
    auto mesh = std::make_unique<mce::Mesh>(builder.end(Tessellator::UploadMode::Buffered,
        "LHoloSortedTransparent", SupplementaryFieldAutoGenerationMode{0}));
    cache.mesh = std::move(mesh);
    cache.order = std::move(order);
    return cache.mesh.get();
}

} // namespace lholo::projection::detail
