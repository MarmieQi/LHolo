// LHolo - CPU draw-list cache. Values and section IDs only; no game resources.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace lholo::projection::detail {

enum class ProjectionDrawSlot : unsigned char {
    Opaque,
    Alpha,
    AlphaOneSided,
    Blend,
    Liquid,
    Placeholder,
    MissingFill,
    MissingOutline,
    WrongFill,
    WrongOutline,
    Count
};
inline constexpr auto kProjectionDrawSlotCount = static_cast<std::size_t>(ProjectionDrawSlot::Count);
static_assert(kProjectionDrawSlotCount <= 16);
inline constexpr std::uint16_t projectionDrawBit(ProjectionDrawSlot slot) {
    return static_cast<std::uint16_t>(1u << static_cast<unsigned>(slot));
}

struct ProjectionDrawPosition {
    float x{}, y{}, z{};
    bool operator==(ProjectionDrawPosition const&) const = default;
};
struct ProjectionDrawRecord {
    std::size_t section{};
    ProjectionDrawPosition center;
    std::uint16_t slots{};
};
struct ProjectionTerrainDraw {
    std::size_t section{};
    std::size_t bucket{};
    bool operator==(ProjectionTerrainDraw const&) const = default;
};

class ProjectionDrawCache {
  public:
    std::array<std::vector<std::size_t>, kProjectionDrawSlotCount> lists;
    std::vector<ProjectionTerrainDraw> transparentTerrain;
    std::uint64_t recordBuilds{};
    std::uint64_t orderBuilds{};
    std::uint64_t listBuilds{};

    [[nodiscard]] std::size_t residentCount() const { return mResidents.size(); }

    // ReadRecord inspects active storage only during resource/layout changes.
    // IsVisible reads this frame's classification. Neither callback is retained.
    template <class ReadRecord, class IsVisible>
    void prepare(std::uint64_t activation, std::uint64_t meshRevision, std::uint64_t visibilityRevision,
                 std::size_t sectionCount, ProjectionDrawPosition eyeLocal,
                 ReadRecord&& read, IsVisible&& visible) {
        bool const resourcesChanged = !mResourcesValid || mActivation != activation ||
                                      mMeshRevision != meshRevision || mSectionCount != sectionCount;
        if (resourcesChanged) {
            mResidents.clear();
            std::array<std::size_t, kProjectionDrawSlotCount> counts{};
            std::size_t terrainCount{};
            for (std::size_t section = 0; section < sectionCount; ++section) {
                auto record = read(section);
                record.section = section;
                if (!record.slots)
                    continue;
                mResidents.push_back(record);
                for (std::size_t slot = 0; slot < counts.size(); ++slot) {
                    if (record.slots & (1u << slot)) {
                        ++counts[slot];
                        if (slot == static_cast<std::size_t>(ProjectionDrawSlot::Blend))
                            ++terrainCount;
                    }
                }
            }
            // Reserve against all resident draws, not today's field of view:
            // rotating the camera must not cause fresh list allocations.
            for (std::size_t slot = 0; slot < lists.size(); ++slot) {
                // Blend is a resource bit only: its draws always go through
                // transparentTerrain, so this uniform slot never owns a list.
                if (slot != static_cast<std::size_t>(ProjectionDrawSlot::Blend))
                    reserveGrowing(lists[slot], counts[slot]);
            }
            reserveGrowing(transparentTerrain, terrainCount);
            reserveGrowing(mDistanceOrder, mResidents.size());
            mVisible.resize(mResidents.size());
            mActivation = activation;
            mMeshRevision = meshRevision;
            mSectionCount = sectionCount;
            mResourcesValid = true;
            mOrderValid = mListsValid = false;
            ++recordBuilds;
        }

        bool const visibilityChanged = !mListsValid || mVisibilityRevision != visibilityRevision;
        if (visibilityChanged) {
            for (std::size_t index = 0; index < mResidents.size(); ++index) {
                mVisible[index] = visible(mResidents[index].section) ? 1 : 0;
            }
        }
        bool const eyeChanged = mHaveEye && mEye != eyeLocal;
        // Standing turns amortize one resident ordering across all directions.
        // While walking, sort only visible candidates, so a narrow view does
        // not pay O(all resident sections log all resident sections) each frame.
        // The second pass of the same view must not upgrade/re-sort that order.
        bool const globalOrder = !eyeChanged && (mGlobalOrder || visibilityChanged);
        bool const orderChanged =
            !mOrderValid || eyeChanged || mGlobalOrder != globalOrder;
        if (orderChanged) {
            mDistanceOrder.clear();
            auto const sortedMask = projectionDrawBit(ProjectionDrawSlot::Blend)
                | projectionDrawBit(ProjectionDrawSlot::Liquid);
            for (std::size_t index = 0; index < mResidents.size(); ++index) {
                auto const& record = mResidents[index];
                if (!(record.slots & sortedMask))
                    continue;
                if (!globalOrder && !mVisible[index])
                    continue;
                auto const dx = record.center.x - eyeLocal.x;
                auto const dy = record.center.y - eyeLocal.y;
                auto const dz = record.center.z - eyeLocal.z;
                auto const squared = dx * dx + dy * dy + dz * dz;
                mDistanceOrder.push_back(
                    {index, std::isfinite(squared) ? squared : std::numeric_limits<float>::max()});
            }
            // Distance is computed once per section, not inside O(N log N)
            // comparisons. Ties retain deterministic section order.
            std::sort(mDistanceOrder.begin(), mDistanceOrder.end(), [](auto const& a, auto const& b) {
                return a.distanceSquared != b.distanceSquared ? a.distanceSquared > b.distanceSquared
                                                              : a.resident < b.resident;
            });
            mEye = eyeLocal;
            mHaveEye = true;
            mGlobalOrder = globalOrder;
            mOrderValid = true;
            mListsValid = false;
            ++orderBuilds;
        }
        if (mListsValid && mVisibilityRevision == visibilityRevision)
            return;

        for (auto& list : lists)
            list.clear();
        transparentTerrain.clear();
        for (std::size_t index = 0; index < mResidents.size(); ++index) {
            auto const& record = mResidents[index];
            if (!mVisible[index])
                continue;
            for (std::size_t slot = 0; slot < 3; ++slot) {
                if (record.slots & (1u << slot))
                    lists[slot].push_back(record.section);
            }
            // Placeholders and correction meshes keep their existing section
            // order. Liquids and blend terrain use the distance order.
            for (std::size_t slot = static_cast<std::size_t>(ProjectionDrawSlot::Placeholder);
                 slot < lists.size(); ++slot) {
                if (record.slots & (1u << slot))
                    lists[slot].push_back(record.section);
            }
        }
        for (auto const& entry : mDistanceOrder) {
            if (!mVisible[entry.resident])
                continue;
            auto const& record = mResidents[entry.resident];
            if (record.slots & projectionDrawBit(ProjectionDrawSlot::Blend)) {
                transparentTerrain.push_back(
                    {record.section, static_cast<std::size_t>(ProjectionDrawSlot::Blend)});
            }
            if (record.slots & projectionDrawBit(ProjectionDrawSlot::Liquid)) {
                lists[static_cast<std::size_t>(ProjectionDrawSlot::Liquid)].push_back(record.section);
            }
        }
        mVisibilityRevision = visibilityRevision;
        mListsValid = true;
        ++listBuilds;
    }

