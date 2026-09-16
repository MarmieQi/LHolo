// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "input/ViewMoveBasis.h"

#include <cmath>

namespace lholo::input {
namespace {

constexpr double kDegreesToRadians = 0.017453292519943295;

// Components of a unit horizontal direction, so two components within this
// distance of each other are the exactly diagonal case. An absolute epsilon is
// appropriate because both magnitudes are at most 1.
constexpr float kDiagonalTieEpsilon = 1e-5f;

// Horizontal facing for a yaw, matching the game's own convention: yaw 0 looks
// south (+Z), 90 west (-X), 180 north (-Z) and -90 east (+X).
struct Facing {
    float x{};
    float z{};
};

Facing facingFromYaw(float yawDegrees) {
    auto const yaw = static_cast<double>(yawDegrees) * kDegreesToRadians;
    return Facing{
        static_cast<float>(-std::sin(yaw)),
        static_cast<float>(std::cos(yaw))
    };
}

// Snap a horizontal direction onto its dominant world axis. Comparing the
// magnitudes first and then the sign keeps a step on exactly one axis, and the
// `>=` tie-break makes an exactly diagonal facing resolve to X. The zero check
// is a contract guard for callers other than viewRelativeMoveStep (a facing
// built from a yaw is always unit length, so it cannot be hit from there).
ViewMoveStep dominantHorizontalStep(float x, float z, int sign) {
    auto const magnitudeX = std::fabs(x);
    auto const magnitudeZ = std::fabs(z);
    if (magnitudeX == 0.0f && magnitudeZ == 0.0f) return ViewMoveStep{};
    if (magnitudeX + kDiagonalTieEpsilon >= magnitudeZ) {
        return ViewMoveStep{x >= 0.0f ? sign : -sign, 0, 0, true};
    }
    return ViewMoveStep{0, 0, z >= 0.0f ? sign : -sign, true};
}

} // namespace

ViewMoveStep viewRelativeMoveStep(HotkeyId move, float yawDegrees) {
    auto const facing = facingFromYaw(yawDegrees);
    switch (move) {
    case HotkeyId::MoveForward:  return dominantHorizontalStep(facing.x, facing.z, 1);
    case HotkeyId::MoveBackward: return dominantHorizontalStep(facing.x, facing.z, -1);
    // Right is the facing rotated a quarter turn: right = (-z, 0, x) for a
    // horizontal facing of (x, 0, z).
    case HotkeyId::MoveRight: return dominantHorizontalStep(-facing.z, facing.x, 1);
    case HotkeyId::MoveLeft:  return dominantHorizontalStep(facing.z, -facing.x, 1);
    case HotkeyId::MoveUp:    return ViewMoveStep{0, 1, 0, true};
    case HotkeyId::MoveDown:  return ViewMoveStep{0, -1, 0, true};
    default:                  return ViewMoveStep{};
    }
}

ViewMoveStep viewForwardStep(float viewX, float viewY, float viewZ, int steps) {
    if (steps == 0) return ViewMoveStep{};
    auto const dx = static_cast<int>(std::round(viewX)) * steps;
    auto const dy = static_cast<int>(std::round(viewY)) * steps;
    auto const dz = static_cast<int>(std::round(viewZ)) * steps;
    if (dx == 0 && dy == 0 && dz == 0) return ViewMoveStep{};
    return ViewMoveStep{dx, dy, dz, true};
}

} // namespace lholo::input
