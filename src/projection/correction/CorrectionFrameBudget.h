#pragma once

#include <chrono>
#include <cstddef>

namespace lholo::projection::detail {

// Owned once by the root opaque update, shared by every file. This limits
// cooperative work; it cannot interrupt one expensive native comparison.
class CorrectionFrameBudget {
public:
    using Clock = std::chrono::steady_clock;
    inline static constexpr auto DefaultDuration = std::chrono::microseconds{1200};
    inline static constexpr std::size_t MaximumWork = 16384;
    inline static constexpr auto IntakeDuration = DefaultDuration / 4;
    inline static constexpr int MaximumIntakeBatches = 64;
    inline static constexpr std::size_t EventSectionsPerBatch = 4;
    inline static constexpr int LoadedIntakeBatches = 8;
    inline static constexpr std::size_t LoadedSectionsPerBatch = 2;
    explicit CorrectionFrameBudget(std::chrono::microseconds duration = DefaultDuration,
                                   std::size_t maximumWork = MaximumWork, Clock::time_point start = Clock::now())
        : mDeadline(start + duration), mMaximumWork(maximumWork) {}

    bool availableAt(Clock::time_point now) {
        if (now >= mDeadline || mWork >= mMaximumWork)
            mExpired = true;
        return !mExpired;
    }
    bool available() { return availableAt(Clock::now()); }
    bool tryConsumeAt(Clock::time_point now) {
        if (!availableAt(now))
            return false;
        ++mWork;
        return true;
    }
    bool tryConsume() {
        if (mExpired || mWork >= mMaximumWork)
            return false;
        if (mWork % 16 == 0)
            return tryConsumeAt(Clock::now());
        ++mWork;
        return true;
    }
    std::size_t work() const { return mWork; }

private:
    Clock::time_point mDeadline;
    std::size_t mMaximumWork{};
    std::size_t mWork{};
    bool mExpired{};
};

} // namespace lholo::projection::detail
