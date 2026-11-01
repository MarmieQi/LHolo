#pragma once

#include <cstdint>
#include <string>
#include <optional>

class ItemStack;
class IClientInstance;

namespace lholo::oreui {
// Opaque native thumbnail inputs; pages do not interpret renderer properties.
struct MaterialIconData {
    std::uint32_t id{};
    std::string bag;
};
// The native helper resolves descriptors through the current thread's item
// registry. Bind the owning client's live registry for the entire conversion.
std::optional<MaterialIconData> materialIconData(ItemStack const& item, IClientInstance const& client);
}
