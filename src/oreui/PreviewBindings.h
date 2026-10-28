#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace lholo::oreui {
bool queuePreviewIntent(std::string_view);
std::optional<std::string> takePreviewUpdate(bool force);
void resetPreviewBindings();
}
