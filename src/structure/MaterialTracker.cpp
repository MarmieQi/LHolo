// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/MaterialTracker.h"

#include "block/BlockPlacementRules.h"
#include "projection/Projection.h"
#include "structure/StructureLoader.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/locale/I18n.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/registry/ItemRegistry.h"
#include "mc/world/item/registry/ItemRegistryManager.h"
#include "mc/world/level/block/Block.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lholo::structure::detail {
namespace {

constexpr int kInventorySlots = 36;
constexpr std::uint64_t kAvailabilityRefreshMs = 400;
constexpr std::uint64_t kMaterialHudRecountIntervalMs = 400;

using BlockCounts = std::unordered_map<Block const*, std::uint64_t>;

struct RawMaterialCounts {
    BlockCounts body;
    BlockCounts liquid;
};

using MaterialHudKey = projection::MaterialProgressKey;
using MaterialHudInput = projection::MaterialProgressSnapshot;

struct MaterialHudResult {
    MaterialHudKey   key;
    RawMaterialCounts counts;
};

struct MaterialHudWorkerState {
    std::optional<std::future<MaterialHudResult>> inFlight;
    std::optional<MaterialHudKey>                 published;
    std::uint64_t                                 nextScheduleAt{};
};

MaterialHudWorkerState& materialHudWorkerState() {
    static MaterialHudWorkerState state;
    return state;
}

void countBlock(BlockCounts& counts, Block const* blockValue) {
    if (!blockValue) return;
    auto& count = counts[blockValue];
    if (count != std::numeric_limits<std::uint64_t>::max()) ++count;
}

RawMaterialCounts collectRawMaterials(
    std::vector<LoadedStructure::RenderBlock> const& renderBlocks
) {
    RawMaterialCounts counts;
    // Palette cardinality is normally tiny compared with the block count. Do
    // not reserve one hash bucket per projected cell for very large structures.
    counts.body.reserve(std::min<std::size_t>(renderBlocks.size(), 4096));
    counts.liquid.reserve(std::min<std::size_t>(renderBlocks.size(), 64));
    for (auto const& entry : renderBlocks) {
        countBlock(counts.body, entry.block);
        countBlock(counts.liquid, entry.liquid);
    }
    return counts;
}

std::string localizedBlockName(Block const& block, std::string_view localeCode) {
    auto const& typeName = block.getTypeName();
    auto const itemId = ItemRegistry::getBlockItemId(block);
    auto const item = ItemRegistryManager::getItemRegistry().getItem(itemId);
    if (auto* itemPtr = item.get()) {
        ItemStack const itemStack(*itemPtr, 1, 0, nullptr);
        auto const name = itemStack.getName();
        if (!name.empty() && name != typeName) return name;
    }

    auto const translationKey = block.buildDescriptionName();
    if (!translationKey.empty()) {
        auto& i18n = ::getI18n();
        auto locale = localeCode.empty()
            ? i18n.getCurrentLanguage().get()
            : i18n.getLocaleFor(std::string{localeCode});
        if (locale) {
            auto const localized = i18n.get(translationKey, std::vector<std::string>{}, locale);
            if (!localized.empty() && localized != translationKey) return localized;
        }
    }

    auto name = block.getDisplayName();
    if (name.empty()) name = typeName;
    return name;
}

std::vector<MaterialRequirement> resolveMaterials(
    RawMaterialCounts counts,
    std::string_view localeCode
) {
    std::map<std::string, MaterialRequirement> byType;
    std::map<std::string, MaterialRequirement> byLiquidType;
    auto aggregate = [&](auto& destination, Block const* blockValue, std::uint64_t blockCount) {
        if (!blockValue || blockCount == 0) return;

        std::string const typeName{blockValue->getTypeName()};
        if (typeName == "minecraft:bubble_column"
            || typeName == "minecraft:piston_arm_collision"
            || typeName == "minecraft:sticky_piston_arm_collision"
            || typeName == "minecraft:moving_block") {
            return;
        }

        std::string key;
        MaterialRequirement requirement;
        requirement.typeName = typeName;
        if (typeName == "minecraft:water" || typeName == "minecraft:flowing_water") {
            key = "minecraft:water";
            requirement.displayName = "水";
        } else if (typeName == "minecraft:lava" || typeName == "minecraft:flowing_lava") {
            key = "minecraft:lava";
            requirement.displayName = "熔岩";
        } else if (auto const item = block::resolvePlacementItem(*blockValue); item.valid) {
            key = "item:" + item.itemId;
            requirement.displayName = item.displayName;
            requirement.itemId = item.itemId;
            requirement.stackSize = item.stackSize;
        } else {
            key = typeName;
            requirement.displayName = localizedBlockName(*blockValue, localeCode);
        }

        auto const result = destination.try_emplace(key, std::move(requirement));
        auto& total = result.first->second.count;
        auto const maximum = std::numeric_limits<std::uint64_t>::max();
        total = blockCount > maximum - total ? maximum : total + blockCount;
    };

    // Registry/localization work now runs once per unique palette state rather
    // than once per projected cell. This is the critical path for million-block
    // structures; the first pass above is only pointer counting.
    for (auto const& [blockValue, count] : counts.body) aggregate(byType, blockValue, count);
    for (auto const& [blockValue, count] : counts.liquid) {
        aggregate(byLiquidType, blockValue, count);
    }

    std::vector<MaterialRequirement> materials;
    materials.reserve(byType.size() + byLiquidType.size());
    auto appendSorted = [&materials](auto& source) {
        std::vector<MaterialRequirement> sorted;
        sorted.reserve(source.size());
        for (auto& entry : source) sorted.push_back(std::move(entry.second));
        std::sort(sorted.begin(), sorted.end(), [](auto const& left, auto const& right) {
            if (left.count != right.count) return left.count > right.count;
            return left.typeName < right.typeName;
        });
        materials.insert(
            materials.end(),
            std::make_move_iterator(sorted.begin()),
            std::make_move_iterator(sorted.end())
        );
    };
    appendSorted(byType);
    appendSorted(byLiquidType);
    return materials;
}

std::vector<MaterialRequirement> collectMaterials(
    std::vector<LoadedStructure::RenderBlock> const& renderBlocks,
    std::string_view localeCode
) {
    return resolveMaterials(collectRawMaterials(renderBlocks), localeCode);
}

std::optional<MaterialHudKey> currentMaterialHudKey() {
    return projection::getMaterialProgressKey();
}

std::optional<MaterialHudInput> captureMaterialHudInput(MaterialHudKey const& key) {
    return projection::captureMaterialProgress(key);
}

MaterialHudResult countMaterialHud(MaterialHudInput input) {
    MaterialHudResult result;
    result.key = input.key;
    auto const& blocks = input.structure->renderBlocks;
    result.counts.body.reserve(std::min<std::size_t>(blocks.size(), 4096));
    result.counts.liquid.reserve(std::min<std::size_t>(blocks.size(), 64));
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (input.progressCorrect[index] != 0) continue;
        auto const& entry = blocks[index];
        auto const layer = input.key.layerAxis == 1 ? entry.x : entry.y;
        if (!projection::isLayerVisible(
            layer, input.key.layerDisplayMode, input.key.displayLayer,
                entry.materialIndex, entry.liquidMaterialIndex, input.key.layerAxis
            )) {
            continue;
        }
        // Block pointers are opaque keys here. Registry, localization and item
        // resolution remain on the game tick thread after aggregation.
        countBlock(result.counts.body, entry.block);
        countBlock(result.counts.liquid, entry.liquid);
    }
    return result;
}

