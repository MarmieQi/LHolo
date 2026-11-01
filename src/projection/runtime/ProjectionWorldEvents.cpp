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
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/chunk/LevelChunk.h"

#include "projection/mesh/ProjectionMeshWorker.h"

namespace lholo::projection::detail {
namespace {

std::mutex                gPendingEventsMutex;
std::mutex                gWorldLifecycleMutex;
std::deque<PendingBlockChange> gIncomingBlockChanges;
std::deque<SubChunkKey>   gIncomingLoadedSubChunks;
std::atomic<BlockSource*> gAttachedBlockSource{};
std::atomic<ChunkSource*> gAttachedChunkSource{};
std::atomic<Level*>       gAttachedLevel{};
std::atomic_bool          gWorldExitPending{};

class ProjectionBlockSourceListener final : public BlockSourceListener {
public:
    void onBlockEntityChanged(BlockSource& source, BlockActor& actor) override {
        if (actor.getType() != BlockActorType::Banner) return;
        std::lock_guard lock(gPendingEventsMutex);
        if (&source != gAttachedBlockSource.load(std::memory_order_acquire)) return;
        // Reuse the bounded correction queue. Entity-only updates are not
        // player destruction and must not start placement suppression.
        gIncomingBlockChanges.push_back({actor.mPosition.get(), 0});
    }

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
        BlockSource& source,
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
        // Keep the source check under the same lock as the queue mutation. A
        // callback that started just before world teardown must not append an
        // old-world event after onLevelDestruction() has cleared the queue.
        if (&source != gAttachedBlockSource.load(std::memory_order_acquire)) return;
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
        auto const& chunkPosition = chunk.mPosition.get();
        std::lock_guard lock(gPendingEventsMutex);
        // See onBlockChanged(): source validation and queue insertion must be
        // ordered with teardown's queue clear.
        if (&source != gAttachedChunkSource.load(std::memory_order_acquire)) return;
        gIncomingLoadedSubChunks.emplace_back(
            chunkPosition.x,
            static_cast<int>(absoluteSubChunkIndex),
            chunkPosition.z
        );
    }

    void onLevelDestruction(std::string const&) override {
        std::lock_guard lifecycleLock(gWorldLifecycleMutex);
        // Publish before waiting for the worker barrier so the render path
        // stops using the old session while engine teardown is in progress.
        gWorldExitPending.store(true, std::memory_order_release);
        // Worker tasks retain non-owning Level/Dimension/ChunkView pointers.
        // Join them before the engine starts destroying the level; deferring
        // this barrier until Present leaves a use-after-free window.
        stopMeshWorker();
        gAttachedLevel.store(nullptr, std::memory_order_release);
        // The level owns this block source and is already tearing it down. Do
        // not retain or later call removeListener through a dying object.
        gAttachedBlockSource.store(nullptr, std::memory_order_release);
        gAttachedChunkSource.store(nullptr, std::memory_order_release);
        {
            std::lock_guard lock(gPendingEventsMutex);
            gIncomingBlockChanges.clear();
            gIncomingLoadedSubChunks.clear();
        }
        // Projection shutdown waits for workers and belongs on the next normal
        // overlay/render frame, outside engine teardown.
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
    std::lock_guard lifecycleLock(gWorldLifecycleMutex);
    if (auto* level = gAttachedLevel.exchange(nullptr, std::memory_order_acq_rel)) {
        level->removeListener(gProjectionLevelListener);
    }
    detachProjectionDimensionEvents();
    gWorldExitPending.store(false, std::memory_order_release);
}

std::mutex& projectionWorldLifecycleMutex() {
    return gWorldLifecycleMutex;
}

bool consumeWorldExitRequest() {
    // Keep the signal sticky until detachProjectionWorldEvents() has completed
    // the world-state cleanup. Both the Present and render paths may observe
    // it; clearing it at the first observer allowed the other path to activate
    // the old StructureSession in the new world.
    return gWorldExitPending.load(std::memory_order_acquire);
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
