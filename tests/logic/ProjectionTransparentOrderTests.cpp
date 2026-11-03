#include "projection/core/ProjectionTransparentOrder.h"
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
    return failures;
}