  private:
    template <class T> static void reserveGrowing(std::vector<T>& values, std::size_t count) {
        // Progressive uploads often add one section per frame. Reserving the
        // exact new count each time would reallocate/copy on every upload.
        if (values.capacity() < count)
            values.reserve(std::max(count, values.capacity() + values.capacity() / 2 + 8));
    }

    struct DistanceEntry {
        std::size_t resident;
        float distanceSquared;
    };
    std::vector<ProjectionDrawRecord> mResidents;
    std::vector<DistanceEntry> mDistanceOrder;
    std::vector<unsigned char> mVisible;
    ProjectionDrawPosition mEye;
    std::uint64_t mActivation{}, mMeshRevision{}, mVisibilityRevision{};
    std::size_t mSectionCount{};
    bool mResourcesValid{}, mOrderValid{}, mListsValid{}, mHaveEye{}, mGlobalOrder{};
};

struct ProjectionDrawTimings {
    struct TransparencySample {
        enum Stage : std::size_t { Collect, View, Sort, Setup, Vertices, Finalize, Draw, Total, StageCount };
        std::array<std::uint64_t, StageCount> cpuMicros{};
        std::size_t faces{}, sections{}, vertices{};
        bool called{}, collected{}, sorted{}, rebuilt{}, reused{};
    };
    struct TransparencyWindow {
        std::array<std::uint64_t, TransparencySample::StageCount> sum{}, peak{};
        std::chrono::steady_clock::time_point started{};
        std::uint64_t calls{}, collected{}, sorted{}, rebuilt{}, reused{}, vertices{};
        std::uint64_t meshPass{}, meshPassPeak{}, visibility{}, visibilityPeak{}, draws{}, drawsPeak{};
        std::size_t faces{}, facesPeak{}, sections{}, sectionsPeak{};
    };
    struct Sample {
        std::uint64_t prepare{}, submit{};
        std::size_t draws{};
        std::uint64_t visibility{};
    };
    std::array<Sample, 120> samples{};
    Sample opaque;
    std::uint64_t frames{}, lastOrderBuilds{};
    std::uint64_t lastVisibilityMicros{};
    TransparencySample transparency;
    TransparencyWindow transparencyWindow;
};

} // namespace lholo::projection::detail
