#include "structure/capture/StructureCapture.h"

#include "i18n/Message.h"
#include "structure/capture/McstructureExporter.h"

#include <algorithm>
#include <mutex>

#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"

namespace lholo::structure::capture {
namespace {

std::mutex    gMutex;
Draft         gDraft;
i18n::Message gStatus{i18n::TextKey::CaptureStatusNeedTwoPoints};
Level*        gLevel{};
Dimension*    gDimension{};
std::uint64_t gRevision{};

struct ClientContext {
    LocalPlayer* player{};
    Level*       level{};
    Dimension*   dimension{};
};

ClientContext currentContext() {
    auto client = ll::service::getClientInstance();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player) return {};
    return {player, &player->getLevel(), &player->getDimension()};
}

void resetLocked() {
    gDraft = {};
    gStatus = i18n::Message{i18n::TextKey::CaptureStatusNeedTwoPoints};
    ++gRevision;
}

void syncContextLocked(ClientContext const& context) {
    if (!context.player) {
        if (gLevel || gDimension || gDraft.first || gDraft.second) resetLocked();
        gLevel = nullptr;
        gDimension = nullptr;
        return;
    }
    if (gLevel && (gLevel != context.level || gDimension != context.dimension)) resetLocked();
    gLevel = context.level;
    gDimension = context.dimension;
}

Bounds normalizedBounds(Draft const& draft) {
    auto const& first = *draft.first;
    auto const& second = *draft.second;
    return {
        {std::min(first.x, second.x), std::min(first.y, second.y), std::min(first.z, second.z)},
        {std::max(first.x, second.x), std::max(first.y, second.y), std::max(first.z, second.z)}
    };
}

void setStatus(i18n::Message const& status) {
    std::lock_guard lock(gMutex);
    gStatus = status;
}

} // namespace

Snapshot getSnapshot() {
    auto const context = currentContext();
    std::lock_guard lock(gMutex);
    syncContextLocked(context);
    return {gDraft, static_cast<bool>(context.player), i18n::format(gStatus), gRevision};
}

std::optional<Bounds> getBounds() {
    auto const context = currentContext();
    std::lock_guard lock(gMutex);
    syncContextLocked(context);
    if (!context.player || !gDraft.first || !gDraft.second) return std::nullopt;
    return normalizedBounds(gDraft);
}

void updateDraft(Draft const& draft) {
    auto const context = currentContext();
    std::lock_guard lock(gMutex);
    syncContextLocked(context);
    if (!context.player) return;
    if (gDraft == draft) return;
    gDraft = draft;
    if (gDraft.first && gDraft.second) {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusSelectionReady};
    } else if (gDraft.first || gDraft.second) {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusNeedOtherPoint};
    } else {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusNeedTwoPoints};
    }
    ++gRevision;
}

void setPointFromPlayer(PointSlot slot) {
    auto const context = currentContext();
    if (!context.player) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusNoWorld});
        return;
    }
    auto const position = context.player->getFeetBlockPos();

    std::lock_guard lock(gMutex);
    syncContextLocked(context);
    auto& point = slot == PointSlot::First ? gDraft.first : gDraft.second;
    point = Point{position.x, position.y, position.z};
    gStatus = i18n::Message{
        slot == PointSlot::First
            ? i18n::TextKey::CaptureStatusPoint1Recorded
            : i18n::TextKey::CaptureStatusPoint2Recorded
    };
    ++gRevision;
}

void exportStructure(Draft const& draft, std::filesystem::path const& output) {
    auto const context = currentContext();
    if (!context.player) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusNoWorld});
        return;
    }
    if (draft.mode != CaptureMode::Client) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusSingleplayerUnsupported});
        return;
    }
    if (!draft.first || !draft.second) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusNeedBothPoints});
        return;
    }

    Bounds bounds;
    {
        std::lock_guard lock(gMutex);
        syncContextLocked(context);
        if (gDraft != draft) {
            gDraft = draft;
            ++gRevision;
        }
        bounds = normalizedBounds(gDraft);
    }

    BlockPos const min{bounds.min.x, bounds.min.y, bounds.min.z};
    BlockPos const max{bounds.max.x, bounds.max.y, bounds.max.z};
    auto& region = context.player->getDimensionBlockSource();
    if (!region.areChunksFullyLoaded(min, max)) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusRegionNotLoaded});
        return;
    }

    auto structure = StructureTemplate::create(
        "lholo:client_export",
        region,
        BoundingBox{min, max},
        false,
        !draft.includeEntities
    );
    if (!structure) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusTemplateFailed});
        return;
    }
    if (!exportMcstructure(*structure, output)) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusWriteFailed});
        return;
    }
    setStatus(i18n::Message{i18n::TextKey::CaptureStatusExported});
}

void clear() {
    std::lock_guard lock(gMutex);
    resetLocked();
}

} // namespace lholo::structure::capture
