// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Thread-local virtual block view used only while LHolo tessellates or renders
// projected block entities. Outside an explicit scope every query is empty.

#pragma once

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/world/ProjectionWorldLookup.h"

class Block;
class BlockActor;
class BlockPos;
class BlockSource;

namespace lholo::projection::detail {

class ScopedTessellationBlocks {
public:
    explicit ScopedTessellationBlocks(
        ProjectionWorldLookup const&      blocks,
        ExpectedBlockActorMap const& blockActors,
        bool* worldDependency = nullptr
    );
    ~ScopedTessellationBlocks();
    bool biomeSampleFailed() const;

    ScopedTessellationBlocks(ScopedTessellationBlocks const&) = delete;
    ScopedTessellationBlocks& operator=(ScopedTessellationBlocks const&) = delete;

private:
    ProjectionWorldLookup const*      mPreviousBlocks{};
    ExpectedBlockActorMap const* mPreviousBlockActors{};
    bool mPreviousBiomeSampleFailed{};
    bool* mPreviousWorldDependency{};
};

// Vanilla BlockType::connectionUpdate recomputes a block's flattened
// connection states correctly for every family (fences, glass panes, iron
// bars) but also applies its result to the region it is given. Scope the
// calls with this guard: the BlockSource setBlock hooks then swallow every
// write the update attempts on this thread and the world stays untouched.
class ScopedRegionWriteSuppression {
public:
    ScopedRegionWriteSuppression();
    ~ScopedRegionWriteSuppression();

    ScopedRegionWriteSuppression(ScopedRegionWriteSuppression const&) = delete;
    ScopedRegionWriteSuppression& operator=(ScopedRegionWriteSuppression const&) = delete;

private:
    bool mPrevious{};
};

bool regionWritesSuppressed();
void markTessellationBiomeSampleFailed();

Block const*      findTessellationBlock(BlockPos const& position);
BlockActor const* findTessellationBlockActor(BlockPos const& position);
// Read the actual region while inside a projected tessellation scope.
Block const& readRealTessellationBlock(BlockSource& region, BlockPos const& position);

} // namespace lholo::projection::detail
