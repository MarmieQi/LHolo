#pragma once
#include <cassert>
#include <cstddef>

namespace lholo::projection::detail {
inline constexpr std::size_t ProjectionResidencyProbesPerFrame = 128;
inline constexpr std::size_t ProjectionResidencyReleasesPerFrame = 4;

// Repeated position/resource updates renew the scan without restarting its
// cursor, so continuous movement cannot starve the end of a large blueprint.
class ProjectionResidencyScan {
    std::size_t mCount{}, mRemaining{}, mCursor{};
public:
    void request(std::size_t count) {
        mCount = mRemaining = count;
        if (mCursor >= count) mCursor = 0;
    }
    bool pending() const { return mRemaining != 0; }
    std::size_t next() {
        assert(pending());
        auto const index = mCursor;
        if (++mCursor == mCount) mCursor = 0;
        --mRemaining;
        return index;
    }
};
} // namespace lholo::projection::detail
