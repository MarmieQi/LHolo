// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/MaterialTracker.h"
#include "structure/IndependentTaskFiles.h"
#include "structure/InventoryAvailability.h"
#include "structure/MaterialCounts.h"
#include "structure/MaterialDeltaState.h"
#include "structure/MaterialUnits.h"
#include "structure/TaskProjectionView.h"

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
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/registry/ItemRegistry.h"
#include "mc/world/item/registry/ItemRegistryManager.h"
#include "mc/world/level/block/Block.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/util/BlockUtils.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
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

constexpr std::uint64_t kAvailabilityRefreshMs = 400;
constexpr std::uint64_t kMaterialHudRecountIntervalMs = 400;
// File-load completion and task selection can invalidate from other threads.
// Only the player tick (and shutdown after producers stop) owns worker state.
std::atomic_bool materialInvalidated{};

using MaterialHudKey = projection::MaterialProgressKey;
using MaterialHudInput = projection::MaterialProgressSnapshot;

struct MaterialHudResult {
    MaterialHudKey     key;
    MaterialDeltaState delta;
};

struct ResolvedPlainBlock {
    std::string                      displayName;
    std::string                      itemId;
    int                              stackSize{64};
    std::shared_ptr<ItemStack const> itemPrototype;
};

struct MaterialHudWorkerState {
    std::optional<std::future<MaterialHudResult>> inFlight;
    std::optional<MaterialHudKey>                 published;
    std::optional<MaterialDeltaState>             hudDelta;
    std::uint64_t                                 nextScheduleAt{};
    std::weak_ptr<LoadedStructure const>           unitSource;
    std::shared_ptr<MaterialUnitOwners const>      unitOwners;
    bool                                          waitingListProgress{};
    std::optional<MaterialHudKey>                 publishedListKey;
    std::optional<MaterialDeltaState>             listDelta;
    MaterialListScope                             publishedListScope{MaterialListScope::All};
    std::uint64_t                                 nextListRefreshAt{};
    std::string                                   plainBlockLocale;
    std::unordered_map<Block const*, ResolvedPlainBlock> plainBlockCache;
};

MaterialHudWorkerState& materialHudWorkerState() {
    static MaterialHudWorkerState state;
    return state;
}

MaterialUnitOwners materialUnits(StructureBlocks const& blocks) {
    return buildMaterialUnits(blocks, [](Block const* value) { return block::placementPart(*value); });
}

std::shared_ptr<MaterialUnitOwners const> cachedMaterialUnits(
    std::shared_ptr<LoadedStructure const> const& structure
) {
    auto& worker = materialHudWorkerState();
    if (worker.unitSource.lock() != structure) {
        worker.unitOwners = std::make_shared<MaterialUnitOwners const>(materialUnits(structure->renderBlocks));
        worker.unitSource = structure;
    }
    return worker.unitOwners;
}

void saturatingAdd(std::uint64_t& target, std::uint64_t delta) {
    auto const maximum = std::numeric_limits<std::uint64_t>::max();
    target = delta > maximum - target ? maximum : target + delta;
}

std::string localizedBlockName(Block const& block, std::string_view localeCode) {
    auto const& typeName = block.getTypeName();
    auto const itemId = block.getBlockItemId();
    auto const item = ItemRegistryManager::getItemRegistry().getItem(itemId);
    if (auto* itemPtr = item.get()) {
        ItemStack itemStack;
        itemStack.reinit(*itemPtr, 1, 0);
        if (block::hasUsableItem(itemStack)) {
            auto const name = itemStack.getName();
            if (!name.empty() && name != typeName) return name;
        }
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
            saturatingAdd(sources[found->second].count, source.count);
            saturatingAdd(sources[found->second].missing, source.missing);
        }
    }
    return sources;
}

std::vector<MaterialRequirement> resolveMaterials(
    RawMaterialCounts counts,
    std::string_view localeCode,
    Level& level
) {
    auto& worker = materialHudWorkerState();
    if (worker.plainBlockLocale != localeCode) {
        worker.plainBlockCache.clear();
        worker.plainBlockLocale = std::string{localeCode};
    }
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
        } else if (!source.nbt) {
            auto [it, inserted] = worker.plainBlockCache.try_emplace(blockValue);
            if (inserted) {
                auto const item = block::resolvePlacementItem(*blockValue);
                if (item.valid) {
                    it->second.displayName = item.displayName;
                    it->second.itemId = item.itemId;
                    it->second.stackSize = item.stackSize;
                    it->second.itemPrototype = item.displayItem;
                } else {
                    it->second.displayName = localizedBlockName(*blockValue, localeCode);
                }
            }
            requirement.displayName = it->second.displayName;
            requirement.itemId = it->second.itemId;
            requirement.stackSize = it->second.stackSize;
            requirement.itemPrototype = it->second.itemPrototype;
        } else {
            auto const item = block::resolvePlacementItem(*blockValue, *source.nbt, level);
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
        saturatingAdd(found->count, blockCount);
        saturatingAdd(found->missing, source.missing);
    };

    // The worker only counts opaque source pointers. Native item resolution
    // stays on the game tick thread. Merge duplicate contents before resolving
    // and comparing items, rather than repeating that work for every position.
    for (auto const& source : coalesceMaterialSources(counts.body)) aggregate(byType, source);
    // Material demand is source blocks, not the volume generated by their flow.
    // Use the native predicate once per liquid state on the game thread; both
    // the full list and missing-material HUD pass through this resolver.
    for (auto const& source : counts.liquid.entries()) {
        if (BlockUtils::isLiquidSource(*source.block)) aggregate(byLiquidType, source);
    }

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

