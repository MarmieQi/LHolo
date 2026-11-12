#pragma once

#include <string_view>

namespace lholo::oreui {
inline constexpr std::string_view kActionHintOverlayPath = "hbui/lholo-action-hint.html";
bool installActionHintOverlay();
void updateActionHintOverlay();
void uninstallActionHintOverlay();
std::string_view actionHintOverlayPage();
}
