#include "ui/hud/editor/HudEditorView.h"
#include "ui/hud/editor/HudEditor.h"
#include "ui/hud/NativeHudPainter.h"
#include "i18n/Translator.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/TextAlignment.h"
#include "mc/client/gui/controls/UIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/input/RectangleArea.h"
#include <algorithm>
#include <array>
#include <string>

namespace lholo::ui::hud {
void drawHudEditorView(UIRenderContext& context, ClientInstance& client) {
    auto const frame = paintNativeHud(context, client, readHudContent(), effectiveHudPlacements(), effectiveHudBackgroundOpacities(), effectiveHudScales());
    if (frame.width == 0 || frame.height == 0) return;
    auto const width = frame.width, height = frame.height, lineHeight = frame.lineHeight;
    auto const border = std::max(0.5f, lineHeight / 18.0f);
    auto fontHandle = client.$getFontHandle();
    auto& font = fontHandle.getFont();
    auto const view = hudEditorSnapshot();
    using i18n::TextKey;
    auto text = [&](std::string value, float x, float y, float textWidth, mce::Color color) {
        TextMeasureData const data{1.0f, 0.0f, true, false, false, ::ui::TextAlignment::Left};
        CaretMeasureData const caret{-1, false};
        context.drawText(font, {x, x + textWidth, y, y + lineHeight}, std::move(value), color,
            1.0f, ::ui::TextAlignment::Left, data, caret);
    };
    auto fill = [&](float x, float y, float w, float h, mce::Color color) {
        context.fillRectangle({x, x + w, y, y + h}, color, 1.0f);
    };
    for (std::size_t index = 0; index < frame.panels.size(); ++index) {
        auto const& panel = frame.panels[index];
        publishHudEditorBounds(index, {panel.x / width, panel.y / height, panel.width / width, panel.height / height});
        if (view.selected == static_cast<int>(index)) {
            mce::Color const selectedBorder{1.0f, 0.0f, 0.0f, 1.0f};
            fill(panel.x, panel.y, panel.width, border, selectedBorder);
            fill(panel.x, panel.y + panel.height - border, panel.width, border, selectedBorder);
            fill(panel.x, panel.y, border, panel.height, selectedBorder);
            fill(panel.x + panel.width - border, panel.y, border, panel.height, selectedBorder);
        }
    }
    std::array<char const*, 6> const labels{i18n::tr(TextKey::HudLayoutResetAll), i18n::tr(TextKey::ButtonCancel),
        i18n::tr(TextKey::HudLayoutDone), i18n::tr(TextKey::HudLayoutReset), i18n::tr(TextKey::HudLayoutBackgroundOpacity), i18n::tr(TextKey::HudLayoutScale)};
    std::array<HudEditorBounds, 6> buttons{};
    HudEditorBounds sliderPopup{}, sliderTrack{};
    float total{};
    constexpr float controlBackgroundOpacity = 0.5f;
    float const gap = 4, buttonHeight = lineHeight + 8;
    for (int index = 0; index < 3; ++index) {
        buttons[index].width = context.getLineLength(font, labels[index], 1.0f, false) + 14;
        total += buttons[index].width;
    }
    total += gap * 2;
    float x = (width - total) / 2;
    for (int index = 0; index < 3; ++index) {
        buttons[index] = {x, 8, buttons[index].width, buttonHeight};
        x += buttons[index].width + gap;
    }
    if (view.selected >= 0) {
        float const resetWidth = context.getLineLength(font, labels[3], 1.0f, false) + 14;
        float const opacityWidth = context.getLineLength(font, labels[4], 1.0f, false) + 14;
        float const scaleWidth = context.getLineLength(font, labels[5], 1.0f, false) + 14;
        float const rowWidth = resetWidth + opacityWidth + scaleWidth + gap * 2;
        float const y = height - buttonHeight - 8;
        x = (width - rowWidth) / 2;
        buttons[3] = {x, y, resetWidth, buttonHeight};
        buttons[4] = {x + resetWidth + gap, y, opacityWidth, buttonHeight};
        buttons[5] = {buttons[4].x + opacityWidth + gap, y, scaleWidth, buttonHeight};
        if (view.slider != HudEditorSlider::None) {
            bool const scaling = view.slider == HudEditorSlider::Scale;
            auto const value = scaling ? effectiveHudScales()[view.selected] : effectiveHudBackgroundOpacities()[view.selected];
            auto const& trigger = buttons[scaling ? 5 : 4];
            float const popupWidth = std::min(width - 16, std::max(140.0f, trigger.width));
            float const popupHeight = lineHeight + 23;
            float const popupX = std::clamp(trigger.x + (trigger.width - popupWidth) / 2, 8.0f, width - popupWidth - 8);
            float const popupY = y - gap - popupHeight;
            paintHudBackground(context, {popupX, popupY, popupWidth, popupHeight}, hudStyle(lineHeight).rounding, controlBackgroundOpacity);
            auto const percent = std::to_string(value) + "%";
            float const valueWidth = context.getLineLength(font, percent, 1.0f, false);
            text(percent, popupX + (popupWidth - valueWidth) / 2, popupY + 6, valueWidth, {1.0f, 1.0f, 1.0f, 1.0f});
            float const trackX = popupX + 10, trackWidth = popupWidth - 20;
            float const trackY = popupY + lineHeight + 11;
            float const progress = trackWidth * (scaling ? (value - 50) / 150.0f : value / 100.0f);
            paintHudBackground(context, {trackX, trackY - 1.5f, trackWidth, 3}, 1.5f, 1.0f);
            fill(trackX, trackY - 1, progress, 2, {0.8f, 0.8f, 0.8f, 1.0f});
            bool const hover = view.sliderTrack.contains(view.pointerX, view.pointerY);
            float const knobSize = view.sliderDragging || hover ? 8.0f : 6.0f;
            fill(trackX + progress - knobSize / 2, trackY - knobSize / 2, knobSize, knobSize, {1.0f, 1.0f, 1.0f, 1.0f});
            sliderPopup = {popupX / width, popupY / height, popupWidth / width, popupHeight / height};
            sliderTrack = {trackX / width, (trackY - 7) / height, trackWidth / width, 14 / height};
        }
    }
    for (int index = 0; index < 6; ++index) {
        auto& box = buttons[index];
        if (box.width == 0) continue;
        bool const hover = box.contains(view.pointerX * width, view.pointerY * height);
        auto const opacity = controlBackgroundOpacity + (view.pressed == index ? 0.2f : hover ? 0.1f : 0.0f);
        paintHudBackground(context, {box.x, box.y, box.width, box.height}, hudStyle(lineHeight).rounding, opacity);
        text(labels[index], box.x + 7, box.y + 4, box.width - 14, {1.0f, 1.0f, 1.0f, 1.0f});
        box = {box.x / width, box.y / height, box.width / width, box.height / height};
    }
    publishHudEditorButtons(buttons, sliderPopup, sliderTrack);
    context.flushText(0, std::nullopt);
}
}
