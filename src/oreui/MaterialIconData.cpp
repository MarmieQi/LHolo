#include "oreui/MaterialIconData.h"
#include "mc/client/gui/screens/ItemRendererData.h"
#include "mc/client/gui/screens/ItemRendererDataHelpers.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/registry/ItemRegistryManager.h"
#include "nlohmann/json.hpp"

// The 26.60 SDK declares this non-exported constructor for what was an
// aggregate in 26.51. Preserve the former data{} value initialization;
// setItemRendererBindings still fills the native visual metadata below.
ItemRendererData::ItemRendererData()
    : itemIdAux{}, isEmptyStack{}, chargedItem{}, itemCustomColor{},
      itemPickupTime{}, showItemPickup{}, armorTrimMaterial{}, bannerPatterns{},
      bannerColors{}, bannerType{}, decoratedPotSherds{}, serializedActorPose{},
      shieldIsActive{}, shieldBaseColor{}, shieldBaseColorHovered{},
      itemLoadstoneTrackingHandle{}, showBundleOpenFront{}, showBundleOpenBack{} {}

namespace lholo::oreui {
std::optional<MaterialIconData> materialIconData(ItemStack const& item, IClientInstance const& client) {
    auto const registry = client.getItemRegistry();
    auto const owner = registry.mWeakRegistry.lock();
    if (!owner) return std::nullopt;
    // View::update's original registry scope has ended before our post-update
    // publication runs. Keep the registry alive and restore the previous TLS
    // registry after all native metadata and ItemRendererData cleanup finish.
    ItemRegistryManager::ScopedItemRegistry const scope{owner};
    ItemRendererData data{};
    ItemRendererDataHelpers::setItemRendererBindings(data, item);
    // ScreenController item-renderer bindings and ItemResourceResolver
    // use these PropertyBag keys. Values come from the native item helper,
    // including banner defaults, rather than a second NBT conversion table.
    nlohmann::json properties{
        {"#item_id_aux", data.itemIdAux},
        {"#charged_item", data.chargedItem},
        {"#item_custom_color", data.itemCustomColor},
        {"#armor_trim_material", data.armorTrimMaterial.get()},
        {"#decorated_pot_sherds", data.decoratedPotSherds.get()},
        {"#serialized_actor_pose", data.serializedActorPose},
        {"#shield_is_active", data.shieldIsActive},
        {"#shield_base_color", data.shieldBaseColor},
        {"#shield_base_color_hovered", data.shieldBaseColorHovered},
        {"#item_lodestone_tracking_handle", data.itemLoadstoneTrackingHandle},
        {"#should_show_bundle_open_front", data.showBundleOpenFront},
        {"#should_show_bundle_open_back", data.showBundleOpenBack},
    };
    if (!data.bannerPatterns->empty() || !data.bannerColors->empty() || data.bannerType >= 0) {
        properties["#banner_patterns"] = data.bannerPatterns.get();
        properties["#banner_colors"] = data.bannerColors.get();
        properties["#banner_type"] = data.bannerType;
    }
    MaterialIconData result{2166136261u, properties.dump()};
    // GeometryAtlas tiles use a uint32 identity. Hash the complete visual
    // inputs, so equal item IDs with different colors/patterns cannot reuse
    // the default banner tile.
    for (unsigned char c : item.getTypeName() + result.bag)
        result.id = (result.id ^ c) * 16777619u;
    return result;
}
}
