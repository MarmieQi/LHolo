#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace lholo::projection::detail {
inline constexpr std::size_t ProjectionGeometryBudget = 512ULL * 1024 * 1024;
struct ProjectionResident {
    std::size_t id{}, bytes{};
    float distanceSquared{};
};
// Pure eviction policy; a normal structure below the limit keeps every mesh.
inline std::vector<std::size_t> selectProjectionEvictions(std::span<ProjectionResident const> entries,
                                                         std::size_t budget) {
    std::size_t total{};
    for (auto const& entry : entries) total += entry.bytes;
    if (total <= budget) return {};
    std::vector<ProjectionResident> ordered(entries.begin(), entries.end());
    std::sort(ordered.begin(), ordered.end(), [](auto const& a, auto const& b) {
        auto distance = [](float value) { return std::isfinite(value) ? value : 0.0f; };
        return distance(a.distanceSquared) != distance(b.distanceSquared)
            ? distance(a.distanceSquared) > distance(b.distanceSquared) : a.id > b.id;
    });
    std::vector<std::size_t> evicted;
    for (auto const& entry : ordered) {
        if (total <= budget) break;
        if (!entry.bytes) continue;
        evicted.push_back(entry.id);
        total -= entry.bytes;
    }
    return evicted;
}
} // namespace lholo::projection::detail
