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

bool sameMaterialItem(ItemStack const& left, ItemStack const& right) {
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

PlacementItem resolvePlacementItem(Block const& block, BlockActor const* blockActor) {
    // 1. Bedrock native polymorphic resolution: BlockType::asItemInstance(block, blockActor)
    // Mojang implements this virtual method on all BlockType subclasses (crops, signs, redstone, etc.)
    try {
        ItemInstance const pickItem = block.getBlockType().asItemInstance(block, blockActor);
        if (!pickItem.isNull()) {
            int const size = static_cast<int>(pickItem.getMaxStackSize());
            return {
                stripMinecraftFormatting(pickItem.getHoverName()),
                pickItem.getTypeName(),
                size > 0 ? size : 64,
                true,
                std::make_shared<ItemStack>(ItemStack::fromTag(*pickItem.save(SaveContext{}))),
            };
        }
    } catch (...) {
    }

    // 2. Try makePlacementItem
    ItemStack const placeItem = makePlacementItem(block);
    if (!placeItem.isNull()) {
        int const size = static_cast<int>(placeItem.getMaxStackSize());
        return {
            stripMinecraftFormatting(placeItem.getHoverName()),
            placeItem.getTypeName(),
            size > 0 ? size : 64,
            true,
            std::make_shared<ItemStack>(placeItem),
        };
    }

    // 3. Native Block -> ItemInstance conversion
    ItemInstance const gameItem{block};
    if (!gameItem.isNull()) {
        int const size = static_cast<int>(gameItem.getMaxStackSize());
        return {
            stripMinecraftFormatting(gameItem.getHoverName()),
            gameItem.getTypeName(),
            size > 0 ? size : 64,
            true,
            std::make_shared<ItemStack>(ItemStack::fromTag(*gameItem.save(SaveContext{}))),
        };
    }

    return {};
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
