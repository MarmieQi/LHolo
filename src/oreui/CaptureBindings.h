#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace lholo::oreui {
bool queueCaptureIntent(std::string_view payload);
std::optional<std::string> takeCaptureUpdate(bool force = false);
std::string buildCustomBlockCatalog();
void resetCaptureBindings();
void shutdownCaptureBindings();
}
