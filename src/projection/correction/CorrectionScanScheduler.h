#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace lholo::projection::detail {

enum class CorrectionWorkKind : std::size_t { Event, Loaded, Initial, Extra };
inline constexpr std::size_t CorrectionQuantum = 32;

struct CorrectionPriority {
    int rank{};
    float distance{};
    bool operator<(CorrectionPriority const& other) const {
        return rank < other.rank || (rank == other.rank && distance < other.distance);
    }
};

// Per-file initial work: O(section count) storage, bounded candidate probing,
// no per-frame all-section sorting and no scan through completed sections.
class CorrectionScanScheduler {
public:
    void reset(std::span<std::vector<std::size_t> const> sections) {
        mOffsets.assign(sections.size(), 0);
        mPending.clear();
        mPositions.resize(sections.size());
        for (std::size_t i = 0; i < sections.size(); ++i) {
            if (sections[i].empty())
                continue;
            mPositions[i] = mPending.size();
            mPending.push_back(i);
        }
        mProbe = mFair = mSelections = 0;
    }
    bool empty() const { return mPending.empty(); }
    std::size_t offset(std::size_t section) const { return mOffsets[section]; }
    template <class Priority>
    std::optional<std::size_t> select(Priority&& priority, std::optional<std::size_t> nearby) {
        if (empty())
            return std::nullopt;
        if (++mSelections % 8 == 0) {
            auto section = mPending[mFair++ % mPending.size()];
            return section;
        }
        std::optional<std::size_t> best;
        CorrectionPriority bestPriority{std::numeric_limits<int>::max(), std::numeric_limits<float>::max()};
        auto consider = [&](std::size_t section) {
            auto p = priority(section);
            if (!std::isfinite(p.distance))
                p.distance = std::numeric_limits<float>::max();
            if (!best || p < bestPriority) {
                best = section;
                bestPriority = p;
            }
        };
        if (nearby && *nearby < mOffsets.size() && mPositions[*nearby] < mPending.size() &&
            mPending[mPositions[*nearby]] == *nearby)
            consider(*nearby);
        auto probes = std::min<std::size_t>(64, mPending.size());
        for (std::size_t i = 0; i < probes; ++i)
            consider(mPending[mProbe++ % mPending.size()]);
        return best;
    }
    void advance(std::size_t section, std::size_t cells, std::size_t sectionSize) {
        assert(cells && mOffsets[section] + cells <= sectionSize);
        mOffsets[section] += cells;
        if (mOffsets[section] != sectionSize)
            return;
        auto position = mPositions[section];
        auto moved = mPending.back();
        mPending[position] = moved;
        mPositions[moved] = position;
        mPending.pop_back();
        mPositions[section] = std::numeric_limits<std::size_t>::max();
    }

private:
    std::vector<std::size_t> mOffsets, mPending, mPositions;
    std::size_t mProbe{}, mFair{};
    std::uint64_t mSelections{};
};

struct CorrectionWorkCursor {
    std::size_t lane{};
    std::array<std::size_t, 4> files{};
    std::array<std::uint64_t, 4> services{};
};
struct CorrectionWorkChoice {
    std::size_t file{};
    CorrectionWorkKind kind{};
};

// All work classes receive a turn under continuous event pressure. Cursors
// survive frame exhaustion, including frames too short for a complete cycle.
template <class ReadPriority>
std::optional<CorrectionWorkChoice> selectCorrectionWork(std::size_t fileCount, CorrectionWorkCursor& cursor,
                                                         ReadPriority&& read) {
    if (!fileCount)
        return std::nullopt;
    constexpr std::array lanes{CorrectionWorkKind::Event,   CorrectionWorkKind::Event,  CorrectionWorkKind::Loaded,
                               CorrectionWorkKind::Initial, CorrectionWorkKind::Event,  CorrectionWorkKind::Initial,
                               CorrectionWorkKind::Extra,   CorrectionWorkKind::Initial};
    for (std::size_t attempt = 0; attempt < lanes.size(); ++attempt) {
        auto kind = lanes[cursor.lane++ % lanes.size()];
        auto k = static_cast<std::size_t>(kind);
        bool fair = (cursor.services[k] + 1) % 8 == 0;
        std::optional<std::size_t> best;
        CorrectionPriority bestPriority{std::numeric_limits<int>::max(), std::numeric_limits<float>::max()};
        for (std::size_t offset = 0; offset < fileCount; ++offset) {
            auto file = (cursor.files[k] + offset) % fileCount;
            auto p = read(file, kind);
            if (!p)
                continue;
            if (!std::isfinite(p->distance))
                p->distance = std::numeric_limits<float>::max();
            if (fair) {
                best = file;
                break;
            }
            if (!best || *p < bestPriority) {
                best = file;
                bestPriority = *p;
            }
        }
        if (best) {
            ++cursor.services[k];
            if (fair)
                cursor.files[k] = (*best + 1) % fileCount;
            return CorrectionWorkChoice{*best, kind};
        }
    }
    return std::nullopt;
}

} // namespace lholo::projection::detail
