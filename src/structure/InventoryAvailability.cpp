// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/InventoryAvailability.h"

#include "block/BlockPlacementRules.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/containers/IContainerRegistryAccess.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/components/storage_item_utility/StorageItemUtility.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/BlockType.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lholo::structure::detail {
namespace {

constexpr int kInventorySlots = 36;
constexpr int kMaxContainerDepth = 4;
constexpr std::string_view kShulkerItemsTag = "Items";
constexpr std::string_view kBundleContentTag = "storage_item_component_content";
constexpr std::string_view kBundleIdTag = "storage_item_component_id";

using RequirementLookup = std::unordered_map<std::string_view, std::vector<std::size_t>>;

struct ScanContext {
    Level&                                  level;
    IContainerRegistryAccess&               containers;
    std::vector<MaterialRequirement> const& requirements;
    RequirementLookup const&                byItemId;
    std::vector<int>&                       available;
};

bool isShulkerBox(ItemStack const& item) {
    if (!item.mItem) return false;
    auto const* type = item.mItem->getBlockTypeForRendering();
    return type && type->getBlockEntityType() == BlockActorType::ShulkerBox;
}

bool hasNestedContainerTag(CompoundTag const& itemTag) {
    auto const userDataIt = itemTag.mTags.find("tag");
    if (userDataIt == itemTag.mTags.end() || !userDataIt->second.hold<CompoundTag>()) return false;
    auto const& userData = userDataIt->second.get<CompoundTag>();
    return userData.mTags.contains(kBundleContentTag)
        || userData.mTags.contains(kBundleIdTag)
        || userData.mTags.contains(kShulkerItemsTag);
}

void collectFromItem(ItemStack const& item, ScanContext& ctx, int depth);

void collectFromTagList(ListTag const& list, ScanContext& ctx, int depth) {
    for (std::size_t index = 0; index < list.size(); ++index) {
        auto const& entry = list[index];
        if (!entry.hold<CompoundTag>()) continue;
        auto const& tag = entry.get<CompoundTag>();

        bool matchesNeeded = false;
        if (auto const nameIt = tag.mTags.find("Name");
            nameIt != tag.mTags.end() && nameIt->second.hold<StringTag>()) {
            auto const& name = static_cast<std::string const&>(nameIt->second.get<StringTag>());
            matchesNeeded = ctx.byItemId.contains(name);
        }
        if (!matchesNeeded && !hasNestedContainerTag(tag)) continue;

        auto stack = ItemStack::fromTag(tag, ctx.level);
        if (block::hasUsableItem(stack)) {
            collectFromItem(stack, ctx, depth);
        }
    }
}

void collectFromItem(ItemStack const& item, ScanContext& ctx, int depth) {
    if (!block::hasUsableItem(item)) return;

    auto const typeName = item.getTypeName();
    if (auto const found = ctx.byItemId.find(typeName); found != ctx.byItemId.end()) {
        for (auto const reqIndex : found->second) {
            auto const& prototype = ctx.requirements[reqIndex].itemPrototype;
            if (prototype && block::sameMaterialItem(*prototype, item)) {
                ctx.available[reqIndex] += static_cast<int>(item.mCount);
            }
        }
    }

    if (depth >= kMaxContainerDepth || item.mCount != 1) return;

    if (isShulkerBox(item) && item.mUserData) {
        if (auto const stored = item.mUserData->mTags.find(kShulkerItemsTag);
            stored != item.mUserData->mTags.end() && stored->second.hold<ListTag>()) {
            collectFromTagList(stored->second.get<ListTag>(), ctx, depth + 1);
        }
        return;
    }

    if (auto const storageId = StorageItemUtility::getStorageItemID(item)) {
        if (auto const container = ctx.containers.getBackingContainer(*storageId)) {
            auto const size = container->getContainerSize();
            for (int slot = 0; slot < size; ++slot) {
                collectFromItem(container->getItem(slot), ctx, depth + 1);
            }
            return;
        }
    }

    if (item.mUserData) {
        if (auto const stored = item.mUserData->mTags.find(kBundleContentTag);
            stored != item.mUserData->mTags.end() && stored->second.hold<ListTag>()) {
            collectFromTagList(stored->second.get<ListTag>(), ctx, depth + 1);
        }
    }
}

} // namespace

std::vector<int> collectInventoryAvailability(
    LocalPlayer&                            player,
    std::vector<MaterialRequirement> const& requirements
) {
    std::vector<int> available(requirements.size(), 0);
    if (requirements.empty()) return available;

    RequirementLookup byItemId;
    byItemId.reserve(requirements.size());
    for (std::size_t index = 0; index < requirements.size(); ++index) {
        if (!requirements[index].itemId.empty()) {
            byItemId[requirements[index].itemId].push_back(index);
        }
    }
    if (byItemId.empty()) return available;

    auto& level = player.getLevel();
    auto const registryAccess = level.getContainerRegistryAccess();
    ScanContext ctx{level, *registryAccess, requirements, byItemId, available};

    auto& inventory = player.getInventory();
    for (int slot = 0; slot < kInventorySlots; ++slot) {
        collectFromItem(inventory.getItem(slot), ctx, 0);
    }
    return available;
}

} // namespace lholo::structure::detail
