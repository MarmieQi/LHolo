#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>

namespace lholo::projection::detail {

using CorrectionPosition = std::array<int, 3>;

// Queue facts once per coordinate, retaining the latest destruction time even
// when the final block is non-air. World access happens only at consumption.
class CorrectionEventQueue {
public:
    struct Event {
        CorrectionPosition position;
        std::uint64_t destroyedAt{};
    };
    void push(CorrectionPosition position, std::uint64_t destroyedAt) {
        auto [found, inserted] = mEvents.try_emplace(position, destroyedAt);
        if (inserted)
            mOrder.push_back(position);
        else
            found->second = std::max(found->second, destroyedAt);
    }
    bool empty() const { return mOrder.empty(); }
    std::optional<Event> front() const {
        if (empty())
            return std::nullopt;
        return Event{mOrder.front(), mEvents.find(mOrder.front())->second};
    }
    void pop() {
        mEvents.erase(mOrder.front());
        mOrder.pop_front();
    }
    void clear() {
        mEvents.clear();
        mOrder.clear();
    }

private:
    std::map<CorrectionPosition, std::uint64_t> mEvents;
    std::deque<CorrectionPosition> mOrder;
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
