#include "ui/hud/NativeHudPainter.h"
#include "ui/hud/HudText.h"
#include "ui/hud/NativeHudIcons.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/TextAlignment.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/controls/UIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/input/RectangleArea.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace lholo::ui::hud {
namespace {
mce::Color color(TextTone tone) {
    switch (tone) {
    case TextTone::Muted: return {0.65f, 0.65f, 0.65f, 1.0f};
    case TextTone::Success: return {0.55f, 0.85f, 0.40f, 1.0f};
    case TextTone::WrongState: return {1.0f, 0.62f, 0.18f, 1.0f};
    case TextTone::WrongType: return {1.0f, 0.28f, 0.24f, 1.0f};
    case TextTone::Extra: return {1.0f, 0.30f, 0.90f, 1.0f};
    case TextTone::Placement: return {0.45f, 0.85f, 1.0f, 1.0f};
    default: return {1.0f, 1.0f, 1.0f, 1.0f};
    }
}

// Truncate at UTF-8 boundaries before submission, so a long file/material name
// cannot wrap over the following native row or escape the panel background.
std::string fitLine(UIRenderContext& context, Font& font, std::string text, float width, float fontScale) {
    auto measure = [&](std::string const& value) { return context.getLineLength(font, value, fontScale, false); };
    return fitHudText(std::move(text), width, measure);
}

float materialColumnGap(float lineHeight) { return lineHeight * 0.8f; }
template <class Measure>
float measurePanel(HudPanel const& panel, float lineHeight, Measure&& measure) {
    float textWidth{};
    for (auto const& line : panel.lines) {
        auto width = static_cast<float>(measure(line.iconPrefix)) + static_cast<float>(measure(line.text))
            + (line.iconId ? hudIconColumn(lineHeight) : 0.0f);
        if (!line.quantity.empty())
            width += materialColumnGap(lineHeight) + static_cast<float>(measure(line.quantity));
        textWidth = std::max(textWidth, width);
    }
    return textWidth;
}

PanelPlacement drawPanel(UIRenderContext& context, Font& font, RectangleArea const& viewport,
    float lineHeight, HudPanel panel, settings::HudPlacement const& saved, std::vector<HudIconDraw>& icons, float opacity, float fontScale, PanelPlacement const* preceding = nullptr) {
    auto measure = [&](std::string const& text) { return context.getLineLength(font, text, fontScale, false); };
    auto textWidth = measurePanel(panel, lineHeight, measure);
    if (panel.compactRows) {
        auto const widthScale = hudWidthScale(viewport._x1 - viewport._x0 - 2 * kHudMargin,
            textWidth + 2 * hudStyle(lineHeight, panel.compactRows, fontScale).horizontalPadding);
        if (widthScale < 1.0f) {
            lineHeight *= widthScale;
            fontScale *= widthScale;
            textWidth = measurePanel(panel, lineHeight, measure);
        }
    }
    auto const placement = applyHudPlacement(placeHudPanel(viewport._x1 - viewport._x0, viewport._y1 - viewport._y0,
        textWidth, lineHeight, panel.lines.size(), panel.position, preceding, panel.compactRows, fontScale),
        viewport._x1 - viewport._x0, viewport._y1 - viewport._y0, saved);
    if (!placement.rows) return placement;
    auto const x = viewport._x0 + placement.x;
    auto const y = viewport._y0 + placement.y;
    auto const style = hudStyle(lineHeight, panel.compactRows, fontScale);
    paintHudBackground(context, {x, y, placement.width, placement.height}, style.rounding, opacity);
    TextMeasureData const textData{fontScale, 0.0f, true, false, false, ::ui::TextAlignment::Left};
    CaretMeasureData const caret{-1, false};
    for (std::size_t index = 0; index < placement.rows; ++index) {
        auto& line = panel.lines[index];
        if (index + 1 == placement.rows && placement.rows < panel.lines.size()) line = {"...", TextTone::Muted};
        auto const iconColumn = line.iconId ? hudIconColumn(lineHeight) : 0.0f;
        auto const availableWidth = placement.width - 2 * style.horizontalPadding;
        bool const hasQuantity = !line.quantity.empty();
        auto const quantityWidth = hasQuantity ? measure(line.quantity) : 0.0f;
        auto const columnGap = hasQuantity ? materialColumnGap(lineHeight) : 0.0f;
        auto const prefixWidth = std::min(static_cast<float>(measure(line.iconPrefix)), std::max(0.0f, availableWidth - quantityWidth - columnGap));
        auto const showIcon = line.iconId && availableWidth - prefixWidth > iconColumn
            + quantityWidth + columnGap;
        auto const textOffset = prefixWidth + (showIcon ? iconColumn : 0.0f);
        auto const nameWidth = hudNameWidth(availableWidth, textOffset,
            quantityWidth, columnGap);
        auto text = hasQuantity ? std::move(line.text)
            : fitLine(context, font, std::move(line.text), nameWidth, fontScale);
        auto const rowHeight = lineHeight + style.lineGap;
        auto const top = y + style.verticalPadding + static_cast<float>(index) * rowHeight;
        if (showIcon) icons.push_back({*line.iconId, x + style.horizontalPadding + prefixWidth, top, lineHeight, line.iconItem});
        auto const contentX = x + style.horizontalPadding;
        if (!line.iconPrefix.empty()) context.drawText(font,
            {contentX, contentX + prefixWidth, top, top + lineHeight},
            fitLine(context, font, std::move(line.iconPrefix), prefixWidth, fontScale),
            color(line.tone), 1.0f, ::ui::TextAlignment::Left, textData, caret);
        if (!text.empty()) context.drawText(font,
            {contentX + textOffset, contentX + textOffset + nameWidth, top, top + lineHeight},
            std::move(text), color(line.tone), 1.0f, ::ui::TextAlignment::Left, textData, caret);
        if (hasQuantity) {
            auto quantity = std::move(line.quantity);
            if (!quantity.empty()) context.drawText(font,
                {contentX + availableWidth - quantityWidth, contentX + availableWidth, top, top + lineHeight},
                std::move(quantity), color(line.tone), 1.0f, ::ui::TextAlignment::Left, textData, caret);
        }
    }
    return placement;
}

} // namespace
void paintHudBackground(UIRenderContext& context, PanelPlacement const& bounds, float rounding, float opacity) {
    auto const x = bounds.x, y = bounds.y, width = bounds.width, height = bounds.height;
    auto const radius = std::min({rounding, width * 0.5f, height * 0.5f});
    mce::Color const background{0.075f, 0.075f, 0.075f, 1.0f};
    // UIRenderContext exposes fillRectangle, not a rounded-rectangle primitive.
    // Disjoint horizontal bands form the corners without alpha accumulating
    // from overlapping rectangles, and need no texture or extra render backend.
    auto const steps = std::max(1, static_cast<int>(std::ceil(radius * 2.0f)));
    auto const stepHeight = radius / static_cast<float>(steps);
    for (int index = 0; index < steps; ++index) {
        auto const offset = static_cast<float>(index) * stepHeight;
        auto const circleY = radius - offset - stepHeight * 0.5f;
        auto const inset = radius - std::sqrt(std::max(0.0f, radius * radius - circleY * circleY));
        context.fillRectangle({x + inset, x + width - inset, y + offset, y + offset + stepHeight}, background, opacity);
        context.fillRectangle({x + inset, x + width - inset, y + height - offset - stepHeight, y + height - offset}, background, opacity);
    }
    if (height > 2 * radius)
        context.fillRectangle({x, x + width, y + radius, y + height - radius}, background, opacity);
}

