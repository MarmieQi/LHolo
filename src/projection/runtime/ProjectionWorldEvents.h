// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Collection boundary for real-world block and subchunk notifications. This
// module queues facts only; downstream projection stages decide how to apply
// them.

#pragma once

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/correction/CorrectionWorkQueues.h"

#include <cstddef>
#include <mutex>
#include <vector>

class BlockSource;
class Level;

namespace lholo::projection::detail {

struct ProjectionPendingEventCounts {
    std::size_t cells{}, sections{}, loadedSections{};
};

void attachProjectionWorldEvents(Level& level, BlockSource& blockSource);
// A dimension switch invalidates the BlockSource but not the Level. Keep the
// Level listener attached so a later world exit still reaches normal cleanup.
void detachProjectionDimensionEvents();
void detachProjectionWorldEvents();
std::mutex& projectionWorldLifecycleMutex();
bool consumeWorldExitRequest();

std::vector<CorrectionEventBatch> takePendingBlockChangeSections(std::size_t limit);
ProjectionPendingEventCounts pendingProjectionEventCounts();
std::vector<SubChunkKey> takePendingLoadedSubChunks(std::size_t limit);
// No visible runtime instances: future activations scan fresh world state.
void discardPendingProjectionEvents();

} // namespace lholo::projection::detail