template <class IsIncluded, class IsMissing>
std::vector<MaterialRequirement> collectMaterials(
    std::shared_ptr<LoadedStructure const> const& structure,
    std::string_view localeCode,
    Level& level,
    IsIncluded included,
    IsMissing isMissing
) {
    auto const owners = cachedMaterialUnits(structure);
    return resolveMaterials(
        collectRawMaterials(structure->renderBlocks, *owners, included, isMissing),
        localeCode,
        level
    );
}

std::optional<MaterialHudKey> currentMaterialHudKey() {
    return projection::getMaterialProgressKey();
}

std::optional<MaterialHudInput> captureMaterialHudInput(MaterialHudKey const& key) {
    return projection::captureMaterialProgress(key);
}

MaterialHudResult countMaterialHud(MaterialHudInput input, MaterialUnitOwners const& owners) {
    MaterialHudResult result;
    result.key = input.key;
    auto const& blocks = input.structure->renderBlocks;
    result.delta.build(blocks, owners, input.progressCorrect, [&](std::size_t index) {
        if (input.taskView)
            return input.taskView->owns(input.taskFile, index);
        auto const& entry = blocks[index];
        auto const layer = input.key.layerAxis == LayerAxis::X ? entry.x : entry.y;
        return projection::isLayerVisible(
            layer, input.key.layerDisplayMode, input.key.displayLayer,
            entry.materialIndex, entry.liquidMaterialIndex, input.key.layerAxis
        );
    });
    return result;
}

void publishMaterialHudDelta(LocalPlayer& player, MaterialDeltaState const& delta) {
    auto& ui = StructureUiState::getInstance();
    auto materials = resolveMaterials(delta.exportCounts(), player.getLocaleCode(), player.getLevel());
    auto available = collectInventoryAvailability(player, materials);
    // Publish both vectors under one lock. The render thread therefore
    // sees either the complete old snapshot or the complete new one.
    ui.replaceMaterialHudSnapshot(std::move(materials), std::move(available));
}

void updateMaterialHud(LocalPlayer& player) {
    auto& ui = StructureUiState::getInstance();
    auto& worker = materialHudWorkerState();
    if (!ui.materialHudEnabled()) return;

    if (worker.inFlight
        && worker.inFlight->wait_for(std::chrono::milliseconds{0}) == std::future_status::ready) {
        auto result = worker.inFlight->get();
        worker.inFlight.reset();
        auto const finishedAt = GetTickCount64();
        worker.nextScheduleAt = std::max(worker.nextScheduleAt, finishedAt + kMaterialHudRecountIntervalMs);
        if (auto const current = currentMaterialHudKey();
            current
            && current->structureGeneration == result.key.structureGeneration
            && current->activationGeneration == result.key.activationGeneration
            && current->layerDisplayMode == result.key.layerDisplayMode
            && current->displayLayer == result.key.displayLayer
            && current->layerAxis == result.key.layerAxis) {
            worker.hudDelta = std::move(result.delta);
            worker.published = result.key;
            // Catch up any flips that arrived while the baseline was building.
            if (auto delta = projection::takeMaterialProgressDelta(*worker.published)) {
                for (auto const& flip : delta->flips) {
                    worker.hudDelta->applyFlip(flip.index, flip.correct);
                }
                worker.published = delta->key;
            }
            publishMaterialHudDelta(player, *worker.hudDelta);
        }
    }

    auto const key = currentMaterialHudKey();
    if (!key) {
        worker.published.reset();
        worker.hudDelta.reset();
        worker.unitSource.reset();
        worker.unitOwners.reset();
        return;
    }
    if (worker.inFlight || (worker.published && *worker.published == *key)) return;

    // Fast path: apply O(1) cell flips immediately without waiting for the
    // 400 ms full-rescan throttle or spawning a background worker.
    if (worker.published && worker.hudDelta) {
        if (auto delta = projection::takeMaterialProgressDelta(*worker.published)) {
            bool changed{};
            for (auto const& flip : delta->flips) {
                changed = worker.hudDelta->applyFlip(flip.index, flip.correct) || changed;
            }
            worker.published = delta->key;
            if (changed) publishMaterialHudDelta(player, *worker.hudDelta);
            return;
        }
        worker.hudDelta.reset();
    }

    auto const now = GetTickCount64();
    if (now < worker.nextScheduleAt) return;

    auto input = captureMaterialHudInput(*key);
    if (!input) return;

    auto owners = cachedMaterialUnits(input->structure);

    worker.inFlight.emplace(std::async(
        std::launch::async,
        [input = std::move(*input), owners = std::move(owners)]() mutable {
            return countMaterialHud(std::move(input), *owners);
        }
    ));
    worker.nextScheduleAt = now + kMaterialHudRecountIntervalMs;
}

