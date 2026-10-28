// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/MaterialTracker.h"
#include "structure/IndependentTaskFiles.h"

#include "block/BlockPlacementRules.h"
#include "projection/Projection.h"
#include "structure/StructureLoader.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"
#include "i18n/Translator.h"

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
#include "mc/world/level/block/VanillaStates.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/nbt/Tag.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <future>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
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

// A palette state plus the item variant its block entity selects. Banners are
// one block state for every design: the base colour (the banner item's aux
// value), the ominous type and the pattern layers live in the block entity,
// and each design is a different item to make. variant is -1 for non-banners.
struct CountedBlock {
    Block const*  block{};
    int           variant{-1};
    int           bannerType{};
    int           patternCount{};
    std::uint64_t patternHash{};
    bool operator==(CountedBlock const&) const = default;
};

struct CountedBlockHash {
    std::size_t operator()(CountedBlock const& value) const noexcept {
        auto hash = std::hash<Block const*>{}(value.block);
        hash ^= static_cast<std::size_t>(value.variant + 1) * 0x9E3779B97F4A7C15ULL;
        hash ^= static_cast<std::size_t>(value.bannerType) << 7;
        return hash ^ static_cast<std::size_t>(value.patternHash);
    }
};

using BlockCounts = std::unordered_map<CountedBlock, std::uint64_t, CountedBlockHash>;

struct RawMaterialCounts {
    BlockCounts body;
    BlockCounts liquid;
    // One representative per existing material/palette identity, not one
    // native actor per projected cell. Keep source NBT immutable.
    // Keyed by design, so each banner design keeps its own sample.
    std::unordered_map<CountedBlock, std::shared_ptr<CompoundTag const>, CountedBlockHash> blockEntitySamples;
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

bool isBanner(Block const& blockValue) {
    auto const& typeName = blockValue.getTypeName();
    return typeName == "minecraft:standing_banner" || typeName == "minecraft:wall_banner";
}

int intField(CompoundTag const& tag, std::string_view name, int fallback) {
    return tag.contains(name, Tag::Type::Int) ? tag.at(name).get<IntTag>().data : fallback;
}

// The ominous type and pattern layers of a banner, as stored both in a banner
// block entity and in a banner item's user data.
void readBannerDesign(CompoundTag const* tag, CountedBlock& counted) {
    if (!tag) return;
    counted.bannerType = intField(*tag, "Type", 0);
    if (!tag->contains("Patterns", Tag::Type::List)) return;
    // FNV-1a over each layer's pattern code and colour.
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](std::string_view bytes) {
        for (auto const byte : bytes) hash = (hash ^ static_cast<unsigned char>(byte)) * 1099511628211ULL;
    };
    for (auto const& layer : tag->at("Patterns").get<ListTag>()) {
        if (!layer || layer->getId() != Tag::Type::Compound) continue;
        auto const& pattern = static_cast<CompoundTag const&>(*layer);
        if (pattern.contains("Pattern", Tag::Type::String)) mix(pattern.at("Pattern").get<StringTag>());
        mix(std::to_string(intField(pattern, "Color", 0)) + ";");
        ++counted.patternCount;
    }
    counted.patternHash = counted.patternCount > 0 ? hash : 0;
}

std::string designKey(CountedBlock const& counted) {
    return std::to_string(counted.variant) + "/" + std::to_string(counted.bannerType) + "/"
        + std::to_string(counted.patternHash);
}

CountedBlock countedBody(LoadedStructure::RenderBlock const& entry) {
    CountedBlock counted{entry.block};
    if (!entry.block || !isBanner(*entry.block)) return counted;
    counted.variant = 0;
    if (!entry.blockEntityNbt) return counted;
    counted.variant = intField(*entry.blockEntityNbt, "Base", 0);
    readBannerDesign(entry.blockEntityNbt.get(), counted);
    return counted;
}

void countBlock(BlockCounts& counts, CountedBlock const& counted) {
    if (!counted.block) return;
    auto& count = counts[counted];
    if (count != std::numeric_limits<std::uint64_t>::max()) ++count;
}

