// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Shared block identity and block-to-item rules used by projection correction,
// placement execution and the material tracker. Keep name compatibility rules
// here so those three consumers cannot drift apart.

#pragma once

#include <string>
#include <string_view>

class Block;
class ItemStack;

namespace lholo::block {

struct PlacementItem {
    std::string displayName;
    std::string itemId;
    int         stackSize{64};
    bool        valid{false};
};

// Maps game-driven runtime variants to the block form the player can actually
// place. Names without a runtime-only variant are returned unchanged.
[[nodiscard]] inline std::string_view placeableBaseName(std::string_view name) {
    if (name == "minecraft:lit_redstone_lamp")          return "minecraft:redstone_lamp";
    if (name == "minecraft:lit_redstone_ore")           return "minecraft:redstone_ore";
    if (name == "minecraft:lit_deepslate_redstone_ore") return "minecraft:deepslate_redstone_ore";
    if (name == "minecraft:lit_furnace")                return "minecraft:furnace";
    if (name == "minecraft:lit_blast_furnace")          return "minecraft:blast_furnace";
    if (name == "minecraft:lit_smoker")                 return "minecraft:smoker";
    if (name == "minecraft:unlit_redstone_torch")       return "minecraft:redstone_torch";
    if (name == "minecraft:powered_repeater")           return "minecraft:unpowered_repeater";
    if (name == "minecraft:powered_comparator")         return "minecraft:unpowered_comparator";
    return name;
}

// Maps a placeable block name to a differently named inventory item. An empty
// result means the block and its inventory item use the same name.
[[nodiscard]] inline std::string_view placementItemName(std::string_view blockName) {
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
    return {};
}

// Returns the stable identity used to group a runtime block in material lists
// and material-layer projection. An empty key means the runtime-only block
// does not represent a collectible material. This function deliberately uses
// names only, so structure parsing never has to access the item registry.
[[nodiscard]] inline std::string materialKey(std::string_view blockName) {
    if (blockName == "minecraft:bubble_column"
        || blockName == "minecraft:piston_arm_collision"
        || blockName == "minecraft:sticky_piston_arm_collision"
        || blockName == "minecraft:moving_block") {
        return {};
    }
    if (blockName == "minecraft:water" || blockName == "minecraft:flowing_water") {
        return "minecraft:water";
    }
    if (blockName == "minecraft:lava" || blockName == "minecraft:flowing_lava") {
        return "minecraft:lava";
    }

    auto const baseName = placeableBaseName(blockName);
    auto const itemName = placementItemName(baseName);
    auto const identity = itemName.empty() ? baseName : itemName;
    return "item:" + std::string{identity};
}

// Builds the neutral inventory item used to place a block. World orientation
// states are intentionally not copied into the item stack.
[[nodiscard]] ItemStack makePlacementItem(Block const& block);

// Resolves the real inventory item represented by a block. Must run on the game
// tick thread because it touches the Bedrock item registry/localization data.
[[nodiscard]] PlacementItem resolvePlacementItem(Block const& block);

[[nodiscard]] std::string stripMinecraftFormatting(std::string_view text);

} // namespace lholo::block
