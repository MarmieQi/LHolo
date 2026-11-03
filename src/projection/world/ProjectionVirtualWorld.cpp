// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/world/ProjectionVirtualWorld.h"

#include <tuple>
#include <utility>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

namespace lholo::projection::detail {
namespace {

thread_local ExpectedBlockMap const*      gTessellationBlocks{};
thread_local ExpectedBlockActorMap const* gTessellationBlockActors{};
thread_local bool                         gSuppressRegionWrites{};
thread_local bool                         gBiomeSampleFailed{};
thread_local bool*                        gWorldDependency{};

} // namespace

ScopedRegionWriteSuppression::ScopedRegionWriteSuppression()
: mPrevious(std::exchange(gSuppressRegionWrites, true)) {}

ScopedRegionWriteSuppression::~ScopedRegionWriteSuppression() {
    gSuppressRegionWrites = mPrevious;
}

bool regionWritesSuppressed() {
    return gSuppressRegionWrites;
}

ScopedTessellationBlocks::ScopedTessellationBlocks(
    ExpectedBlockMap const&      blocks,
    ExpectedBlockActorMap const& blockActors,
    bool* worldDependency
)
: mPreviousBlocks(std::exchange(gTessellationBlocks, &blocks)),
  mPreviousBlockActors(std::exchange(gTessellationBlockActors, &blockActors)),
  mPreviousBiomeSampleFailed(std::exchange(gBiomeSampleFailed, false)),
  mPreviousWorldDependency(std::exchange(gWorldDependency, worldDependency)) {}

ScopedTessellationBlocks::~ScopedTessellationBlocks() {
    gTessellationBlocks      = mPreviousBlocks;
    gTessellationBlockActors = mPreviousBlockActors;
    gBiomeSampleFailed = mPreviousBiomeSampleFailed;
    gWorldDependency = mPreviousWorldDependency;
}

bool ScopedTessellationBlocks::biomeSampleFailed() const {
    return gBiomeSampleFailed;
}

void markTessellationBiomeSampleFailed() {
    if (gTessellationBlocks) gBiomeSampleFailed = true;
}

Block const* findTessellationBlock(BlockPos const& position) {
    if (!gTessellationBlocks) return nullptr;
    auto const found = gTessellationBlocks->find(
        std::tuple{position.x, position.y, position.z}
    );
    if (found != gTessellationBlocks->end()) return found->second;
    if (gWorldDependency) *gWorldDependency = true;
    return nullptr;
}

BlockActor const* findTessellationBlockActor(BlockPos const& position) {
    if (!gTessellationBlockActors) return nullptr;
    auto const found = gTessellationBlockActors->find(
        std::tuple{position.x, position.y, position.z}
    );
    return found == gTessellationBlockActors->end() ? nullptr : found->second.get();
}

Block const& readRealTessellationBlock(BlockSource& region, BlockPos const& position) {
    struct Restore {
        ExpectedBlockMap const* previous;
        ~Restore() { gTessellationBlocks = previous; }
    } restore{std::exchange(gTessellationBlocks, nullptr)};
    return region.getBlock(position);
}

} // namespace lholo::projection::detail
