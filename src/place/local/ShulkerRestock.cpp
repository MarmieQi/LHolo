#include "place/local/ShulkerRestock.h"

#include "block/BlockPlacementRules.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/BlockType.h"

#include <memory>

namespace lholo::place::local {

std::optional<PlacementRestock> prepareShulkerRestock(Player& player, ItemStackBase const& material, int destination) {
    auto& inventory = player.getInventory();
    constexpr int inventorySlots = 36;

    auto const materialName = material.getTypeName();
    for (int slot = 0; slot < inventorySlots; ++slot) {
        auto const& box = inventory.getItem(slot);
        if (!block::hasUsableItem(box) || box.mCount != 1 || !box.mUserData) continue;
        auto const* type = box.mItem->getBlockTypeForRendering();
        if (!type || type->getBlockEntityType() != BlockActorType::ShulkerBox) continue;

        auto const stored = box.mUserData->mTags.find("Items");
        if (stored == box.mUserData->mTags.end() || !stored->second.hold<ListTag>()) continue;
        auto const& items = stored->second.get<ListTag>();
        for (std::size_t index = 0; index < items.size(); ++index) {
            auto const& entry = items[index];
            if (!entry.hold<CompoundTag>()) continue;
            auto const& tag = entry.get<CompoundTag>();
            auto const name = tag.mTags.find("Name");
            if (name == tag.mTags.end() || !name->second.hold<StringTag>()
                || static_cast<std::string const&>(name->second.get<StringTag>()) != materialName) continue;

            auto stack = ItemStack::fromTag(tag, player.getLevel());
            if (!block::hasUsableItem(stack) || stack.getIdAux() != material.getIdAux()) continue;

            auto data = std::make_unique<CompoundTag>(*box.mUserData);
            auto& remaining = data->at("Items").get<ListTag>();
            remaining.erase(remaining.begin() + index);
            if (remaining.empty()) data->erase("Items");
            ItemStack updatedBox = box;
            updatedBox.setUserData(std::move(data));
            return PlacementRestock{
                destination, inventory.getItem(destination), std::move(stack), RestockSource{slot, box, std::move(updatedBox)}};
        }
    }
    return std::nullopt;
}

} // namespace lholo::place::local
