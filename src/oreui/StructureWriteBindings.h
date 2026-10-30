#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace lholo::oreui {
bool queueStructureWriteIntent(std::string_view);
std::optional<std::string> takeStructureWriteUpdate(bool force);
void resetStructureWriteBindings();
}
