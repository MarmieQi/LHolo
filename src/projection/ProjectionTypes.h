// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include "structure/LayerDisplayTypes.h"

#include <cstdint>
#include <memory>
#include <vector>

class Block;

namespace lholo::structure {
struct LoadedStructure;
}

namespace lholo::projection {

struct BuildProgress {
    std::uint64_t placed{};
    std::uint64_t total{};
    std::uint64_t visiblePlaced{};
    std::uint64_t visibleTotal{};
    std::uint64_t wrongType{};
    std::uint64_t wrongState{};
    std::uint64_t extra{};
};

// Stable public contract for consumers deriving data from projection progress.
// The session mutex and mutable ProjectionState remain private to projection.
struct MaterialProgressKey {
    std::uint64_t structureGeneration{};
    std::uint64_t activationGeneration{};
    std::uint64_t progressRevision{};
    structure::LayerDisplayMode layerDisplayMode{structure::LayerDisplayMode::All};
    int           displayLayer{};
    structure::LayerAxis        layerAxis{structure::LayerAxis::Y};

    bool operator==(MaterialProgressKey const&) const = default;
};

struct MaterialProgressSnapshot {
    MaterialProgressKey                         key;
    std::shared_ptr<structure::LoadedStructure const> structure;
    std::vector<unsigned char>                  progressCorrect;
};

// Easy-place support: one locked lookup of the projected virtual world.
struct ProjectionQuery {
    Block const* block; // nullptr for non-placeable or hidden projection cells
    bool         missing;
};

// Range-place support: a missing projection cell and its expected block.
struct RangeCandidate {
    int          x;
    int          y;
    int          z;
    Block const* block;
};

// A real solid block was removed from a cell owned by the active projection.
// `destroyedAt` uses the same monotonic Windows uptime clock as placement
// throttling, so the placement layer can apply policy without projection
// depending on it.
struct BrokenProjectionCell {
    int           x;
    int           y;
    int           z;
    std::uint64_t destroyedAt;
};

} // namespace lholo::projection