NativeHudFrame paintNativeHud(UIRenderContext& context, ClientInstance& client,
    HudContent content, settings::HudPlacements const& saved, settings::HudPercentages const& opacities, settings::HudPercentages const& scales) {
    if (content.projection.lines.empty() && content.materials.lines.empty() && content.write.lines.empty()) {
        clearNativeHudIcons();
        return {};
    }
    // Borrow font/context only within this render call. No native pointers or
    // geometry from an earlier world, frame or dimension are retained.
    auto fontHandle = client.$getFontHandle();
    auto& font = fontHandle.getFont();
    auto const viewport = context.getFullClippingRectangle();
    auto const lineHeight = std::max(10.0f, font.getWrapHeight());
    bool const sameCorner = content.projection.position == content.materials.position && !saved[0].custom && !saved[1].custom;
    std::vector<HudIconDraw> icons;
    auto const projectionScale = scales[0] / 100.0f, materialScale = scales[1] / 100.0f;
    auto const projection = drawPanel(context, font, viewport, lineHeight * projectionScale, std::move(content.projection), saved[0], icons, opacities[0] / 100.0f, projectionScale);
    auto const materials = drawPanel(context, font, viewport, lineHeight * materialScale, std::move(content.materials), saved[1], icons, opacities[1] / 100.0f, materialScale, sameCorner ? &projection : nullptr);
    auto const writeScale = scales[2] / 100.0f;
    auto const write = drawPanel(context, font, viewport, lineHeight * writeScale, std::move(content.write),
        saved[2], icons, opacities[2] / 100.0f, writeScale);
    if (!projection.rows && !materials.rows && !write.rows) return {};
    // Submit text and icons without advancing the obfuscated-font clock.
    flushHudTextAndIcons(context, client, content.context, icons);
    return {viewport._x1 - viewport._x0, viewport._y1 - viewport._y0, lineHeight, {projection, materials, write}};
}

}
