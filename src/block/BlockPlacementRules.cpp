// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "block/BlockPlacementRules.h"

#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/block/Block.h"

#include <cstddef>

namespace lholo::block {

ItemStack makePlacementItem(Block const& block) {
    std::string const blockName{placeableBaseName(block.getTypeName())};
    auto const itemName = placementItemName(blockName);
    std::string_view const name = itemName.empty() ? std::string_view{blockName} : itemName;
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
