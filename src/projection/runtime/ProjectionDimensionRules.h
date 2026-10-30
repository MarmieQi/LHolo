#pragma once
#include <cstdint>

namespace lholo::projection::detail {
enum class DimensionActivationStatus : unsigned char {
    Ready,
    Deferred,
    Resuming,
    Rebuilding,
};

// A task publishes a new generation when rebuilding its dimension-filtered
// view. It uses its own origin, but still completes a dimension transition.
constexpr DimensionActivationStatus classifyDimensionActivation(bool suspended,
    std::uint64_t suspendedGeneration, int suspendedDimension,
    std::uint64_t generation, int dimension) {
    if (!suspended) return DimensionActivationStatus::Ready;
    if (suspendedGeneration != generation) return DimensionActivationStatus::Rebuilding;
    if (suspendedDimension != dimension) return DimensionActivationStatus::Deferred;
    return DimensionActivationStatus::Resuming;
}
}
