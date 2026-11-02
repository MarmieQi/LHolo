#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lholo::projection::detail {

// Initialization coverage is independent of traversal order. Incremental events
// may check cells ahead of the initial cursor without completing them twice.
class CorrectionScanProgress {
public:
    void reset(std::size_t total) {
        mTotal = total;
        mChecked = 0;
        mVisited.assign((total + 63) / 64, 0);
    }
    void record(std::size_t index) {
        assert(index < mTotal);
        auto& word = mVisited[index / 64];
        auto const mask = std::uint64_t{1} << (index % 64);
        if (!(word & mask)) {
            word |= mask;
            ++mChecked;
        }
    }
    bool complete() const { return mChecked == mTotal; }

private:
    std::vector<std::uint64_t> mVisited;
    std::size_t mTotal{};
    std::size_t mChecked{};
};

} // namespace lholo::projection::detail
