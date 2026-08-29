// LHolo - Fluent-style menu pages

#pragma once

#include <array>

#include "ui/LHoloMenu.h"

namespace lholo::ui {

inline constexpr char kMaterialPopupName[] = "材料清单###LHoloMaterialList";
inline constexpr std::array<char const*, 8> kPageNames{
    "投影", "创建结构", "结构变换", "渲染设置", "HUD 信息显示", "快捷键", "界面缩放", "实验性功能"
};

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
void renderUiScalePage(MenuModel& model, UiMetrics const& metrics);
void renderMaterialPopup(MenuModel const& model, MenuActions const& actions, UiMetrics const& metrics);

} // namespace lholo::ui
