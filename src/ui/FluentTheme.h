// LHolo - Fluent-style Dear ImGui theme
#pragma once

#include "imgui.h"

namespace lholo::ui {

struct UiMetrics {
    ImVec2 viewport{};
    float  scale{1.0f};
    float  gap{8.0f};
    float  outerPadding{16.0f};
    float  sectionPadding{16.0f};
    float  rounding{8.0f};
};

UiMetrics calculateMetrics(ImVec2 viewport, float uiScale);
void applyFluentTheme(UiMetrics const& metrics);
void resetFluentTheme();

} // namespace lholo::ui