void publishMaterialListDelta(LocalPlayer& player, MaterialDeltaState const& delta, MaterialListScope scope) {
    auto materials = resolveMaterials(delta.exportFullCounts(), player.getLocaleCode(), player.getLevel());
    auto const available = collectInventoryAvailability(player, materials);
    for (std::size_t index = 0; index < materials.size(); ++index) {
        materials[index].available = available[index];
    }
    StructureUiState::getInstance().replaceMaterialRequirements(std::move(materials), true, scope);
}

void processPendingMaterialList(LocalPlayer& player) {
    auto& ui = StructureUiState::getInstance();
    if (!ui.consumeMaterialListRequest()) return;

    auto& worker = materialHudWorkerState();
    auto const scope = ui.requestedMaterialListScope();
    auto const session = StructureSession::getInstance().snapshot();
    auto const& loaded = session.loaded;
    auto const source = session.selectedSource;
    auto const& transform = session.transform;
    std::shared_ptr<LoadedStructure const> targetStructure;
    std::optional<TaskProjectionView> taskView;
    if (loaded && loaded->independentFiles) {
        if (auto const* file = independentTaskFile(*loaded, source); file && file->visible && file->loaded) {
            targetStructure = file->loaded;
            auto files = loaded->independentFiles->files;
            auto& selectedFile = files[static_cast<std::size_t>(source)];
            if (scope == MaterialListScope::RenderRange) {
                selectedFile.slice = LayerSettings{transform.layerAxis, transform.layerDisplayMode, transform.displayLayer};
            } else {
                selectedFile.slice = LayerSettings{};
            }
            taskView.emplace(files);
        }
    } else if (loaded) {
        targetStructure = loaded;
    }

    bool progressReady{};
    std::optional<MaterialHudKey> usedKey;
    std::optional<MaterialDeltaState> builtDelta;
    std::vector<MaterialRequirement> materials;
    if (targetStructure) {
        std::optional<MaterialHudInput> progressSnapshot;
        if (auto const key = currentMaterialHudKey()) {
            if (auto captured = captureMaterialHudInput(*key);
                captured && captured->structure == targetStructure
                && captured->progressCorrect.size() == targetStructure->renderBlocks.size()) {
                usedKey = captured->key;
                progressSnapshot = std::move(captured);
            }
        }
        progressReady = progressSnapshot.has_value();
        auto buildWithFilter = [&](auto included) {
            if (progressSnapshot) {
                auto const owners = cachedMaterialUnits(targetStructure);
                MaterialDeltaState delta;
                delta.build(targetStructure->renderBlocks, *owners, progressSnapshot->progressCorrect, included);
                materials = resolveMaterials(delta.exportFullCounts(), player.getLocaleCode(), player.getLevel());
                builtDelta = std::move(delta);
            } else {
                materials = collectMaterials(
                    targetStructure, player.getLocaleCode(), player.getLevel(),
                    included,
                    [](std::size_t) { return false; }
                );
            }
        };
        if (taskView) {
            buildWithFilter([&](std::size_t index) { return taskView->owns(static_cast<std::size_t>(source), index); });
        } else if (scope == MaterialListScope::RenderRange) {
            auto const& blocks = targetStructure->renderBlocks;
            buildWithFilter([&](std::size_t index) {
                auto const& entry = blocks[index];
                auto const layer = transform.layerAxis == LayerAxis::X ? entry.x : entry.y;
                return projection::isLayerVisible(
                    layer, transform.layerDisplayMode, transform.displayLayer,
                    entry.materialIndex, entry.liquidMaterialIndex, transform.layerAxis
                );
            });
        } else {
            buildWithFilter([](std::size_t) { return true; });
        }
        auto const available = collectInventoryAvailability(player, materials);
        for (std::size_t index = 0; index < materials.size(); ++index) {
            materials[index].available = available[index];
        }
    }
    worker.waitingListProgress = targetStructure && !progressReady;
    worker.publishedListKey = usedKey;
    worker.listDelta = std::move(builtDelta);
    worker.publishedListScope = scope;
    worker.nextListRefreshAt = GetTickCount64() + kAvailabilityRefreshMs;

    // Loading another structure can overlap this game-thread calculation. Never
    // publish a completed list for a structure that is no longer active.
    if (StructureSession::getInstance().loaded() != loaded || StructureSession::getInstance().selectedSource() != source) {
        worker.listDelta.reset();
        ui.requestMaterialList(scope);
        return;
    }
    ui.replaceMaterialRequirements(std::move(materials), progressReady, scope);
    // Cover the narrow hand-off where the active structure changes between the
    // identity check above and publishing the snapshot.
    if (StructureSession::getInstance().loaded() != loaded || StructureSession::getInstance().selectedSource() != source) {
        worker.listDelta.reset();
        ui.clearMaterials();
        ui.requestMaterialList(scope);
    }
}

