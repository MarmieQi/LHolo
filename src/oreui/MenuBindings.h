#pragma once

#include <string>
#include <optional>

class IClientInstance;

namespace lholo::oreui {
// Menu use cases delegate game actions to the structure layer.
std::string buildMenuLabels();
void requestMaterialList();
std::optional<std::string> takeProjectionUpdate(bool force = false);
std::optional<std::string> takeMaterialsUpdate(IClientInstance const& client, bool force = false);
void resetMenuBindings();
}