std::vector<int> collectInventoryAvailability(
    LocalPlayer&                            player,
    std::vector<MaterialRequirement> const& requirements
) {
    std::unordered_map<std::string, int> inventoryCounts;
    auto& inventory = player.getInventory();
    for (int slot = 0; slot < kInventorySlots; ++slot) {
        auto const& item = inventory.getItem(slot);
        if (!item.isNull()) inventoryCounts[item.getTypeName()] += static_cast<int>(item.mCount);
    }

    std::vector<int> available(requirements.size(), 0);
    for (std::size_t index = 0; index < requirements.size(); ++index) {
        auto const& itemId = requirements[index].itemId;
        if (itemId.empty()) continue;
        if (auto const found = inventoryCounts.find(itemId); found != inventoryCounts.end()) {
            available[index] = found->second;
        }
    }
    return available;
}

void updateMaterialHud(LocalPlayer& player) {
    auto& ui = StructureUiState::getInstance();
    auto& worker = materialHudWorkerState();
    if (!ui.materialHudEnabled()) return;

    if (worker.inFlight
        && worker.inFlight->wait_for(std::chrono::milliseconds{0}) == std::future_status::ready) {
        auto result = worker.inFlight->get();
        worker.inFlight.reset();
        if (auto const current = currentMaterialHudKey(); current && *current == result.key) {
            auto materials = resolveMaterials(
                std::move(result.counts), player.getLocaleCode()
            );
            auto available = collectInventoryAvailability(player, materials);
            // Publish both vectors under one lock. The render thread therefore
            // sees either the complete old snapshot or the complete new one.
            ui.replaceMaterialHudSnapshot(std::move(materials), std::move(available));
            worker.published = result.key;
        }
    }

    auto const key = currentMaterialHudKey();
    if (!key) {
        worker.published.reset();
        return;
    }
    if (worker.inFlight || (worker.published && *worker.published == *key)) return;
    auto const now = GetTickCount64();
    if (now < worker.nextScheduleAt) return;

    auto input = captureMaterialHudInput(*key);
    if (!input) return;

    worker.inFlight.emplace(std::async(
        std::launch::async,
        [input = std::move(*input)]() mutable { return countMaterialHud(std::move(input)); }
    ));
    worker.nextScheduleAt = now + kMaterialHudRecountIntervalMs;
}

