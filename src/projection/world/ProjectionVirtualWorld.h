// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Thread-local virtual block view used only while LHolo tessellates or renders
// projected block entities. Outside an explicit scope every query is empty.

#pragma once

#include "projection/core/ProjectionInternalTypes.h"

class Block;
class BlockActor;
class BlockPos;

namespace lholo::projection::detail {

class ScopedTessellationBlocks {
public:
    explicit ScopedTessellationBlocks(
        ExpectedBlockMap const&      blocks,
        ExpectedBlockActorMap const& blockActors
    );
    ~ScopedTessellationBlocks();

    ScopedTessellationBlocks(ScopedTessellationBlocks const&) = delete;
    ScopedTessellationBlocks& operator=(ScopedTessellationBlocks const&) = delete;

private:
    ExpectedBlockMap const*      mPreviousBlocks{};
    ExpectedBlockActorMap const* mPreviousBlockActors{};
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

Block const*      findTessellationBlock(BlockPos const& position);
BlockActor const* findTessellationBlockActor(BlockPos const& position);

} // namespace lholo::projection::detail
