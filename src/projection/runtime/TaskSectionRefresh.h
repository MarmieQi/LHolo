#pragma once

#include "projection/ProjectionTypes.h"
#include "projection/correction/CorrectionState.h"
#include "structure/TaskProjectionView.h"

#include <deque>
#include <map>
#include <tuple>

namespace lholo::projection::detail {

// Choose the smaller traversal: intersecting grid cells or existing sparse
// Sections. Empty space in a giant source box never creates layout entries.
template <class Keys, class Index, class Visit>
void visitTaskSections(structure::TaskCellBounds const &local, Keys const &keys, Index const &index, Visit visit) {
    if (local.empty())
        return;
    long long volume = 1;
    auto limit = static_cast<long long>(keys.size());
    for (int axis = 0; axis < 3; ++axis) {
        auto extent = (local.maximum[axis] - 1) / 16 - local.minimum[axis] / 16 + 1;
        if (volume > limit / extent) {
            volume = limit + 1;
            break;
        }
        volume *= extent;
    }
    if (volume > limit) {
        for (std::size_t section = 0; section < keys.size(); ++section) {
            auto [x, y, z] = keys[section];
            structure::TaskCellBounds box{{x * 16LL, y * 16LL, z * 16LL},
                                          {x * 16LL + 16, y * 16LL + 16, z * 16LL + 16}};
            if (!box.intersect(local).empty())
                visit(section);
        }
    } else {
        for (int x = static_cast<int>(local.minimum[0] / 16); x <= (local.maximum[0] - 1) / 16; ++x)
            for (int y = static_cast<int>(local.minimum[1] / 16); y <= (local.maximum[1] - 1) / 16; ++y)
                for (int z = static_cast<int>(local.minimum[2] / 16); z <= (local.maximum[2] - 1) / 16; ++z) {
                    auto found = index.find(std::tuple{x, y, z});
                    if (found != index.end())
                        visit(found->second);
                }
    }
}

// Section/cell cursors coalesce repeated edits without allocating one job per
// blueprint cell. A section can be resumed under the shared frame budget.
class TaskSectionRefresh {
  public:
    void request(std::size_t section) {
        auto [it, inserted] = mOffsets.try_emplace(section, 0);
        if (inserted)
            mOrder.push_back(section);
        else
            it->second = 0;
    }
    bool empty() const { return mOrder.empty(); }
    std::size_t section() const { return mOrder.front(); }
    std::size_t offset() const { return mOffsets.at(mOrder.front()); }
    void advance(std::size_t count, std::size_t total) {
        auto &offset = mOffsets.at(mOrder.front());
        offset += count;
        if (offset == total) {
            mOffsets.erase(mOrder.front());
            mOrder.pop_front();
        }
    }

  private:
    std::map<std::size_t, std::size_t> mOffsets;
    std::deque<std::size_t> mOrder;
};

// Large interaction volumes remain boxes until serviced. Emit one world
// Section at a time into the existing resumable loaded-section work lane.
class TaskAirRefresh {
  public:
    void request(structure::TaskCellBounds const &bounds) {
        if (bounds.empty())
            return;
        structure::TaskCellBounds box;
        auto floor = [](long long n) { return n / 16 - (n % 16 < 0); };
        for (int a = 0; a < 3; ++a) {
            box.minimum[a] = floor(bounds.minimum[a]);
            box.maximum[a] = floor(bounds.maximum[a] - 1) + 1;
        }
        for (auto const &pending : mBoxes)
            if (pending.position == pending.box.minimum && pending.box.intersect(box) == box)
                return;
        mBoxes.push_back({box, box.minimum});
    }
    bool empty() const { return mBoxes.empty(); }
    std::array<int, 3> take() {
        auto &front = mBoxes.front();
        auto const p = front.position;
        if (++front.position[2] == front.box.maximum[2]) {
            front.position[2] = front.box.minimum[2];
            if (++front.position[1] == front.box.maximum[1]) {
                front.position[1] = front.box.minimum[1];
                if (++front.position[0] == front.box.maximum[0])
                    mBoxes.pop_front();
            }
        }
        return {static_cast<int>(p[0]), static_cast<int>(p[1]), static_cast<int>(p[2])};
    }
    void clear() { mBoxes.clear(); }

  private:
    struct Cursor {
        structure::TaskCellBounds box;
        std::array<long long, 3> position;
    };
    std::deque<Cursor> mBoxes;
};

inline void addTaskCellProgress(BuildProgress &p, CorrectionState state) {
    ++p.total;
    ++p.visibleTotal;
    if (state == CorrectionState::Correct) {
        ++p.placed;
        ++p.visiblePlaced;
    }
    if (state == CorrectionState::WrongType)
        ++p.wrongType;
    if (state == CorrectionState::WrongState)
        ++p.wrongState;
}
inline void removeTaskCellProgress(BuildProgress &p, CorrectionState state) {
    --p.total;
    --p.visibleTotal;
    if (state == CorrectionState::Correct) {
        --p.placed;
        --p.visiblePlaced;
    }
    if (state == CorrectionState::WrongType)
        --p.wrongType;
    if (state == CorrectionState::WrongState)
        --p.wrongState;
}
} // namespace lholo::projection::detail
