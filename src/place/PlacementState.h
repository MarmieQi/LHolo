// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Placement-session ownership and synchronization. Callers use concrete
// operations; the underlying atomics, mutexes and mutable caches never escape
// this module.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace lholo::place::detail {

struct FailedPlanKey {
    std::int64_t cell;
    std::uint32_t runtimeId;
    int          itemAux;
    int          eyeX;
    int          eyeY;
    int          eyeZ;
    int          viewX;
    int          viewY;
    int          viewZ;

    bool operator==(FailedPlanKey const&) const = default;
};

struct FailedPlanKeyHash {
    std::size_t operator()(FailedPlanKey const& key) const noexcept {
        std::size_t result = std::hash<std::int64_t>{}(key.cell);
        auto const combine = [&result](auto value) {
            std::size_t const hash = std::hash<decltype(value)>{}(value);
            result ^= hash + 0x9e3779b9U + (result << 6U) + (result >> 2U);
        };
        combine(key.runtimeId);
        combine(key.itemAux);
        combine(key.eyeX);
        combine(key.eyeY);
        combine(key.eyeZ);
        combine(key.viewX);
        combine(key.viewY);
        combine(key.viewZ);
        return result;
    }
};

class PlacementState {
public:
    static PlacementState& getInstance();

    PlacementState(PlacementState const&)            = delete;
    PlacementState(PlacementState&&)                 = delete;
    PlacementState& operator=(PlacementState const&) = delete;
    PlacementState& operator=(PlacementState&&)      = delete;

    [[nodiscard]] bool enabled() const;
    void setEnabled(bool enabled);
    [[nodiscard]] bool rangeEnabled() const;
    void setRangeEnabled(bool enabled);
    [[nodiscard]] bool manualMode() const;
    void setManualMode(bool manual);
    [[nodiscard]] int radius() const;
    void setRadius(int radius);
    [[nodiscard]] int autoPlacementBreakCooldownSeconds() const;
    void setAutoPlacementBreakCooldownSeconds(int seconds);

    [[nodiscard]] bool manualHeld() const;
    // Starts one logical right-button press. Repeated Bedrock callbacks while
    // the same press is held are idempotent and must not reset repeat timing.
    [[nodiscard]] bool beginManualPress(std::uint64_t time);
    // A release keeps an unconsumed quick-tap request alive for the tick loop.
    void releaseManualPress();
    // Cancels both the held state and any unconsumed first-placement request.
    void cancelManualPress();
    void resetManualInput();
    [[nodiscard]] bool manualPlaceRequested() const;
    void setManualPlaceRequested(bool requested);
    [[nodiscard]] std::uint64_t manualPressAt() const;
    [[nodiscard]] std::uint64_t lastManualPlaceAt() const;
    void setLastManualPlaceAt(std::uint64_t time);

    [[nodiscard]] std::uint64_t nextPlaceAt() const;
    void setNextPlaceAt(std::uint64_t time);
    [[nodiscard]] std::uint64_t nextSwapAt() const;
    void setNextSwapAt(std::uint64_t time);

    [[nodiscard]] bool recentPlacementActive(std::int64_t cell, std::uint64_t now) const;
    void recordRecentPlacement(std::int64_t cell, std::uint64_t now, std::uint64_t expiresAt);
    [[nodiscard]] bool autoPlacementSuppressionsActive(std::uint64_t now);
    [[nodiscard]] bool autoPlacementSuppressed(std::int64_t cell, std::uint64_t now) const;
    void suppressAutoPlacement(std::int64_t cell, std::uint64_t expiresAt);
    [[nodiscard]] bool failedPlanCached(FailedPlanKey const& key, std::uint64_t now) const;
    void cacheFailedPlan(FailedPlanKey const& key, std::uint64_t now, std::uint64_t expiresAt);

    [[nodiscard]] std::string aimedProjectedBlockName() const;
    void setAimedProjectedBlockName(std::string name);

    void resetDimensionSession();
    void resetWorldSession();

private:
    PlacementState() = default;

    std::atomic_bool     mEnabled{false};
    std::atomic_bool     mRangeEnabled{false};
    std::atomic_bool     mManualMode{false};
    std::atomic_bool     mManualHeld{false};
    std::atomic_bool     mManualPlaceRequested{false};
    std::atomic_uint64_t mManualPressAt{0};
    std::atomic_uint64_t mLastManualPlaceAt{0};
    std::atomic_int      mRadius{4};
    std::atomic_int      mAutoPlacementBreakCooldownSeconds{10};
    std::atomic_uint64_t mNextPlaceAt{0};
    std::atomic_uint64_t mNextSwapAt{0};

    mutable std::mutex                               mSessionCacheMutex;
    std::unordered_map<std::int64_t, std::uint64_t> mRecentPlacements;
    std::unordered_map<std::int64_t, std::uint64_t> mAutoPlacementSuppressions;
    std::atomic_uint64_t                             mNextAutoPlacementSuppressionExpiry{0};
    std::unordered_map<FailedPlanKey, std::uint64_t, FailedPlanKeyHash> mFailedRangePlans;

    mutable std::mutex mAimedProjectedBlockNameMutex;
    std::string        mAimedProjectedBlockName;
};

} // namespace lholo::place::detail
