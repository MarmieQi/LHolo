// LHolo - Pure section visibility and scheduling rules.
#pragma once

#include "projection/core/ProjectionFrustum.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>

namespace lholo::structure {
struct LoadedStructure;
}

namespace lholo::projection::detail {

// Exact cache key: turning, moving, projection changes and resource publication
// all invalidate classification. No camera/world object is retained.
struct ProjectionViewKey {
    std::array<float, 16> matrix{};
    float originX{}, originY{}, originZ{};
    std::uint64_t activation{}, meshRevision{};
    std::size_t sections{};
    bool hasMatrix{};
    bool operator==(ProjectionViewKey const&) const = default;
};

struct ProjectionBounds {
    Vec3 minimum{};
    Vec3 maximum{};
    bool valid{};

    void include(Vec3 const &point) {
        if (!valid) {
            minimum = maximum = point;
            valid = true;
            return;
        }
        minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z)};
        maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z)};
    }
};

// Partial overlay publication keeps existing model bounds. Empty new geometry
// cannot shrink them; unknown CPU bounds remain conservatively unculled.
inline ProjectionBounds mergeCorrectionBounds(ProjectionBounds existing, ProjectionBounds const& update, bool complete) {
    if (!existing.valid || !complete) return {};
    if (update.valid) { existing.include(update.minimum); existing.include(update.maximum); }
    return existing;
}

// Transform cell origins, then add their unit extent; transforming boundary
// coordinates directly introduces a one-cell error on reversed axes.
ProjectionBounds projectionSectionBounds(std::tuple<int, int, int> const &key,
                                         structure::LoadedStructure const &loaded, int mirror, int rotation);

inline bool projectionBoundsVisible(ProjectionFrustum const &frustum, ProjectionBounds const &bounds,
                                    Vec3 const &origin, float padding = 0) {
    if (!bounds.valid)
        return true;
    return frustum.isBoxVisible({bounds.minimum.x + origin.x - padding, bounds.minimum.y + origin.y - padding,
                                 bounds.minimum.z + origin.z - padding},
                                {bounds.maximum.x + origin.x + padding, bounds.maximum.y + origin.y + padding,
                                 bounds.maximum.z + origin.z + padding});
}

// Hidden edits must not starve initial geometry currently in view.
inline int projectionBuildRank(bool visible, bool prefetch, bool incremental) {
    return (visible ? 0 : prefetch ? 2 : 4) + (incremental ? 0 : 1);
}

struct ProjectionBuildCandidate {
    bool dirty{};
    bool inFlight{};
    bool visible{};
    bool prefetch{};
    bool incremental{};
    float distanceSquared{};
    bool interactive{};
};

// One linear pass, no queue allocation or stale heap entries after camera moves.
// The eighth selection uses round-robin fairness instead of view priority.
template <class ReadCandidate>
std::optional<std::size_t> selectProjectionBuild(std::size_t count, std::uint64_t selections,
                                                 std::size_t cursor, ReadCandidate &&read) {
    bool const background = (selections + 1) % 8 == 0;
    std::optional<std::size_t> selected;
    int selectedRank = std::numeric_limits<int>::max();
    float selectedDistance = std::numeric_limits<float>::max();
    for (std::size_t offset = 0; offset < count; ++offset) {
        auto const index = background ? (cursor + offset) % count : offset;
        auto const candidate = read(index);
        if (!candidate.dirty || candidate.inFlight)
            continue;
        if (background)
            return index;
        auto const rank = candidate.interactive ? -1
            : projectionBuildRank(candidate.visible, candidate.prefetch, candidate.incremental);
        auto const distance = std::isfinite(candidate.distanceSquared) ? candidate.distanceSquared
                                                                       : std::numeric_limits<float>::max();
        if (!selected || rank < selectedRank || (rank == selectedRank && distance < selectedDistance)) {
            selected = index;
            selectedRank = rank;
            selectedDistance = distance;
        }
    }
    return selected;
}

} // namespace lholo::projection::detail
