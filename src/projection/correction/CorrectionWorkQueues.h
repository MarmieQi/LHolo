#pragma once

#include "projection/correction/CorrectionCellMask.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <utility>

namespace lholo::projection::detail {

using CorrectionPosition = std::array<int, 3>;

// Transfer notifications in world-aligned tiles, without expanding dense
// writes back into thousands of queue nodes. Destruction times stay per cell.
struct CorrectionEventBatch {
    CorrectionPosition section{};
    CorrectionCellMask cells;
    std::map<std::uint16_t, std::uint64_t> destroyed;

    std::size_t size() const {
        std::size_t count{};
        for (auto word : cells.words) count += std::popcount(word);
        return count;
    }
    bool intersects(std::array<long long, 3> const& minimum, std::array<long long, 3> const& maximum) const {
        std::array<int, 3> lo{}, hi{};
        for (int axis = 0; axis < 3; ++axis) {
            auto origin = static_cast<long long>(section[axis]) * 16;
            lo[axis] = static_cast<int>(std::clamp(minimum[axis] - origin, 0LL, 16LL));
            hi[axis] = static_cast<int>(std::clamp(maximum[axis] - origin, 0LL, 16LL));
            if (lo[axis] >= hi[axis]) return false;
        }
        if (lo == CorrectionPosition{0, 0, 0} && hi == CorrectionPosition{16, 16, 16})
            return std::any_of(cells.words.begin(), cells.words.end(), [](auto word) { return word != 0; });
        auto bits = ((std::uint64_t{1} << (hi[2] - lo[2])) - 1) << lo[2];
        for (int x = lo[0]; x < hi[0]; ++x)
            for (int y = lo[1]; y < hi[1]; ++y) {
                auto index = (x << 8) | (y << 4);
                if (cells.words[index >> 6] & (bits << (index & 63))) return true;
            }
        return false;
    }
    CorrectionEventBatch clipped(std::array<long long, 3> const& minimum,
                                 std::array<long long, 3> const& maximum) const & {
        CorrectionEventBatch result;
        result.section = section;
        result.cells = clippedCells(minimum, maximum);
        if (result.cells.words == cells.words) return *this;
        for (auto const& [cell, time] : destroyed)
            if (result.cells.contains(cell)) result.destroyed.emplace(cell, time);
        return result;
    }
    CorrectionEventBatch clipped(std::array<long long, 3> const& minimum,
                                 std::array<long long, 3> const& maximum) && {
        auto clipped = clippedCells(minimum, maximum);
        if (clipped.words == cells.words) return std::move(*this);
        cells = clipped;
        // Preserve nodes in a moved batch. Partial boundary tiles should not
        // allocate a second timestamp map during bulk removal notifications.
        for (auto it = destroyed.begin(); it != destroyed.end();)
            if (!cells.contains(it->first)) it = destroyed.erase(it);
            else ++it;
        return std::move(*this);
    }

private:
    CorrectionCellMask clippedCells(std::array<long long, 3> const& minimum,
                                    std::array<long long, 3> const& maximum) const {
        std::array<int, 3> lo{}, hi{};
        for (int axis = 0; axis < 3; ++axis) {
            auto origin = static_cast<long long>(section[axis]) * 16;
            lo[axis] = static_cast<int>(std::clamp(minimum[axis] - origin, 0LL, 16LL));
            hi[axis] = static_cast<int>(std::clamp(maximum[axis] - origin, 0LL, 16LL));
            if (lo[axis] >= hi[axis]) return {};
        }
        if (lo == CorrectionPosition{0, 0, 0} && hi == CorrectionPosition{16, 16, 16}) return cells;
        CorrectionCellMask result;
        result.box(lo, hi, true);
        for (std::size_t i = 0; i < cells.words.size(); ++i) result.words[i] &= cells.words[i];
        return result;
    }
};

// FIFO between tiles; within one tile process set bits together. A tile that
// is continuously refilled yields after 4096 consumed cells to avoid starvation.
// World access happens only at consumption, always reading the latest state.
class CorrectionEventQueue {
public:
    struct Event {
        CorrectionPosition position;
        std::uint64_t destroyedAt{};
    };
    void push(CorrectionPosition position, std::uint64_t destroyedAt) {
        CorrectionPosition section{correctionSectionCoordinate(position[0]), correctionSectionCoordinate(position[1]),
                                   correctionSectionCoordinate(position[2])};
        auto& batch = ensure(section);
        auto cell = correctionCellSlot(position[0], position[1], position[2]);
        if (!batch.cells.contains(cell)) { batch.cells.set(cell); ++batch.remaining; ++mCells; }
        batch.word = std::min(batch.word, static_cast<std::size_t>(cell >> 6));
        if (destroyedAt) {
            auto& time = batch.destroyed[cell];
            time = std::max(time, destroyedAt);
        }
    }
    void push(CorrectionEventBatch incoming) {
        auto count = incoming.size();
        if (!count) return;
        auto& batch = ensure(incoming.section);
        if (!batch.remaining) {
            static_cast<CorrectionEventBatch&>(batch) = std::move(incoming);
            batch.remaining = count;
            mCells += count;
            return;
        }
        for (std::size_t word = 0; word < batch.cells.words.size(); ++word) {
            auto added = std::popcount(incoming.cells.words[word] & ~batch.cells.words[word]);
            mCells += added;
            batch.remaining += added;
            batch.cells.words[word] |= incoming.cells.words[word];
        }
        batch.word = 0;
        batch.destroyed.merge(incoming.destroyed);
        for (auto const& [cell, time] : incoming.destroyed) {
            auto& previous = batch.destroyed[cell];
            previous = std::max(previous, time);
        }
    }
    bool empty() const { return mOrder.empty(); }
    std::size_t size() const { return mCells; }
    std::size_t sections() const { return mEvents.size(); }
    std::optional<Event> front() const {
        if (empty()) return std::nullopt;
        auto const& batch = mEvents.find(mOrder.front())->second;
        auto cell = first(batch);
        auto time = batch.destroyed.find(cell);
        return Event{{batch.section[0] * 16 + (cell >> 8), batch.section[1] * 16 + ((cell >> 4) & 15),
                      batch.section[2] * 16 + (cell & 15)}, time == batch.destroyed.end() ? 0 : time->second};
    }
    void pop() {
        assert(!empty());
        auto found = mEvents.find(mOrder.front());
        auto& batch = found->second;
        auto cell = first(batch);
        batch.cells.words[cell >> 6] &= ~(std::uint64_t{1} << (cell & 63));
        batch.destroyed.erase(cell);
        --mCells;
        if (!--batch.remaining) {
            mEvents.erase(found);
            mOrder.pop_front();
            mFrontConsumed = 0;
        } else if (++mFrontConsumed == 4096) {
            mOrder.push_back(mOrder.front());
            mOrder.pop_front();
            mFrontConsumed = 0;
        }
    }
    std::optional<CorrectionEventBatch> takeSection() {
        if (empty()) return std::nullopt;
        auto found = mEvents.find(mOrder.front());
        mCells -= found->second.remaining;
        CorrectionEventBatch batch = std::move(found->second);
        mEvents.erase(found);
        mOrder.pop_front();
        mFrontConsumed = 0;
        return batch;
    }
    // Mesh scheduling only asks about a local section plus its one-cell halo.
    // Probe intersecting world tiles, never scan the entire pending queue.
    bool intersects(std::array<long long, 3> const& minimum, std::array<long long, 3> const& maximum) const {
        if (empty()) return false;
        CorrectionPosition lo{}, hi{};
        for (int axis = 0; axis < 3; ++axis) {
            if (minimum[axis] >= maximum[axis]) return false;
            auto floor = [](long long value) { return value / 16 - (value % 16 < 0 ? 1 : 0); };
            lo[axis] = static_cast<int>(floor(minimum[axis]));
            hi[axis] = static_cast<int>(floor(maximum[axis] - 1));
        }
        for (int x = lo[0]; x <= hi[0]; ++x)
            for (int y = lo[1]; y <= hi[1]; ++y)
                for (int z = lo[2]; z <= hi[2]; ++z) {
                    auto found = mEvents.find({x, y, z});
                    if (found == mEvents.end()) continue;
                    // Only boundary tiles need clipping; retain sparse accuracy.
                    if (found->second.intersects(minimum, maximum)) return true;
                }
        return false;
    }
    void clear() {
        mEvents.clear();
        mOrder.clear();
        mCells = 0;
        mFrontConsumed = 0;
    }

private:
    struct Pending : CorrectionEventBatch {
        std::size_t remaining{};
        mutable std::size_t word{};
    };
    static std::uint16_t first(Pending const& batch) {
        while (!batch.cells.words[batch.word]) ++batch.word;
        return static_cast<std::uint16_t>(batch.word * 64 + std::countr_zero(batch.cells.words[batch.word]));
    }
    Pending& ensure(CorrectionPosition const& section) {
        auto [found, inserted] = mEvents.try_emplace(section);
        if (inserted) { found->second.section = section; mOrder.push_back(section); }
        return found->second;
    }
    std::map<CorrectionPosition, Pending> mEvents;
    std::deque<CorrectionPosition> mOrder;
    std::size_t mCells{};
    std::size_t mFrontConsumed{};
};

// A loaded subchunk may arrive during initial scanning. Refresh it immediately
// in resumable quanta, interleaving other subchunks. A second notification after
// progress has started requests another pass, never invalidates its cursor.
class CorrectionSubChunkQueue {
public:
    struct Work {
        CorrectionPosition position;
        std::size_t cell{};
    };
    void push(CorrectionPosition position) {
        auto [found, inserted] = mWork.try_emplace(position);
        if (inserted)
            mOrder.push_back(position);
        else if (found->second.cell)
            found->second.repeat = true;
    }
    bool empty() const { return mOrder.empty(); }
    std::size_t size() const { return mWork.size(); }
    std::optional<Work> front() const {
        if (empty())
            return std::nullopt;
        return Work{mOrder.front(), mWork.find(mOrder.front())->second.cell};
    }
    void advance(std::size_t cells) {
        assert(!empty());
        auto key = mOrder.front();
        auto found = mWork.find(key);
        auto& cursor = found->second;
        assert(cells && cursor.cell + cells <= 4096);
        cursor.cell += cells;
        mOrder.pop_front();
        if (cursor.cell == 4096) {
            if (!cursor.repeat) {
                mWork.erase(found);
                return;
            }
            cursor = {};
        }
        mOrder.push_back(key);
    }
    void clear() {
        mWork.clear();
        mOrder.clear();
    }

private:
    struct Cursor {
        std::size_t cell{};
        bool repeat{};
    };
    std::map<CorrectionPosition, Cursor> mWork;
    std::deque<CorrectionPosition> mOrder;
};

} // namespace lholo::projection::detail
