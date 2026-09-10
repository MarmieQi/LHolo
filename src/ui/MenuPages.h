// LHolo - Fluent-style menu pages

#pragma once

#include <string>

#include "ui/LHoloMenu.h"

namespace lholo::ui {

// Displayed title plus a stable ###ID, so the popup keeps one Dear ImGui
// identity while the title follows the selected language.
std::string materialPopupName();
char const* pageName(MenuPage page);

void renderNavigation(MenuModel& model, UiMetrics const& metrics);

void renderProjectionPage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics);
void renderCreateStructurePage(
    MenuModel& model, MenuActions const& actions, UiMetrics const& metrics
);
void renderExperimentalPage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics);
void renderTransformPage(MenuModel& model, UiMetrics const& metrics);
void renderRenderPage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics);
void renderHotkeysPage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics);
void renderHudPage(MenuModel& model, UiMetrics const& metrics);
void renderInterfacePage(MenuModel& model, UiMetrics const& metrics);
void renderMaterialPopup(MenuModel const& model, UiMetrics const& metrics);

} // namespace lholo::ui
