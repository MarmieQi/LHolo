// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/MaterialTracker.h"
#include "structure/MaterialCounts.h"
#include "structure/IndependentTaskFiles.h"

#include "block/BlockPlacementRules.h"
#include "projection/Projection.h"
#include "structure/StructureLoader.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/locale/I18n.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/registry/ItemRegistry.h"
#include "mc/world/item/registry/ItemRegistryManager.h"
#include "mc/world/level/block/Block.h"
#include "mc/deps/nbt/CompoundTag.h"

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

struct RawMaterialCounts {
    MaterialCounts body;
    MaterialCounts liquid;
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

RawMaterialCounts collectRawMaterials(
    std::vector<LoadedStructure::RenderBlock> const& renderBlocks
) {
    RawMaterialCounts counts;
    // Palette cardinality is normally tiny compared with the block count. Do
    // not reserve one hash bucket per projected cell for very large structures.
    counts.body.reserve(std::min<std::size_t>(renderBlocks.size(), 4096));
    counts.liquid.reserve(std::min<std::size_t>(renderBlocks.size(), 64));
    for (auto const& entry : renderBlocks) {
        counts.body.add(entry.block, entry.blockEntityNbt);
        counts.liquid.add(entry.liquid);
    }
    return counts;
}

std::string localizedBlockName(Block const& block, std::string_view localeCode) {
    auto const& typeName = block.getTypeName();
    auto const itemId = block.getBlockItemId();
    auto const item = ItemRegistryManager::getItemRegistry().getItem(itemId);
    if (auto* itemPtr = item.get()) {
        ItemStack itemStack;
        itemStack.reinit(*itemPtr, 1, 0);
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

std::vector<MaterialSourceCount> coalesceMaterialSources(MaterialCounts const& counts) {
    std::vector<MaterialSourceCount> sources;
    std::unordered_map<Block const*, std::unordered_map<std::string, std::size_t>> byContents;
    for (auto const& source : counts.entries()) {
        if (!source.nbt) {
            // Plain blocks are already counted once per palette state.
            sources.push_back(source);
            continue;
        }
        CompoundTag identity{*source.nbt};
        for (auto const* coordinate : {"x", "y", "z"}) identity.mTags.erase(coordinate);
        auto const [found, inserted] = byContents[source.block].try_emplace(identity.toBinaryNbt(), sources.size());
        if (inserted) sources.push_back(source);
        else {
            auto& total = sources[found->second].count;
            auto const maximum = std::numeric_limits<std::uint64_t>::max();
            total = source.count > maximum - total ? maximum : total + source.count;
        }
    }
    return sources;
}

std::vector<MaterialRequirement> resolveMaterials(
    RawMaterialCounts counts,
    std::string_view localeCode,
    Level& level
) {
    using Groups = std::map<std::string, std::vector<MaterialRequirement>>;
    Groups byType;
    Groups byLiquidType;
    auto aggregate = [&](auto& destination, MaterialSourceCount const& source) {
        auto const* blockValue = source.block;
        auto const blockCount = source.count;
        if (!blockValue || blockCount == 0) return;

        std::string const typeName{blockValue->getTypeName()};
        auto const key = block::materialKey(typeName);
        if (key.empty()) return;
        MaterialRequirement requirement;
        requirement.typeName = typeName;
        if (typeName == "minecraft:water" || typeName == "minecraft:flowing_water") {
            requirement.nameKey = i18n::TextKey::MaterialWater;
        } else if (typeName == "minecraft:lava" || typeName == "minecraft:flowing_lava") {
            requirement.nameKey = i18n::TextKey::MaterialLava;
        } else {
            auto const item = source.nbt
                ? block::resolvePlacementItem(*blockValue, *source.nbt, level)
                : block::resolvePlacementItem(*blockValue);
            if (item.valid) {
                requirement.displayName = item.displayName;
                requirement.itemId = item.itemId;
                requirement.stackSize = item.stackSize;
                requirement.itemPrototype = item.displayItem;
            } else requirement.displayName = localizedBlockName(*blockValue, localeCode);
        }

        // A block name is only the fallback identity. The native pick result
        // distinguishes colors/patterns/types, and merges standing/wall forms.
        auto& variants = destination[requirement.itemPrototype ? "item:" + requirement.itemId : key];
        auto found = std::find_if(variants.begin(), variants.end(), [&](auto const& existing) {
            if (requirement.itemPrototype && existing.itemPrototype)
                return block::sameMaterialItem(*requirement.itemPrototype, *existing.itemPrototype);
            return !requirement.itemPrototype && !existing.itemPrototype;
        });
        if (found == variants.end()) {
            variants.push_back(std::move(requirement));
            found = std::prev(variants.end());
        }
        auto& total = found->count;
        auto const maximum = std::numeric_limits<std::uint64_t>::max();
        total = blockCount > maximum - total ? maximum : total + blockCount;
    };

    // The worker only counts opaque source pointers. Native item resolution
    // stays on the game tick thread. Merge duplicate contents before resolving
    // and comparing items, rather than repeating that work for every position.
    for (auto const& source : coalesceMaterialSources(counts.body)) aggregate(byType, source);
    for (auto const& source : counts.liquid.entries()) aggregate(byLiquidType, source);

    std::vector<MaterialRequirement> materials;
    materials.reserve(byType.size() + byLiquidType.size());
    auto appendSorted = [&materials](auto& source) {
        std::vector<std::pair<std::string, MaterialRequirement>> sorted;
        sorted.reserve(source.size());
        for (auto& [key, variants] : source) {
            for (auto& requirement : variants)
                sorted.emplace_back(key, std::move(requirement));
        }
        std::stable_sort(sorted.begin(), sorted.end(), [](auto const& left, auto const& right) {
            if (left.second.count != right.second.count) {
                return left.second.count > right.second.count;
            }
            return left.first < right.first;
        });
        for (auto& [key, requirement] : sorted) {
            (void)key;
            materials.push_back(std::move(requirement));
        }
    };
    appendSorted(byType);
    appendSorted(byLiquidType);
    return materials;
}

std::vector<MaterialRequirement> collectMaterials(
    std::vector<LoadedStructure::RenderBlock> const& renderBlocks,
    std::string_view localeCode,
    Level& level
) {
    return resolveMaterials(collectRawMaterials(renderBlocks), localeCode, level);
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
        if (input.structure->composed && entry.sourceIndex!=input.key.selectedSource) continue;
        auto const layer = input.key.layerAxis == LayerAxis::X ? entry.x : entry.y;
        if (!projection::isLayerVisible(
            layer, input.key.layerDisplayMode, input.key.displayLayer,
                entry.materialIndex, entry.liquidMaterialIndex, input.key.layerAxis
            )) {
            continue;
        }
        // Block pointers are opaque keys here. Registry, localization and item
        // resolution remain on the game tick thread after aggregation.
        result.counts.body.add(entry.block, entry.blockEntityNbt);
        result.counts.liquid.add(entry.liquid);
    }
    return result;
}

std::vector<int> collectInventoryAvailability(
    LocalPlayer&                            player,
    std::vector<MaterialRequirement> const& requirements
) {
    std::unordered_map<std::string, std::vector<ItemStack const*>> inventoryItems;
    auto& inventory = player.getInventory();
    for (int slot = 0; slot < kInventorySlots; ++slot) {
        auto const& item = inventory.getItem(slot);
        if (!item.isNull()) inventoryItems[item.getTypeName()].push_back(&item);
    }

    std::vector<int> available(requirements.size(), 0);
    for (std::size_t index = 0; index < requirements.size(); ++index) {
        auto const& itemId = requirements[index].itemId;
        if (itemId.empty()) continue;
        if (auto const found = inventoryItems.find(itemId); found != inventoryItems.end()) {
            auto const& prototype = requirements[index].itemPrototype;
            for (auto const* item : found->second)
                if (prototype && block::sameMaterialItem(*prototype, *item))
                    available[index] += static_cast<int>(item->mCount);
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
                std::move(result.counts), player.getLocaleCode(), player.getLevel()
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
    auto const source=StructureSession::getInstance().selectedSource();
    std::vector<MaterialRequirement> materials;
    if (loaded && loaded->composed) {
        std::vector<LoadedStructure::RenderBlock> selected;
        if (auto const* file=independentTaskFile(*loaded,source); file && file->visible && file->loaded) {
            for (auto const& block:file->loaded->renderBlocks)
                if (projection::isLayerVisible(file->slice.axis==LayerAxis::X ? block.x : block.y,
                    file->slice.mode,file->slice.layer,block.materialIndex,block.liquidMaterialIndex,file->slice.axis))
                    selected.push_back(block);
        } else for (auto const& block:loaded->renderBlocks)
            if (block.sourceIndex==source) selected.push_back(block);
        materials=collectMaterials(selected,player.getLocaleCode(), player.getLevel());
    } else if (loaded) materials = collectMaterials(loaded->renderBlocks, player.getLocaleCode(), player.getLevel());
    // Loading another structure can overlap this game-thread calculation. Never
    // publish a completed list for a structure that is no longer active.
    if (StructureSession::getInstance().loaded() != loaded || StructureSession::getInstance().selectedSource()!=source) {
        ui.requestMaterialList();
        return;
    }
    ui.replaceMaterialRequirements(std::move(materials));
    // Cover the narrow hand-off where the active structure changes between the
    // identity check above and publishing the snapshot.
    if (StructureSession::getInstance().loaded() != loaded || StructureSession::getInstance().selectedSource()!=source) {
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
