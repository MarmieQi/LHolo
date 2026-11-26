// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/capture/CapturePreviewBuilder.h"

#include "i18n/Translator.h"
#include "structure/StructureLoader.h"
#include "structure/capture/CaptureBounds.h"

#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace lholo::structure::capture {
namespace {

constexpr std::size_t kMaxPreviewRenderBlocks = 2'000'000;

LocalPlayer* currentLocalPlayer() {
    auto client = ll::service::getClientInstance();
    return client ? client->getLocalPlayer() : nullptr;
}

} // namespace

PreviewBuildResult buildWorldSelectionPreview(Draft const& draft) {
    auto* player = currentLocalPlayer();
    if (!player) return {{}, i18n::tr(i18n::TextKey::CaptureStatusNoWorld)};
    if (!draft.first || !draft.second) return {{}, i18n::tr(i18n::TextKey::CaptureStatusNeedBothPoints)};

    Bounds const bounds{
        {std::min(draft.first->x, draft.second->x),
         std::min(draft.first->y, draft.second->y),
         std::min(draft.first->z, draft.second->z)},
        {std::max(draft.first->x, draft.second->x),
         std::max(draft.first->y, draft.second->y),
         std::max(draft.first->z, draft.second->z)},
    };

    auto& region = player->getDimensionBlockSource();
    if (region.mOwnerThreadID.get() != std::this_thread::get_id()) {
        return {{}, i18n::tr(i18n::TextKey::PreviewUnavailable)};
    }
    auto const size = captureSize(bounds);
    if (!size) return {{}, i18n::tr(i18n::TextKey::CaptureStatusSizeLimit)};

    BlockPos const min{bounds.min.x, bounds.min.y, bounds.min.z};
    BlockPos const max{bounds.max.x, bounds.max.y, bounds.max.z};
    if (min.y < region.getMinHeight() || max.y >= region.getMaxHeight()) {
        return {{}, i18n::tr(i18n::TextKey::CaptureStatusHeightLimit)};
    }
    if ((*size)[1] > region.getMaxHeight() - region.getMinHeight()) {
        return {{}, i18n::tr(i18n::TextKey::PreviewHeightLimit)};
    }
    if (!region.areChunksFullyLoaded(min, max)) {
        return {{}, i18n::tr(i18n::TextKey::CaptureStatusRegionNotLoaded)};
    }

    auto loaded = std::make_shared<LoadedStructure>();
    loaded->sizeX = (*size)[0];
    loaded->sizeY = (*size)[1];
    loaded->sizeZ = (*size)[2];
    loaded->volume = static_cast<std::uint64_t>(loaded->sizeX) * loaded->sizeY * loaded->sizeZ;

    SaveContext const saveContext{};
    for (int x = 0; x < loaded->sizeX; ++x) {
        for (int y = 0; y < loaded->sizeY; ++y) {
            for (int z = 0; z < loaded->sizeZ; ++z) {
                BlockPos const pos{min.x + x, min.y + y, min.z + z};
                auto const& primary = region.getBlock(pos);
                auto const& liquid = region.getLiquidBlock(pos);

                Block const* body{};
                Block const* extraLiquid{};
                if (!primary.isAir()) {
                    if (&primary == &liquid) {
                        extraLiquid = &primary;
                    } else {
                        body = &primary;
                        if (!liquid.isAir()) extraLiquid = &liquid;
                    }
                } else if (!liquid.isAir()) {
                    extraLiquid = &liquid;
                }
                if (!body && !extraLiquid) continue;

                if (loaded->renderBlocks.size() >= kMaxPreviewRenderBlocks) {
                    return {{}, i18n::tr(i18n::TextKey::CaptureStatusSizeLimit)};
                }

                std::shared_ptr<CompoundTag const> nbt;
                if (body) {
                    if (auto const* actor = region.getBlockEntity(pos)) {
                        auto tag = std::make_shared<CompoundTag>();
                        if (actor->save(*tag, saveContext)) nbt = std::move(tag);
                    }
                }
                loaded->renderBlocks.push_back(StructureBlock{x, y, z, body, extraLiquid, std::move(nbt)});
                if (body) ++loaded->primaryBlocks;
                if (extraLiquid) ++loaded->secondaryBlocks;
            }
        }
    }
    loaded->hasAirCells = loaded->renderBlocks.size() < loaded->volume;
    return {std::move(loaded), {}};
}

PreviewBuildResult buildCustomStructurePreview(CustomStructure const& draft) {
    auto* player = currentLocalPlayer();
    if (!player) return {{}, i18n::tr(i18n::TextKey::CaptureStatusNoWorld)};

    auto const volume = customStructureVolume(draft.size);
    if (!volume) return {{}, i18n::tr(i18n::TextKey::CaptureStatusSizeLimit)};

    auto& region = player->getDimensionBlockSource();
    if (draft.size[1] > region.getMaxHeight() - region.getMinHeight()) {
        return {{}, i18n::tr(i18n::TextKey::PreviewHeightLimit)};
    }

    struct ResolvedLayer {
        int y{};
        int endY{};
        Block const* block{};
    };
    std::vector<ResolvedLayer> resolved;
    resolved.reserve(draft.layers.size());
    std::uint64_t const layerArea = static_cast<std::uint64_t>(draft.size[0]) * draft.size[2];
    std::uint64_t totalBlocks{};
    for (auto const& layer : draft.layers) {
        auto block = Block::tryGetFromRegistry(HashedString{layer.block});
        if (!block || block->getTypeName() != layer.block) {
            return {{}, i18n::tr(i18n::TextKey::PreviewFailed)};
        }
        if (block->isAir()) continue;
        auto const layerCount = static_cast<std::uint64_t>(layer.endY - layer.y + 1);
        if (layerCount > kMaxPreviewRenderBlocks / std::max<std::uint64_t>(1, layerArea)
            || totalBlocks > kMaxPreviewRenderBlocks - layerCount * layerArea) {
            return {{}, i18n::tr(i18n::TextKey::CaptureStatusSizeLimit)};
        }
        totalBlocks += layerCount * layerArea;
        resolved.push_back({layer.y, layer.endY, block.as_ptr()});
    }

    auto loaded = std::make_shared<LoadedStructure>();
    loaded->sizeX = draft.size[0];
    loaded->sizeY = draft.size[1];
    loaded->sizeZ = draft.size[2];
    loaded->volume = static_cast<std::uint64_t>(*volume);

    for (auto const& layer : resolved) {
        for (int y = layer.y; y <= layer.endY; ++y) {
            for (int x = 0; x < loaded->sizeX; ++x) {
                for (int z = 0; z < loaded->sizeZ; ++z) {
                    loaded->renderBlocks.push_back(StructureBlock{x, y, z, layer.block});
                }
            }
        }
    }
    loaded->primaryBlocks = loaded->renderBlocks.size();
    loaded->hasAirCells = loaded->renderBlocks.size() < loaded->volume;
    return {std::move(loaded), {}};
}

} // namespace lholo::structure::capture
