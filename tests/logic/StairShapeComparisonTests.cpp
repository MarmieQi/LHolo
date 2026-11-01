#include "projection/correction/StairShapeComparison.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <vector>

int runStairShapeComparisonTests() {
    using namespace lholo::projection::detail;
    int failures{}, checks{};
    auto check = [&](bool value, char const* message) {
        ++checks;
        if (!value) { ++failures; std::fprintf(stderr, "StairShapeComparisonTests: %s\n", message); }
    };
    AABB const base{{0, 0, 0}, {1, .5f, 1}};
    std::vector<AABB> const outer{base, {{.5f, .5f, 0}, {1, 1, .5f}}};
    // Same outer corner, with the base subdivided and pieces in another order.
    std::vector<AABB> const outerSplit{
        {{.5f, .5f, 0}, {1, 1, .5f}}, {{.5f, 0, 0}, {1, .5f, 1}}, {{0, 0, 0}, {.5f, .5f, 1}}
    };
    std::vector<AABB> const wrongOuter{base, {{0, .5f, 0}, {.5f, 1, .5f}}};
    // Two overlapping decompositions of the same inner corner.
    std::vector<AABB> const inner{
        base, {{.5f, .5f, 0}, {1, 1, 1}}, {{0, .5f, 0}, {1, 1, .5f}}
    };
    std::vector<AABB> const innerSplit{
        {{.5f, .5f, .5f}, {1, 1, 1}}, base, {{0, .5f, 0}, {1, 1, .5f}}
    };
    std::vector<AABB> const straightEast{base, {{.5f, .5f, 0}, {1, 1, 1}}};
    std::vector<AABB> const straightNorth{base, {{0, .5f, 0}, {1, 1, .5f}}};
    auto transform = [](std::vector<AABB> boxes, int rotation, bool mirror, bool upsideDown) {
        for (auto& box : boxes) {
            if (mirror) {
                float const x = box.min.x;
                box.min.x = 1 - box.max.x;
                box.max.x = 1 - x;
            }
            for (int i = 0; i < rotation; ++i) {
                float const x0 = box.min.x, x1 = box.max.x;
                box.min.x = 1 - box.max.z;
                box.max.x = 1 - box.min.z;
                box.min.z = x0;
                box.max.z = x1;
            }
            if (upsideDown) {
                float const y = box.min.y;
                box.min.y = 1 - box.max.y;
                box.max.y = 1 - y;
            }
            box.min += Vec3{-193, 74, 2047};
            box.max += Vec3{-193, 74, 2047};
        }
        return boxes;
    };
    for (int rotation = 0; rotation < 4; ++rotation)
        for (bool mirror : {false, true})
            for (bool upsideDown : {false, true}) {
                auto apply = [&](auto const& shape) { return transform(shape, rotation, mirror, upsideDown); };
                check(sameStairShape(apply(outer), apply(outerSplit)), "outer corner subdivision/order");
                check(sameStairShape(apply(inner), apply(innerSplit)), "inner corner overlap/subdivision");
                check(!sameStairShape(apply(outer), apply(wrongOuter)), "another outer corner differs");
                check(!sameStairShape(apply(outer), apply(inner)), "inner and outer corners differ");
                check(!sameStairShape(apply(straightEast), apply(straightNorth)), "straight direction matters");
                check(!sameStairShape(apply(outer), transform(outer, rotation, mirror, !upsideDown)),
                    "upside-down half matters");
            }
    auto duplicate = outer;
    duplicate.push_back(outer[1]);
    check(sameStairShape(outer, duplicate), "duplicate native boxes do not change occupied volume");
    auto narrow = outer;
    narrow[1].max.x -= .0625f;
    check(!sameStairShape(outer, narrow), "differences smaller than a half block are preserved");
    check(!sameStairShape({}, {}) && !sameStairShape({}, outer), "missing native geometry cannot match");
    auto invalid = outer;
    invalid[1].min.x = std::numeric_limits<float>::quiet_NaN();
    check(!sameStairShape(outer, invalid), "invalid native boxes cannot match");
    invalid = outer;
    invalid[1].max = invalid[1].min;
    check(!sameStairShape(outer, invalid), "degenerate native boxes cannot match");
    std::printf("StairShapeComparisonTests: %d checks, %d failures\n", checks, failures);
    return failures;
}
