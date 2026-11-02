#pragma once

#include "projection/correction/CorrectionCellMask.h"

namespace lholo::projection::detail {

// Only one coverage bitmap per file. Region tiles are built lazily inside the
// correction budget; no dense allocation over a potentially enormous volume.
class CorrectionAirScan {
public:
    void reset() { *this = CorrectionAirScan{}; }
    bool complete() const { return mComplete; }
    std::array<int, 3> tileOrigin() const { return mOrigin; }
    std::size_t eraseBox(std::array<int, 3> lo, std::array<int, 3> hi) {
        std::size_t before{}, after{};
        for (auto word : mMask.words)
            before += std::popcount(word);
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = static_cast<int>(std::clamp(static_cast<long long>(lo[axis]) - mOrigin[axis], 0LL, 16LL));
            hi[axis] = static_cast<int>(std::clamp(static_cast<long long>(hi[axis]) - mOrigin[axis], 0LL, 16LL));
        }
        mMask.box(lo, hi, false);
        for (auto word : mMask.words)
            after += std::popcount(word);
        return before - after;
    }
    template <class Regions, class ReadOccupancy>
    std::optional<std::array<int, 3>> step(Regions const& regions, ReadOccupancy&& read) {
        if (auto cell = mMask.take(mWord))
            return position(*cell);
        if (mRegion >= regions.size()) {
            mComplete = true;
            return std::nullopt;
        }
        auto const& box = regions[mRegion];
        if (box.sizeX <= 0 || box.sizeY <= 0 || box.sizeZ <= 0) {
            ++mRegion;
            mStart = true;
            return std::nullopt;
        }
        std::array<int, 3> lower{box.x, box.y, box.z};
        std::array<long long, 3> upper{static_cast<long long>(box.x) + box.sizeX,
                                       static_cast<long long>(box.y) + box.sizeY,
                                       static_cast<long long>(box.z) + box.sizeZ};
        if (mStart) {
            for (int axis = 0; axis < 3; ++axis)
                mNext[axis] = correctionSectionCoordinate(lower[axis]);
            mStart = false;
        }
        for (int axis = 0; axis < 3; ++axis)
            mOrigin[axis] = mNext[axis] * 16;
        std::array<int, 3> lo{}, hi{};
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = static_cast<int>(std::max(0LL, static_cast<long long>(lower[axis]) - mOrigin[axis]));
            hi[axis] = static_cast<int>(std::min(16LL, upper[axis] - mOrigin[axis]));
        }
        mMask = {};
        mMask.box(lo, hi, true);
        mMask.subtract(read(mNext));
        mWord = 0;
        // Advance the tile cursor now; mOrigin/mMask retain the current tile.
        for (int axis = 2; axis >= 0; --axis) {
            ++mNext[axis];
            if (static_cast<long long>(mNext[axis]) * 16 < upper[axis])
                break;
            mNext[axis] = correctionSectionCoordinate(lower[axis]);
            if (axis == 0) {
                ++mRegion;
                mStart = true;
            }
        }
        if (auto cell = mMask.take(mWord))
            return position(*cell);
        return std::nullopt;
    }

private:
    std::array<int, 3> position(std::uint16_t cell) const {
        return {mOrigin[0] + (cell >> 8), mOrigin[1] + ((cell >> 4) & 15), mOrigin[2] + (cell & 15)};
    }
    CorrectionCellMask mMask;
    std::array<int, 3> mOrigin{}, mNext{};
    std::size_t mRegion{}, mWord{64};
    bool mStart{true}, mComplete{};
};

} // namespace lholo::projection::detail
