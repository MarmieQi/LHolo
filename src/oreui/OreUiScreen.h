#pragma once

namespace lholo::oreui {
inline constexpr char kMenuRoute[] = "/lholo-menu";
inline constexpr char kMenuAsset[] = "/hbui/lholo-menu.html";

bool installMenuScreen();
void uninstallMenuScreen();
// Called by the primary ClientInstance update; never retain a View or Scene.
void processMenuScreen();
enum class MenuEntryPage { Tasks, Hud, CreateWorld, CreateCustom };
void requestMenuOpen(MenuEntryPage page = MenuEntryPage::Tasks);
MenuEntryPage menuEntryPage();
void requestMenuClose();
bool isMenuOnTop();
}
