// LHolo - Fluent-style menu widgets

#pragma once

#include "ui/LHoloMenu.h"

#include <imgui.h>

namespace lholo::ui {

float fieldWidth(UiMetrics const& metrics);
float numericFieldWidth(UiMetrics const& metrics);
float adaptiveComboWidth(char const* const* items, int count);

// JE-style stack count: below one stack -> "N"; otherwise "N (a x S + b)" (the
// "+ b" omitted when it divides evenly), e.g. 111 -> "111 (1 x 64 + 47)",
// 18 buckets (S=1) -> "18 (18 x 1)".
std::string formatStackCount(std::uint64_t count, int stackSize);

template <typename Body>
void renderSection(char const* id, char const* title, UiMetrics const& metrics, Body&& body) {
    // Function groups deliberately share the page canvas.  A child window
    // here would add a card border and its own scrollbar, which makes the
    // menu feel fragmented and fights the single, unobtrusive page scroll.
    ImGui::PushID(id);
    ImGui::TextUnformatted(title);
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.35f));
    body();
    ImGui::Dummy(ImVec2(0.0f, metrics.gap * 1.1f));
    ImGui::PopID();
}

template <typename Control>
void renderValueRow(char const* label, UiMetrics const& metrics, Control&& control) {
    if (metrics.compact) {
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        control();
        return;
    }
    ImGui::SetNextItemWidth(fieldWidth(metrics));
    control();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
}

template <typename Control>
void renderNumericValueRow(char const* label, UiMetrics const& metrics, Control&& control) {
    if (metrics.compact) {
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        control();
        return;
    }
    ImGui::SetNextItemWidth(numericFieldWidth(metrics));
    control();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
}

void renderCheckboxRow(char const* id, char const* label, bool& value, UiMetrics const& metrics);
void drawCenteredInputValue(char const* text, ImVec2 minimum, ImVec2 maximum);

void renderSteppedInt(
    char const*    id,
    char const*    label,
    int&           value,
    int            minimum,
    int            maximum,
    UiMetrics const& metrics
);

void renderSteppedFloat(
    char const*    id,
    char const*    label,
    float&         value,
    float          minimum,
    float          maximum,
    float          step,
    UiMetrics const& metrics
);

} // namespace lholo::ui
