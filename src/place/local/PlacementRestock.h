#pragma once

#include "mc/world/item/ItemStack.h"

#include <optional>

class Inventory;
class Player;

namespace lholo::place::local {

struct RestockSource {
    int slot;
    ItemStack original;
    ItemStack updated;
};

struct PlacementRestock {
    int materialSlot;
    ItemStack original;
    ItemStack material;
    std::optional<RestockSource> source;

    void apply(Player& player);
    void restore(Player& player) const;
};

int findRestockSlot(Inventory const& inventory);
std::optional<PlacementRestock> prepareCreativeRestock(Player& player, ItemStackBase const& material, int slot);

} // namespace lholo::place::local
