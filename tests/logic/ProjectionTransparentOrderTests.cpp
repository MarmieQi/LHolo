#include "projection/core/ProjectionTransparentOrder.h"
#include "projection/core/ProjectionBottomFace.h"
#include "projection/mesh/ProjectionTransparentMeshData.h"
#include <chrono>
#include <cstdio>

int runProjectionTransparentOrderTests() {
    using namespace lholo::projection::detail;
    int failures{};
    auto check = [&](bool result, char const* name) {
        if (!result) { ++failures; std::fprintf(stderr, "Transparent order FAIL: %s\n", name); }
    };
    // One section: back glass, water, front glass must interleave regardless
    // of which source mesh each face originally belonged to.
    std::vector<ProjectionTransparentFaceKey> faces{
        {{0, 0, -1}, 0}, {{0, 0, -3}, 1}, {{0, 0, -2}, 2}
    };
    auto const mixedOrder = projectionTransparentOrder(faces, {0, 0, -1});
    check(mixedOrder == std::vector<std::uint32_t>{1, 2, 0}, "glass/water interleave");
    check(projectionTransparentOrder(faces, {0, 0, 1}) == std::vector<std::uint32_t>{0, 2, 1}, "opposite view");
    faces = {{{-2, 0, 0}, 0}, {{0, 0, 0}, 1}, {{3, 0, 0}, 2}};
    check(projectionTransparentOrder(faces, {1, 0, 0}) == std::vector<std::uint32_t>{2, 1, 0}, "camera rotation");
    faces = {{{0, 0, -2}, 10}, {{0, 0, -2}, 5}};
    auto const equalDepth = projectionTransparentOrder(faces, {0, 0, -1});
    check(equalDepth == std::vector<std::uint32_t>{1, 0}, "coplanar stable identity");
    check(projectionTransparentOrder(faces, {0, 0, -1}) == equalDepth, "stationary repeat");
    std::reverse(faces.begin(), faces.end());
    auto const reordered = projectionTransparentOrder(faces, {0, 0, -1});
    check(faces[reordered.front()].id == 5, "input order independent tie");
    faces = {{{0, 0, std::numeric_limits<float>::quiet_NaN()}, 0}, {{0, 0, -2}, 1}};
    check(projectionTransparentOrder(faces, {0, 0, -1}).front() == 1, "nonfinite depth is deterministic");
    check(projectionTransparentOrder({}, {0, 0, -1}).empty(), "empty geometry");
    faces = {{{0, 0, -2}, 0, {0, 0, -1}, true}, {{0, 0, -3}, 1, {0, 0, 1}, true},
        {{0, 0, -2}, 2, {0, 0, -1}, false}};
    check(projectionTransparentOrder(faces, {0, 0, -1}, std::array<float, 3>{0, 0, 0})
        == std::vector<std::uint32_t>{1, 2}, "glass backface culled, double-sided liquid retained");
    std::array<std::array<float, 3>, 4> square{{{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}};
    check(projectionUnitFaceNeighbor(square, {0, 0, 0}) == std::array<int, 3>{1, 0, 0}, "full cube shared boundary");
    square[2][2] = 0.5f;
    check(!projectionUnitFaceNeighbor(square, {0, 0, 0}), "partial face must not hide a neighbor");
    std::array<std::array<float, 3>, 4> bottom{{{0.25f,0,0.75f},{0.25f,0,0.25f},
        {0.75f,0,0.25f},{0.75f,0,0.75f}}};
    check(projectionBottomFace(bottom, {0,0,0}), "partial model bottom covered by full support");
    std::reverse(bottom.begin(), bottom.end());
    check(!projectionBottomFace(bottom, {0,0,0}), "upward decoration preserved");
    std::reverse(bottom.begin(), bottom.end());
    bottom[0][1] = 0.00001f;
    check(!projectionBottomFace(bottom, {0,0,0}), "raised or sloped faces are not coplanar bottoms");
    bottom[0][1] = 0; bottom[0][0] = -0.25f;
    check(!projectionBottomFace(bottom, {0,0,0}), "face outside support footprint preserved");
    ProjectionTransparentFaceKey const liquidBottom{{0.5f,0,0.5f},0,{0,-1,0},true};
    check(!projectionFaceVisible(liquidBottom, {0.5f,3,0.5f})
        && projectionFaceVisible(liquidBottom, {0.5f,-1,0.5f}), "floating liquid bottom visible only from below");

    std::array<ProjectionTransparentQuad, 3> quads;
    std::vector<ProjectionTransparentQuad const*> sources;
    for (std::size_t face = 0; face < quads.size(); ++face) {
        sources.push_back(&quads[face]);
        for (std::size_t corner = 0; corner < 4; ++corner) {
            auto const value = static_cast<float>(face * 10 + corner);
            quads[face].vertices[corner] = {{value, -value, value + 0.5f}, {0, 1, -1},
                {value / 32, -value / 64}, static_cast<std::uint32_t>(0x10203040U + face * 4 + corner)};
        }
    }
    std::vector<glm::vec3> positions;
    std::vector<glm::vec4> normals;
    std::vector<std::uint32_t> colors;
    std::vector<glm::vec2> uv;
    for (auto const& selected : {std::vector<std::uint32_t>{2, 0, 1}, {1}, {0, 2, 0}, {}}) {
        auto const bounds = fillProjectionTransparentVertices(sources, selected, positions, normals, colors, uv);
        check(positions.size() == selected.size() * 4 && normals.size() == positions.size()
            && colors.size() == positions.size() && uv.size() == positions.size(), "all streams resize together without stale tail");
        glm::vec3 lo{std::numeric_limits<float>::max()}, hi{std::numeric_limits<float>::lowest()};
        glm::vec2 uvLo{std::numeric_limits<float>::max()}, uvHi{std::numeric_limits<float>::lowest()};
        for (std::size_t index = 0; index < positions.size(); ++index) {
            auto const& original = quads[selected[index / 4]].vertices[index % 4];
            check(positions[index] == glm::vec3{original.position[0], original.position[1], original.position[2]}
                && normals[index] == glm::vec4{original.normal[0], original.normal[1], original.normal[2], 0}
                && colors[index] == original.color && uv[index] == glm::vec2{original.uv[0], original.uv[1]},
                "batch stream preserves ordered corners, normals, alpha/color and UV");
            for (int axis = 0; axis < 3; ++axis) {
                lo[axis] = std::min(lo[axis], original.position[axis]);
                hi[axis] = std::max(hi[axis], original.position[axis]);
            }
            for (int axis = 0; axis < 2; ++axis) {
                uvLo[axis] = std::min(uvLo[axis], original.uv[axis]);
                uvHi[axis] = std::max(uvHi[axis], original.uv[axis]);
            }
        }
        check(bounds.positions == std::pair{lo, hi} && bounds.uv == std::pair{uvLo, uvHi},
            "bounds include only submitted vertices and preserve negative positions and UVs");
    }
    // CPU packing only: comparable face count to the reported blueprint,
    // without Minecraft calls, native allocation or GPU submission.
    std::vector<ProjectionTransparentQuad const*> benchmarkFaces(40000, &quads[0]);
    std::vector<std::uint32_t> benchmarkOrder(benchmarkFaces.size());
    for (std::size_t i = 0; i < benchmarkOrder.size(); ++i) benchmarkOrder[i] = static_cast<std::uint32_t>(i);
    fillProjectionTransparentVertices(benchmarkFaces, benchmarkOrder, positions, normals, colors, uv);
    auto const capacity = positions.capacity();
    auto const start = std::chrono::steady_clock::now();
    for (int repeat = 0; repeat < 8; ++repeat)
        fillProjectionTransparentVertices(benchmarkFaces, benchmarkOrder, positions, normals, colors, uv);
    auto const elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    check(positions.size() == 160000 && positions.capacity() == capacity, "steady-size packing reuses stream capacity");
    std::printf("Transparent CPU packing only: 160000 vertices, %.3f ms average (no Minecraft/GPU)\n", elapsed / 8);
    return failures;
}
