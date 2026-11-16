// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Placement-session ownership and synchronization. Callers use concrete
// operations; the underlying atomics, mutexes and mutable caches never escape
// this module.

#pragma once

#include "place/AimedProjectedBlock.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
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

inline std::int64_t packPlacementCell(int x, int y, int z) {
    return (static_cast<std::int64_t>(x) & 0x3FFFFFF) << 38
         | (static_cast<std::int64_t>(z) & 0x3FFFFFF) << 12
         | (static_cast<std::int64_t>(y) & 0xFFF);
}

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
    [[nodiscard]] int localRadius() const { return mLocalRadius.load(); }
    void setLocalRadius(int radius) { mLocalRadius.store(radius); }
    [[nodiscard]] bool localLiquidsDisabled() const { return mLocalLiquidsDisabled.load(); }
    void setLocalLiquidsDisabled(bool disabled) { mLocalLiquidsDisabled.store(disabled); }
    [[nodiscard]] bool localShulkerRestock() const { return mLocalShulkerRestock.load(); }
    void setLocalShulkerRestock(bool enabled) { mLocalShulkerRestock.store(enabled); }
    [[nodiscard]] bool localCreativeRestock() const { return mLocalCreativeRestock.load(); }
    void setLocalCreativeRestock(bool enabled) { mLocalCreativeRestock.store(enabled); }
    [[nodiscard]] bool localCreativeRestockOverwrite() const { return mLocalCreativeRestockOverwrite.load(); }
    void setLocalCreativeRestockOverwrite(bool enabled) { mLocalCreativeRestockOverwrite.store(enabled); }
    [[nodiscard]] int autoPlacementBreakCooldownSeconds() const;
    void setAutoPlacementBreakCooldownSeconds(int seconds);

    // Starts one logical right-button press. Repeated Bedrock callbacks while
    // the same press is held are idempotent and must not reset repeat timing.
    [[nodiscard]] bool beginManualPress(std::uint64_t time);
    // A release keeps an unconsumed quick-tap request alive for the tick loop.
    void releaseManualPress();
    // Cancels both the held state and any unconsumed first-placement request.
    void cancelManualPress();
    void resetManualInput();
    [[nodiscard]] std::optional<std::uint64_t> dueManualPress(std::uint64_t now);
    void recordManualAttempt(std::uint64_t pressId, std::uint64_t now);

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

    [[nodiscard]] AimedProjectedBlock aimedProjectedBlock() const;
    void setAimedProjectedBlock(AimedProjectedBlock value);

    void resetDimensionSession();
    void resetWorldSession();

private:
    PlacementState() = default;

    std::atomic_bool     mEnabled{false};
    std::atomic_bool     mRangeEnabled{false};
    std::atomic_bool     mManualMode{false};
    std::atomic_int      mRadius{4};
    std::atomic_int      mLocalRadius{4};
    std::atomic_bool     mLocalLiquidsDisabled{true};
    std::atomic_bool     mLocalShulkerRestock{};
    std::atomic_bool     mLocalCreativeRestock{};
    std::atomic_bool     mLocalCreativeRestockOverwrite{};
    std::atomic_int      mAutoPlacementBreakCooldownSeconds{10};
    std::atomic_uint64_t mNextPlaceAt{0};
    std::atomic_uint64_t mNextSwapAt{0};

    std::mutex mManualInputMutex;
    bool mManualHeld{};
    bool mManualPlaceRequested{};
    std::uint64_t mManualPressId{};
    std::uint64_t mManualPressAt{};
    std::uint64_t mLastManualAttemptAt{};

    mutable std::mutex                               mSessionCacheMutex;
    std::unordered_map<std::int64_t, std::uint64_t> mRecentPlacements;
    std::unordered_map<std::int64_t, std::uint64_t> mAutoPlacementSuppressions;
    std::atomic_uint64_t                             mNextAutoPlacementSuppressionExpiry{0};
    std::unordered_map<FailedPlanKey, std::uint64_t, FailedPlanKeyHash> mFailedRangePlans;

    mutable std::mutex mAimedProjectedBlockMutex;
    AimedProjectedBlock mAimedProjectedBlock;
};

} // namespace lholo::place::detail
