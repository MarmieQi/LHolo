// LHolo - Fluent-style menu widgets

#include "ui/MenuWidgets.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <string>

namespace lholo::ui {

std::string formatStackCount(std::uint64_t count, int stackSize) {
    auto const stack = static_cast<std::uint64_t>(stackSize > 0 ? stackSize : 64);
    if (count < stack) return std::to_string(count);
    auto const stacks = count / stack;
    auto const remainder = count % stack;
    char buffer[64];
    if (remainder == 0) {
        std::snprintf(
            buffer, sizeof(buffer), "%llu (%llu x %llu)",
            static_cast<unsigned long long>(count),
            static_cast<unsigned long long>(stacks),
            static_cast<unsigned long long>(stack)
        );
    } else {
        std::snprintf(
            buffer, sizeof(buffer), "%llu (%llu x %llu + %llu)",
            static_cast<unsigned long long>(count),
            static_cast<unsigned long long>(stacks),
            static_cast<unsigned long long>(stack),
            static_cast<unsigned long long>(remainder)
        );
    }
    return buffer;
}

float fieldWidth(UiMetrics const& metrics) {
    auto const available = ImGui::GetContentRegionAvail().x;
    auto const minimum = ImGui::CalcTextSize("0000000000").x
        + ImGui::GetStyle().FramePadding.x * 2.0f;
    auto const preferred = available * (metrics.compact ? 1.0f : 0.30f);
    auto const maximum = available * (metrics.compact ? 1.0f : 0.48f);
    return std::max(0.0f, std::min(std::max(preferred, minimum), maximum));
}

float numericFieldWidth(UiMetrics const& metrics) {
    auto const available = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    auto const framePadding = ImGui::GetStyle().FramePadding.x * 2.0f;
    // These rows contain percentages, not free-form long integers.  Reserve
    // room for a sign while sizing from the value itself instead of making a
    // numeric editor consume the whole form column.
    auto const contentWidth = ImGui::CalcTextSize("-000").x + framePadding;
    auto const minimum = ImGui::CalcTextSize("00").x + framePadding;
    auto const maximum = available * (metrics.compact ? 1.0f : 0.22f);
    return std::min(available, std::max(minimum, std::min(contentWidth, maximum)));
}

float adaptiveComboWidth(char const* const* items, int count) {
    auto longest = 0.0f;
    for (int index = 0; index < count; ++index) {
        longest = std::max(longest, ImGui::CalcTextSize(items[index]).x);
    }
    // Combo's arrow area is one frame high in Dear ImGui.  The rest is sized
    // from the actual option text, so short choices no longer create a wide
    // empty field while long localized choices remain readable.
    auto const desired = longest + ImGui::GetStyle().FramePadding.x * 2.0f
        + ImGui::GetFrameHeight();
    return std::min(desired, ImGui::GetContentRegionAvail().x);
}

void renderCheckboxRow(char const* id, char const* label, bool& value, UiMetrics const& metrics) {
    // Retain the familiar square checkbox while keeping the surrounding page
    // flat.  The local border makes the unchecked state legible on the dark
    // canvas without adding a container around the whole setting.
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, std::max(1.0f, metrics.scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, metrics.rounding * 0.55f);
    ImGui::Checkbox(id, &value);
    ImGui::PopStyleVar(2);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
}

void drawCenteredInputValue(char const* text, ImVec2 minimum, ImVec2 maximum) {
    auto const textSize = ImGui::CalcTextSize(text);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(
            minimum.x + (maximum.x - minimum.x - textSize.x) * 0.5f,
            minimum.y + (maximum.y - minimum.y - textSize.y) * 0.5f
        ),
        ImGui::GetColorU32(ImGuiCol_Text),
        text
    );
}