void processPendingMaterialList(LocalPlayer& player) {
    auto& ui = StructureUiState::getInstance();
    if (!ui.consumeMaterialListRequest()) return;

    auto const loaded = StructureSession::getInstance().loaded();
    std::vector<MaterialRequirement> materials;
    if (loaded) materials = collectMaterials(loaded->renderBlocks, player.getLocaleCode());
    // Loading another structure can overlap this game-thread calculation. Never
    // publish a completed list for a structure that is no longer active.
    if (StructureSession::getInstance().loaded() != loaded) {
        ui.requestMaterialList();
        return;
    }
    ui.replaceMaterialRequirements(std::move(materials));
    // Cover the narrow hand-off where the active structure changes between the
    // identity check above and publishing the snapshot.
    if (StructureSession::getInstance().loaded() != loaded) {
        ui.clearMaterials();
        ui.requestMaterialList();
    }
}

void refreshAvailability(LocalPlayer& player) {
    static std::uint64_t lastRefreshMs{};
    auto& ui = StructureUiState::getInstance();
    if (!ui.materialHudEnabled()) return;

    auto const requirements = ui.materialHudSnapshot().requirements;
    if (requirements.empty()) return;
    auto const now = GetTickCount64();
    if (lastRefreshMs != 0 && now - lastRefreshMs < kAvailabilityRefreshMs) return;
    lastRefreshMs = now;

    ui.setMaterialHudAvailability(collectInventoryAvailability(player, requirements));
}

} // namespace

void requestMaterialListRefresh() {
    StructureUiState::getInstance().requestMaterialList();
}

void invalidateMaterialList() {
    auto& ui = StructureUiState::getInstance();
    ui.clearMaterials();
    auto& worker = materialHudWorkerState();
    worker.published.reset();
    worker.nextScheduleAt = 0;
}

void tickMaterialTracker(LocalPlayer& player) {
    processPendingMaterialList(player);
    updateMaterialHud(player);
    refreshAvailability(player);
}

void shutdownMaterialTracker() {
    auto& worker = materialHudWorkerState();
    if (worker.inFlight) {
        // The task owns only immutable structure data and performs finite CPU
        // work. Join before the DLL unloads so no worker can execute old code.
        worker.inFlight->wait();
        worker.inFlight.reset();
    }
    worker.published.reset();
    worker.nextScheduleAt = 0;
    StructureUiState::getInstance().clearMaterialHud();
}

} // namespace lholo::structure::detail