void refreshOpenMaterialList(LocalPlayer& player) {
    auto& ui = StructureUiState::getInstance();
    if (!ui.materialListOpen()) return;

    auto& worker = materialHudWorkerState();
    auto const now = GetTickCount64();
    auto const key = currentMaterialHudKey();

    // Fast path: apply O(1) cell flips immediately without waiting for the
    // 400 ms inventory refresh interval or running a full structure rescan.
    if (key && worker.publishedListKey && *worker.publishedListKey != *key) {
        bool const requireSameLayer = worker.publishedListScope == MaterialListScope::RenderRange;
        if (worker.listDelta) {
            if (auto delta = projection::takeMaterialProgressDelta(*worker.publishedListKey, requireSameLayer)) {
                bool changed{};
                for (auto const& flip : delta->flips) {
                    changed = worker.listDelta->applyFlip(flip.index, flip.correct) || changed;
                }
                worker.publishedListKey = delta->key;
                if (changed) {
                    publishMaterialListDelta(player, *worker.listDelta, worker.publishedListScope);
                    worker.nextListRefreshAt = now + kAvailabilityRefreshMs;
                }
                return;
            }
            worker.listDelta.reset();
        }
        ui.requestMaterialList(ui.requestedMaterialListScope());
        worker.nextListRefreshAt = now + kAvailabilityRefreshMs;
        return;
    }
    if (!key && worker.publishedListKey) {
        worker.listDelta.reset();
        worker.publishedListKey.reset();
    }

    if (now < worker.nextListRefreshAt) return;
    worker.nextListRefreshAt = now + kAvailabilityRefreshMs;

    if (key && !worker.publishedListKey) {
        ui.requestMaterialList(ui.requestedMaterialListScope());
        return;
    }

    auto const snapshot = ui.materialListSnapshotIfChanged(std::nullopt);
    if (!snapshot || !snapshot->ready || snapshot->requirements.empty()) return;
    ui.updateMaterialListAvailability(collectInventoryAvailability(player, snapshot->requirements));
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

void requestMaterialListRefresh(bool renderRange) {
    StructureUiState::getInstance().requestMaterialList(
        renderRange ? MaterialListScope::RenderRange : MaterialListScope::All
    );
}

void invalidateMaterialList() {
    auto& ui = StructureUiState::getInstance();
    ui.clearMaterials();
    materialInvalidated.store(true, std::memory_order_release);
}

void tickMaterialTracker(LocalPlayer& player) {
    auto& worker = materialHudWorkerState();
    if (materialInvalidated.exchange(false, std::memory_order_acq_rel)) {
        worker.published.reset();
        worker.hudDelta.reset();
        worker.nextScheduleAt = 0;
        worker.waitingListProgress = false;
        worker.publishedListKey.reset();
        worker.listDelta.reset();
        worker.nextListRefreshAt = 0;
    }
    if (worker.waitingListProgress && currentMaterialHudKey()) {
        worker.waitingListProgress = false;
        auto& ui = StructureUiState::getInstance();
        ui.requestMaterialList(ui.requestedMaterialListScope());
    }
    refreshOpenMaterialList(player);
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
    worker.hudDelta.reset();
    worker.nextScheduleAt = 0;
    worker.unitSource.reset();
    worker.unitOwners.reset();
    worker.waitingListProgress = false;
    worker.publishedListKey.reset();
    worker.listDelta.reset();
    worker.nextListRefreshAt = 0;
    worker.plainBlockCache.clear();
    worker.plainBlockLocale.clear();
    materialInvalidated.store(false, std::memory_order_relaxed);
    StructureUiState::getInstance().clearMaterialHud();
}

} // namespace lholo::structure::detail