void renderSteppedInt(
    char const*    id,
    char const*    label,
    int&           value,
    int            minimum,
    int            maximum,
    UiMetrics const& metrics
) {
    ImGui::PushID(id);
    auto const buttonWidth = ImGui::GetFrameHeight();
    // The global spacing is intentionally generous for ordinary rows.  A
    // stepped editor is one control group, so use a tighter local rhythm to
    // keep '-' value '+' visually connected.
    auto const spacing = ImGui::GetStyle().ItemSpacing.x * 0.55f;
    if (metrics.compact) {
        ImGui::TextUnformatted(label);
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(0.0f, spacing);
    }
    auto const available = ImGui::GetContentRegionAvail().x;
    auto const controlWidth = metrics.compact ? available : fieldWidth(metrics);
    auto const inputMaximum = std::max(0.0f, controlWidth - buttonWidth * 2.0f - spacing * 2.0f);
    char valueText[16]{};
    std::snprintf(valueText, sizeof(valueText), "%d", value);
    auto const framePadding = ImGui::GetStyle().FramePadding.x * 2.0f;
    auto const minimumInputWidth = ImGui::CalcTextSize("-0").x + framePadding;
    auto const desiredInputWidth = ImGui::CalcTextSize(valueText).x + framePadding;
    auto const inputWidth = std::min(inputMaximum, std::max(minimumInputWidth, desiredInputWidth));
    auto const groupWidth = buttonWidth * 2.0f + inputWidth + spacing * 2.0f;
    if (metrics.compact && controlWidth > groupWidth) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (controlWidth - groupWidth) * 0.5f);
    }
    if (ImGui::Button("-", ImVec2(buttonWidth, 0.0f))) {
        if (value > minimum) --value;
    }
    ImGui::SameLine(0.0f, spacing);
    // Dear ImGui's numeric input is left-aligned.  Keep the widget for
    // keyboard editing but paint its resting value in the centre of the
    // compact, auto-sized number field.
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::SetNextItemWidth(inputWidth);
    if (ImGui::InputInt("##Value", &value, 0, 0)) {
        value = std::clamp(value, minimum, maximum);
    }
    ImGui::PopStyleColor();
    auto const inputRectMinimum = ImGui::GetItemRectMin();
    auto const inputRectMaximum = ImGui::GetItemRectMax();
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::Button("+", ImVec2(buttonWidth, 0.0f))) {
        if (value < maximum) ++value;
    }
    std::snprintf(valueText, sizeof(valueText), "%d", value);
    drawCenteredInputValue(valueText, inputRectMinimum, inputRectMaximum);
    ImGui::PopID();
}

void renderSteppedFloat(
    char const*    id,
    char const*    label,
    float&         value,
    float          minimum,
    float          maximum,
    float          step,
    UiMetrics const& metrics
) {
    ImGui::PushID(id);
    auto const buttonWidth = ImGui::GetFrameHeight();
    auto const spacing = ImGui::GetStyle().ItemSpacing.x * 0.55f;
    if (metrics.compact) {
        ImGui::TextUnformatted(label);
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(0.0f, spacing);
    }
    auto const available = ImGui::GetContentRegionAvail().x;
    auto const controlWidth = metrics.compact ? available : fieldWidth(metrics);
    auto const inputMaximum = std::max(0.0f, controlWidth - buttonWidth * 2.0f - spacing * 2.0f);
    char valueText[16]{};
    std::snprintf(valueText, sizeof(valueText), "%.1f", value);
    auto const framePadding = ImGui::GetStyle().FramePadding.x * 2.0f;
    auto const minimumInputWidth = ImGui::CalcTextSize("-0.0").x + framePadding;
    auto const desiredInputWidth = ImGui::CalcTextSize(valueText).x + framePadding;
    auto const inputWidth = std::min(inputMaximum, std::max(minimumInputWidth, desiredInputWidth));
    auto const groupWidth = buttonWidth * 2.0f + inputWidth + spacing * 2.0f;
    if (metrics.compact && controlWidth > groupWidth) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (controlWidth - groupWidth) * 0.5f);
    }
    if (ImGui::Button("-", ImVec2(buttonWidth, 0.0f))) {
        value = std::max(minimum, value - step);
    }
    ImGui::SameLine(0.0f, spacing);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::SetNextItemWidth(inputWidth);
    ImGui::InputFloat("##Value", &value, 0.0f, 0.0f, "%.1f");
    ImGui::PopStyleColor();
    auto const inputRectMinimum = ImGui::GetItemRectMin();
    auto const inputRectMaximum = ImGui::GetItemRectMax();
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::Button("+", ImVec2(buttonWidth, 0.0f))) {
        value = std::min(maximum, value + step);
    }
    value = std::clamp(std::round(value * 10.0f) / 10.0f, minimum, maximum);
    std::snprintf(valueText, sizeof(valueText), "%.1f", value);
    drawCenteredInputValue(valueText, inputRectMinimum, inputRectMaximum);
    ImGui::PopID();
}

} // namespace lholo::ui