void countBlock(BlockCounts& counts, Block const* blockValue) {
    countBlock(counts, CountedBlock{blockValue});
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
        auto const counted = countedBody(entry);
        countBlock(counts.body, counted);
        if (entry.block && entry.blockEntityNbt) counts.blockEntitySamples.try_emplace(counted, entry.blockEntityNbt);
        countBlock(counts.liquid, entry.liquid);
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

std::vector<MaterialRequirement> resolveMaterials(
    RawMaterialCounts counts,
    std::string_view localeCode,
    Level& level
) {
    std::map<std::string, MaterialRequirement> byType;
    std::map<std::string, MaterialRequirement> byLiquidType;
    auto aggregate = [&](auto& destination, CountedBlock const& counted, std::uint64_t blockCount) {
        Block const* blockValue = counted.block;
        int const variant = counted.variant;
        if (!blockValue || blockCount == 0) return;

        std::string const typeName{blockValue->getTypeName()};
        auto key = block::materialKey(typeName);
        if (key.empty()) return;
        bool const water = typeName == "minecraft:water" || typeName == "minecraft:flowing_water";
        bool const lava = typeName == "minecraft:lava" || typeName == "minecraft:flowing_lava";
        MaterialRequirement requirement;
        requirement.typeName = typeName;
        if (water || lava) {
            // Only a source needs a bucket; the flowing cells around it fill
            // themselves. Sources in the block layer and in the liquid layer
            // are the same material, so both go to the liquid rows.
            if (blockValue->getState<int>(VanillaStates::LiquidDepth()).value_or(0) != 0) return;
            requirement.nameKey = water ? i18n::TextKey::MaterialWater : i18n::TextKey::MaterialLava;
            auto const result = byLiquidType.try_emplace(key, std::move(requirement));
            auto& total = result.first->second.count;
            auto const maximum = std::numeric_limits<std::uint64_t>::max();
            total = blockCount > maximum - total ? maximum : total + blockCount;
            return;
        }
        auto const sample = counts.blockEntitySamples.find(counted);
        auto const* nbt = sample == counts.blockEntitySamples.end() ? nullptr : sample->second.get();
        if (auto const item = (nbt ? block::resolvePlacementItem(*blockValue, *nbt, level) : block::resolvePlacementItem(*blockValue)); item.valid) {
            requirement.displayName = item.displayName;
            requirement.itemId = item.itemId;
            requirement.stackSize = item.stackSize;
            requirement.iconItem = item.displayItem;
            // Blocks with separate wall and standing forms (banners, signs,
            // coral fans) are one inventory item: group by that item.
            key = "item:" + item.itemId;
            if (variant >= 0) {
                // A banner's colour is the item's aux value; each design is
                // its own material, named by its own item (an ominous banner
                // is named by the game from its Type).
                ItemStack design;
                design.reinit(item.itemId, 1, static_cast<short>(variant));
                if (!design.isNull()) {
                    if (counted.bannerType != 0) {
                        auto userData = std::make_unique<CompoundTag>();
                        userData->putInt("Type", counted.bannerType);
                        design.setUserData(std::move(userData));
                    }
                    requirement.displayName = block::stripMinecraftFormatting(design.getHoverName());
                }
                if (counted.bannerType == 0 && counted.patternCount > 0) {
                    char suffix[96]{};
                    std::snprintf(
                        suffix, sizeof(suffix), i18n::tr(i18n::TextKey::MaterialBannerPatterns), counted.patternCount
                    );
                    requirement.displayName += suffix;
                }
                requirement.variantKey = designKey(counted);
                key += "#" + requirement.variantKey;
            }
        } else {
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
    for (auto const& [counted, count] : counts.body) aggregate(byType, counted, count);
    for (auto const& [counted, count] : counts.liquid) {
        aggregate(byLiquidType, counted, count);
    }

    std::vector<MaterialRequirement> materials;
    materials.reserve(byType.size() + byLiquidType.size());
    auto appendSorted = [&materials](auto& source) {
        std::vector<std::pair<std::string, MaterialRequirement>> sorted;
        sorted.reserve(source.size());
        for (auto& [key, requirement] : source) {
            sorted.emplace_back(key, std::move(requirement));
        }
        std::sort(sorted.begin(), sorted.end(), [](auto const& left, auto const& right) {
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
        auto const counted = countedBody(entry);
        countBlock(result.counts.body, counted);
        if (entry.block && entry.blockEntityNbt) result.counts.blockEntitySamples.try_emplace(counted, entry.blockEntityNbt);
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
        if (item.isNull()) continue;
        std::string key = item.getTypeName();
        if (key == "minecraft:banner") {
            // Banners only count toward the material of their own design.
            CountedBlock design;
            design.variant = item.getAuxValue();
            readBannerDesign(item.mUserData.get(), design);
            key += "#" + designKey(design);
        }
        inventoryCounts[key] += static_cast<int>(item.mCount);
    }

    std::vector<int> available(requirements.size(), 0);
    for (std::size_t index = 0; index < requirements.size(); ++index) {
        auto const& requirement = requirements[index];
        if (requirement.itemId.empty()) continue;
        auto const key = requirement.variantKey.empty()
            ? requirement.itemId
            : requirement.itemId + "#" + requirement.variantKey;
        if (auto const found = inventoryCounts.find(key); found != inventoryCounts.end()) {
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
