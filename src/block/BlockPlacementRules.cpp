// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "block/BlockPlacementRules.h"
#include "mc/dataloadhelper/NewUniqueIdsDataLoadHelper.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/VanillaBlockActorFactory.h"

#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/SaveContext.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/BannerItem.h"
#include "mc/world/level/block/actor/BannerBlockActor.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/Direction.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/components/BlockMultiBlockComponent.h"
#include "mc/world/level/block/states/BuiltInBlockStates.h"

#include <cstddef>
#include <utility>

namespace lholo::block {

PlacementPart placementPart(Block const& block) {
    auto const* type = &block.getBlockType();
    if (auto const* multi = block.tryGetComponent<BlockMultiBlockComponent>()) {
        auto const offset = multi->getDistanceToMultiBlockStart(block);
        return {type, {offset.x, offset.y, offset.z},
            static_cast<int>(BlockMultiBlockComponent::getMultiBlockPartId(block))};
    }
    // These native states identify placement halves. getSecondPart alone is
    // unsuitable: connected chests also implement it but consume two items.
    if (auto const upper = block.getState<bool>(VanillaStates::UpperBlockBit()))
        return {type, {0, *upper ? -1 : 0, 0}, *upper ? 1 : 0};
    if (auto const head = block.getState<bool>(VanillaStates::HeadPieceBit())) {
        auto direction = block.getState<Direction::Type>(BuiltInBlockStates::CardinalDirection());
        if (!direction) {
            if (auto const legacy = block.getState<int>(VanillaStates::Direction()))
                direction = static_cast<Direction::Type>(*legacy);
        }
        if (direction && *direction < Direction::Type::Count) {
            auto const index = static_cast<unsigned>(*direction);
            return {type,
                {*head ? -Direction::STEP_X()[index] : 0, 0,
                 *head ? -Direction::STEP_Z()[index] : 0},
                *head ? 1 : 0, static_cast<int>(*direction)};
        }
    }
    return {};
}

bool hasUsableItem(ItemStackBase const& item) {
    return item.mCount > 0 && item.mItem.get() != nullptr && !item.isNull();
}

bool sameMaterialItem(ItemStack const& left, ItemStack const& right) {
    if (!hasUsableItem(left) || !hasUsableItem(right)) return false;
    auto const type = left.getTypeName();
    if (type != right.getTypeName() || !left.hasSameAuxValue(right)) return false;
    if (type == "minecraft:banner") {
        // Native reads normalize omitted/default banner fields. Unrelated
        // user data (for example a display name) does not change the material.
        return BannerBlockActor::getBannerType(left.mUserData.get())
                == BannerBlockActor::getBannerType(right.mUserData.get())
            && BannerItem::getPatternAndColorPairsFromItemStack(left)
                == BannerItem::getPatternAndColorPairsFromItemStack(right);
    }
    return left.hasSameUserData(right);
}

ItemStack makePlacementItem(Block const& block) {
    std::string const blockName{placeableBaseName(block.getTypeName())};
    auto const itemName = placementItemName(blockName);
    std::string_view const name = itemName.empty() ? std::string_view{blockName} : itemName;
    // 26.40 removed the ItemStack(name, count, aux, tag) constructor; the
    // remaining route is default construction followed by reinit().
    ItemStack item;
    item.reinit(name, 1, 0);
    if (!hasUsableItem(item)) return {};
    return item;
}

std::string stripMinecraftFormatting(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        if (index + 1 < text.size()
            && static_cast<unsigned char>(text[index]) == 0xC2
            && static_cast<unsigned char>(text[index + 1]) == 0xA7) {
            index += 2;
            if (index < text.size()) ++index;
            continue;
        }
        out.push_back(text[index++]);
    }
    return out;
}

namespace {

PlacementItem describeItem(std::shared_ptr<ItemStack const> item) {
    if (!item || !hasUsableItem(*item)) return {};
    auto id = item->getTypeName();
    if (id.empty()) return {};
    auto name = stripMinecraftFormatting(item->getHoverName());
    if (name.empty()) name = id;
    int const size = static_cast<int>(item->getMaxStackSize());
    return {std::move(name), std::move(id), size > 0 ? size : 64, true, std::move(item)};
}

PlacementItem describeItem(ItemInstance const& item) {
    if (!hasUsableItem(item)) return {};
    auto tag = item.save(SaveContext{});
    if (!tag) return {};
    // Validate the reconstructed stack as well as the native pick result.
    return describeItem(std::make_shared<ItemStack>(ItemStack::fromTag(*tag)));
}

} // namespace

PlacementItem resolvePlacementItem(Block const& block, BlockActor const* blockActor) {
    // Each native candidate is checked before reading names or serializing.
    // Ordinary C++ failures advance to the next candidate; fail-fast calls
    // must be prevented by validation, not caught after the fact.
    try {
        auto result = describeItem(block.getBlockType().asItemInstance(block, blockActor));
        if (result.valid) return result;
    } catch (...) {}

    try {
        auto item = makePlacementItem(block);
        if (hasUsableItem(item)) {
            auto result = describeItem(std::make_shared<ItemStack>(item));
            if (result.valid) return result;
        }
    } catch (...) {}

    try {
        auto item = std::make_shared<ItemStack>();
        item->reinit(block, 1);
        return describeItem(std::move(item));
    } catch (...) {
        return {};
    }
}

PlacementItem resolvePlacementItem(Block const& block, CompoundTag const& nbt, Level& level) {
    try {
        BlockPos const origin{0, 0, 0};
        auto actor = VanillaBlockActorFactory::createBlockActor(origin, block.getBlockType());
        if (actor) {
            NewUniqueIdsDataLoadHelper helper;
            helper.mLevel = &level;
            actor->load(level, nbt, helper);
            actor->mPosition = origin;
            return resolvePlacementItem(block, actor.get());
        }
    } catch (...) {
        // Unsupported actor data must not discard the ordinary block item.
    }
    return resolvePlacementItem(block);
}

} // namespace lholo::block
