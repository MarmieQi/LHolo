#pragma once
#include "structure/write/StructureWriteTypes.h"
#include "structure/write/command/CommandWriteSession.h"
#include "i18n/TextKeys.h"
#include <algorithm>

namespace lholo::structure::write {
inline i18n::TextKey writeResultText(WriteResult result) {
    using enum WriteResult;
    using K = i18n::TextKey;
    switch (result) {
    case None: return K::Count;
    case Complete: return K::WriteWorldComplete;
    case NoLocalWorld: return K::WriteWorldNoLocalWorld;
    case NoCommandWorld: return K::WriteWorldCommandNoWorld;
    case CommandUnsupported: return K::WriteWorldCommandUnsupported;
    case ContextChanged: return K::WriteWorldContextChanged;
    case NoSelection: return K::WriteWorldNoSelection;
    case UnmappedBlocks: return K::WriteWorldUnmappedBlocks;
    case RegionNotLoaded: return K::WriteWorldRegionNotLoaded;
    case HeightLimit: return K::WriteWorldHeightLimit;
    case CoordinateLimit: return K::WriteWorldCoordinateLimit;
    case TemplateFailed: case Failed: return K::WriteWorldFailed;
    }
    return K::WriteWorldFailed;
}
inline i18n::TextKey writeCommandText(command::Status const& status) {
    using enum command::State;
    using K = i18n::TextKey;
    switch (status.state) {
    case Idle: return K::Count;
    case Running: return K::WriteWorldCommandRunning;
    case WaitingChunks: return K::WriteWorldCommandWaiting;
    case WaitingDimension: return K::WriteWorldCommandDimension;
    case Complete: return K::WriteWorldComplete;
    case Cancelled: return K::WriteWorldCommandCancelled;
    case Paused: case Unknown: {
        auto const found = std::find(i18n::kTextKeyIds.begin(), i18n::kTextKeyIds.end(), status.error);
        return found == i18n::kTextKeyIds.end() ? K::WriteWorldCommandPaused
            : static_cast<K>(found - i18n::kTextKeyIds.begin());
    }
    }
    return K::Count;
}
}
