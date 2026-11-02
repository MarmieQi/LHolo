// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// CPU geometry construction shared by the asynchronous worker and synchronous
// compatibility fallback.

#pragma once

#include <cstddef>
#include <array>

#include "mc/client/renderer/Tessellator.h"
#include "mc/util/Mirror.h"
#include "mc/util/Rotation.h"

class BlockSource;
class BlockTessellator;
class Block;

namespace lholo::projection::detail {

struct ProjectionState;

// Conservative union of the engine's declared biome sampling patterns.
std::array<int, 3> projectionBiomeSamplingPadding();
bool projectionUsesBiomeTint(Block const& block);

struct ProjectionSectionBuildSettings {
    Mirror   mirror{Mirror::None};
    Rotation rotation{Rotation::None};
    int      mirrorMode{};
    int      rotationTurns{};
    int      offsetX{};
    int      offsetY{};
    int      offsetZ{};
    float    correctionFillOpacity{};
    float    correctionOutlineOpacity{};
    bool     identityTransform{};
    bool     correctionOnly{};
};

void buildProjectionSection(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    BlockTessellator&                     blockTessellator,
    BlockSource&                          region,
    std::size_t                           section,
    Tessellator::UploadMode               uploadMode,
    ProjectionSectionBuildSettings const& settings
);

// Geometry-only overlay update: no world/chunk source or BlockTessellator.
void buildProjectionCorrectionSection(ProjectionState& state, Tessellator& tessellator, std::size_t section,
    Tessellator::UploadMode uploadMode, ProjectionSectionBuildSettings const& settings);

} // namespace lholo::projection::detail
