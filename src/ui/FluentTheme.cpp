// LHolo - Fluent-style Dear ImGui theme

#include "ui/FluentTheme.h"

#include <algorithm>
#include <cmath>

namespace lholo::ui {
namespace {

bool       gBaseStyleReady{};
ImGuiStyle gBaseStyle{};
float      gLastScale{-1.0f};
ImVec2     gLastViewport{-1.0f, -1.0f};

// Keep action hints at the original UI density with the existing 36px atlas.
constexpr float kMenuDensity{1.18f * 1.30f};

ImVec4 color(float r, float g, float b, float a = 1.0f) { return {r, g, b, a}; }

} // namespace

UiMetrics calculateMetrics(ImVec2 viewport, float uiScale) {
    UiMetrics metrics;
    metrics.viewport = viewport;
    metrics.scale = std::clamp(uiScale, 1.0f, 5.0f);
    // Logical dimensions are scaled once below.
    metrics.gap = 8.0f * metrics.scale * kMenuDensity;
    metrics.outerPadding = 16.0f * metrics.scale * kMenuDensity;
    metrics.sectionPadding = 12.0f * metrics.scale * kMenuDensity;
    metrics.rounding = 4.0f * metrics.scale * kMenuDensity;
    return metrics;
}

void applyFluentTheme(UiMetrics const& metrics) {
    if (!gBaseStyleReady) {
        gBaseStyle = ImGui::GetStyle();
        gBaseStyleReady = true;
    }
    auto const viewportChanged = std::abs(gLastViewport.x - metrics.viewport.x) > 0.5f
        || std::abs(gLastViewport.y - metrics.viewport.y) > 0.5f;
    if (std::abs(gLastScale - metrics.scale) < 0.001f && !viewportChanged) return;

    auto style = gBaseStyle;
    style.ScaleAllSizes(metrics.scale * kMenuDensity);
    style.WindowPadding = {metrics.outerPadding, metrics.outerPadding};
    style.FramePadding = {metrics.sectionPadding * 0.82f, metrics.sectionPadding * 0.42f};
    style.ItemSpacing = {metrics.gap, metrics.gap};
    style.ItemInnerSpacing = {metrics.gap * 0.7f, metrics.gap * 0.6f};
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = metrics.rounding;
    style.PopupRounding = metrics.rounding;
    style.ScrollbarRounding = metrics.rounding;
    style.GrabRounding = metrics.rounding;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f * metrics.scale;

    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = color(0.96f, 0.96f, 0.96f);
    colors[ImGuiCol_TextDisabled] = color(0.58f, 0.58f, 0.60f);
    colors[ImGuiCol_WindowBg] = color(0.075f, 0.075f, 0.075f, 1.0f);
    colors[ImGuiCol_ChildBg] = color(0.075f, 0.075f, 0.075f, 1.0f);
    colors[ImGuiCol_PopupBg] = color(0.115f, 0.115f, 0.115f, 1.0f);
    colors[ImGuiCol_TitleBg] = color(0.115f, 0.115f, 0.115f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = color(0.115f, 0.115f, 0.115f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = color(0.115f, 0.115f, 0.115f, 1.0f);
    colors[ImGuiCol_Border] = color(0.24f, 0.24f, 0.25f, 1.0f);
    colors[ImGuiCol_FrameBg] = color(0.145f, 0.145f, 0.145f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = color(0.185f, 0.185f, 0.185f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = color(0.22f, 0.22f, 0.22f, 1.0f);
    // Keep ordinary actions neutral.  Blue is reserved for state (switches
    // and the navigation marker), instead of competing with every control.
    colors[ImGuiCol_Button] = color(0.16f, 0.16f, 0.16f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = color(0.21f, 0.21f, 0.21f, 1.0f);
    colors[ImGuiCol_ButtonActive] = color(0.26f, 0.26f, 0.26f, 1.0f);
    colors[ImGuiCol_Header] = color(0.16f, 0.16f, 0.16f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = color(0.20f, 0.20f, 0.20f, 1.0f);
    colors[ImGuiCol_HeaderActive] = color(0.16f, 0.16f, 0.16f, 1.0f);
    colors[ImGuiCol_CheckMark] = color(0.0f, 0.47f, 0.84f, 1.0f);
    colors[ImGuiCol_SliderGrab] = color(0.0f, 0.47f, 0.84f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = color(0.18f, 0.62f, 1.0f, 1.0f);
    colors[ImGuiCol_Separator] = color(0.18f, 0.18f, 0.19f, 1.0f);
    colors[ImGuiCol_TableHeaderBg] = color(0.15f, 0.15f, 0.16f, 1.0f);
    colors[ImGuiCol_TableBorderStrong] = color(0.23f, 0.23f, 0.24f, 1.0f);
    colors[ImGuiCol_TableBorderLight] = color(0.18f, 0.18f, 0.19f, 1.0f);
    colors[ImGuiCol_TableRowBg] = color(0.11f, 0.11f, 0.12f, 0.55f);
    colors[ImGuiCol_TableRowBgAlt] = color(0.14f, 0.14f, 0.15f, 0.55f);
    colors[ImGuiCol_ModalWindowDimBg] = color(0.0f, 0.0f, 0.0f, 0.62f);

    ImGui::GetStyle() = style;
    // The existing atlas is 36px. Rendering it at half scale gives a crisp
    // 18px logical default while keeping headroom for 4K automatic scaling.
    ImGui::GetIO().FontGlobalScale = metrics.scale * 0.5f * kMenuDensity;
    gLastScale = metrics.scale;
    gLastViewport = metrics.viewport;
}

void resetFluentTheme() {
    if (ImGui::GetCurrentContext()) ImGui::GetIO().FontGlobalScale = 1.0f;
    gBaseStyleReady = false;
    gLastScale = -1.0f;
    gLastViewport = {-1.0f, -1.0f};
}

} // namespace lholo::ui
