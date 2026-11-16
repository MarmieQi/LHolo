#include "place/local/PlacementRestock.h"

#include "block/BlockPlacementRules.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/level/Level.h"

namespace lholo::place::local {

int findRestockSlot(Inventory const& inventory) {
    for (int slot = 0; slot < 36; ++slot) {
        if (inventory.getItem(slot).isNull()) return slot;
    }
    return -1;
}

std::optional<PlacementRestock> prepareCreativeRestock(Player& player, ItemStackBase const& material, int slot) {
    auto const tag = material.save(SaveContext{});
    if (!tag) return std::nullopt;
    auto stack = ItemStack::fromTag(*tag, player.getLevel());
    if (!block::hasUsableItem(stack)) return std::nullopt;
    stack.set(1);
    return PlacementRestock{slot, player.getInventory().getItem(slot), std::move(stack), std::nullopt};
}

void PlacementRestock::apply(Player& player) {
    auto& inventory = player.getInventory();
    material.serverInitNetId();
    if (source) inventory.setItem(source->slot, source->updated);
    inventory.setItem(materialSlot, material);
}

void PlacementRestock::restore(Player& player) const {
    auto& inventory = player.getInventory();
    inventory.setItem(materialSlot, original);
    if (source) inventory.setItem(source->slot, source->original);
}

} // namespace lholo::place::local
