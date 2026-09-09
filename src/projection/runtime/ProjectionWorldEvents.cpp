// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/runtime/ProjectionWorldEvents.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <tuple>

#include <Windows.h>

#include "mc/world/level/BlockSource.h"
#include "mc/world/level/BlockSourceListener.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelListener.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/LevelChunk.h"

namespace lholo::projection::detail {
namespace {

std::mutex                gPendingEventsMutex;
std::deque<PendingBlockChange> gIncomingBlockChanges;
std::deque<SubChunkKey>   gIncomingLoadedSubChunks;
std::atomic<BlockSource*> gAttachedBlockSource{};
std::atomic<ChunkSource*> gAttachedChunkSource{};
std::atomic<Level*>       gAttachedLevel{};
std::atomic_bool          gWorldExitPending{};

class ProjectionBlockSourceListener final : public BlockSourceListener {
public:
    void onSourceDestroyed(BlockSource& source) override {
        auto* expected = &source;
        if (gAttachedBlockSource.compare_exchange_strong(
                expected,
                nullptr,
                std::memory_order_acq_rel
            )) {
            gAttachedChunkSource.store(nullptr, std::memory_order_release);
        }
    }

    void onBlockChanged(
        BlockSource&,
        BlockPos const&              pos,
        uint                           layer,
        Block const&                  block,
        Block const&                  oldBlock,
        int,
        ActorBlockSyncMessage const*,
        BlockChangedEventTarget,
        Actor*
    ) override {
        // Layer zero is the ordinary solid-block layer. Keep the occurrence
        // time with the fact so delayed consumers never restart the 10-second
        // auto-placement suppression window.
        bool const destroyed = layer == 0 && !oldBlock.isAir() && block.isAir();
        std::lock_guard lock(gPendingEventsMutex);
        gIncomingBlockChanges.push_back(PendingBlockChange{
            pos,
            destroyed ? GetTickCount64() : 0,
        });
    }
};

ProjectionBlockSourceListener gProjectionBlockSourceListener;

class ProjectionLevelListener final : public LevelListener {
public:
    void onSubChunkLoaded(
        ChunkSource& source,
        LevelChunk&  chunk,
        short        absoluteSubChunkIndex,
        bool
    ) override {
        if (&source != gAttachedChunkSource.load(std::memory_order_acquire)) return;
        auto const& chunkPosition = chunk.getPosition();
        std::lock_guard lock(gPendingEventsMutex);
        gIncomingLoadedSubChunks.emplace_back(
            chunkPosition.x,
            static_cast<int>(absoluteSubChunkIndex),
            chunkPosition.z
        );
    }

    void onLevelDestruction(std::string const&) override {
        gAttachedLevel.store(nullptr, std::memory_order_release);
        // The level owns this block source and is already tearing it down. Do
        // not retain or later call removeListener through a dying object.
        gAttachedBlockSource.store(nullptr, std::memory_order_release);
        gAttachedChunkSource.store(nullptr, std::memory_order_release);
        // Only publish a fact here. Projection shutdown waits for workers and
        // belongs on the next normal overlay frame, outside engine teardown.
        gWorldExitPending.store(true, std::memory_order_release);
    }
};

ProjectionLevelListener gProjectionLevelListener;

} // namespace

void attachProjectionWorldEvents(Level& level, BlockSource& blockSource) {
    if (auto* attached = gAttachedBlockSource.load(std::memory_order_acquire);
        attached != &blockSource) {
        if (attached) {
            attached->removeListener(gProjectionBlockSourceListener);
        }
        blockSource.addListener(gProjectionBlockSourceListener);
        gAttachedBlockSource.store(&blockSource, std::memory_order_release);
    }
    gAttachedChunkSource.store(&blockSource.getChunkSource(), std::memory_order_release);
    if (auto* attached = gAttachedLevel.load(std::memory_order_acquire); attached != &level) {
        if (attached) {
            attached->removeListener(gProjectionLevelListener);
        }
        level.addListener(gProjectionLevelListener);
        gAttachedLevel.store(&level, std::memory_order_release);
    }
}

void detachProjectionDimensionEvents() {
    gAttachedChunkSource.store(nullptr, std::memory_order_release);
    if (auto* blockSource = gAttachedBlockSource.exchange(nullptr, std::memory_order_acq_rel)) {
        blockSource->removeListener(gProjectionBlockSourceListener);
    }
    std::lock_guard lock(gPendingEventsMutex);
    gIncomingBlockChanges.clear();
    gIncomingLoadedSubChunks.clear();
}

void detachProjectionWorldEvents() {
    if (auto* level = gAttachedLevel.exchange(nullptr, std::memory_order_acq_rel)) {
        level->removeListener(gProjectionLevelListener);
    }
    detachProjectionDimensionEvents();
}

bool consumeWorldExitRequest() {
    if (!gWorldExitPending.load(std::memory_order_acquire)) return false;
    return gWorldExitPending.exchange(false, std::memory_order_acq_rel);
}

std::vector<PendingBlockChange> takePendingBlockChanges(std::size_t limit) {
    std::vector<PendingBlockChange> changes;
    {
        std::lock_guard lock(gPendingEventsMutex);
        auto const count = std::min(limit, gIncomingBlockChanges.size());
        changes.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            changes.push_back(gIncomingBlockChanges.front());
            gIncomingBlockChanges.pop_front();
        }
    }
    std::sort(changes.begin(), changes.end(), [](auto const& lhs, auto const& rhs) {
        return std::tie(lhs.position.x, lhs.position.y, lhs.position.z)
            < std::tie(rhs.position.x, rhs.position.y, rhs.position.z);
    });
    std::vector<PendingBlockChange> merged;
    merged.reserve(changes.size());
    for (auto const& change : changes) {
        if (!merged.empty()
            && merged.back().position.x == change.position.x
            && merged.back().position.y == change.position.y
            && merged.back().position.z == change.position.z) {
            // Preserve the latest destruction if one position changed more
            // than once before this batch was consumed.
            merged.back().destroyedAt = std::max(merged.back().destroyedAt, change.destroyedAt);
            continue;
        }
        merged.push_back(change);
    }
    return merged;
}

std::vector<SubChunkKey> takePendingLoadedSubChunks(std::size_t limit) {
    std::vector<SubChunkKey> loaded;
    {
        std::lock_guard lock(gPendingEventsMutex);
        auto const count = std::min(limit, gIncomingLoadedSubChunks.size());
        loaded.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            loaded.push_back(gIncomingLoadedSubChunks.front());
            gIncomingLoadedSubChunks.pop_front();
        }
    }
    return loaded;
}

} // namespace lholo::projection::detail
