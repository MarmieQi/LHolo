// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "block/BlockPlacementRules.h"

#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/block/Block.h"

#include <cstddef>

namespace lholo::block {
namespace {

char const* placingItemName(std::string_view blockName) {
    if (blockName == "minecraft:redstone_wire") return "minecraft:redstone";
    if (blockName == "minecraft:unpowered_comparator"
        || blockName == "minecraft:powered_comparator") {
        return "minecraft:comparator";
    }
    if (blockName == "minecraft:unpowered_repeater"
        || blockName == "minecraft:powered_repeater") {
        return "minecraft:repeater";
    }
    if (blockName == "minecraft:unlit_redstone_torch") return "minecraft:redstone_torch";
    return nullptr;
}

} // namespace

ItemStack makePlacementItem(Block const& block) {
    std::string const blockName{placeableBaseName(block.getTypeName())};
    char const* const itemName = placingItemName(blockName);
    std::string_view const name = itemName ? std::string_view{itemName} : std::string_view{blockName};
    return ItemStack(name, 1, 0, nullptr);
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

PlacementItem resolvePlacementItem(Block const& block) {
    ItemStack const placeItem = makePlacementItem(block);
    if (!placeItem.isNull()) {
        int const size = static_cast<int>(placeItem.getMaxStackSize());
        return {
            stripMinecraftFormatting(placeItem.getHoverName()),
            placeItem.getTypeName(),
            size > 0 ? size : 64,
            true,
        };
    }

    // Block-only forms such as wall signs and wall coral fans resolve to their
    // inventory form through Bedrock's own Block -> ItemInstance conversion.
    ItemInstance const gameItem{block};
    if (gameItem.isNull()) return {};
    int const size = static_cast<int>(gameItem.getMaxStackSize());
    return {
        stripMinecraftFormatting(gameItem.getHoverName()),
        gameItem.getTypeName(),
        size > 0 ? size : 64,
        true,
    };
}

} // namespace lholo::block
