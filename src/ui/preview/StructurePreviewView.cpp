#include "ui/preview/StructurePreviewView.h"
#include "ui/hud/HudText.h"
#include "ui/hud/NativeHudPainter.h"
#include "i18n/Translator.h"
#include "i18n/Message.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/ScreenSizeData.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/TextAlignment.h"
#include "mc/client/gui/controls/UIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/input/RectangleArea.h"

namespace lholo::ui::preview {
void drawStructurePreviewBackground(UIRenderContext& context, ClientInstance& client, PreviewViewState const& s) {
    auto const screen = client.$getGuiData()->mScreenSizeData->clientUIScreenSize.get();
    float const w = screen.x * s.frame.width, h = screen.y * s.frame.height;
    float const left = (screen.x - w) * 0.5f, top = (screen.y - h) * 0.5f;
    auto fontHandle = client.$getFontHandle();
    auto const lineHeight = std::max(10.0f,fontHandle.getFont().getWrapHeight());
    hud::paintHudBackground(context,{left,top,w,h},hud::hudStyle(lineHeight).rounding,0.60f);
}
void drawStructurePreviewView(UIRenderContext& context, ClientInstance& client, PreviewViewState const& s) {
    auto const size = client.$getGuiData()->mScreenSizeData->clientUIScreenSize.get();
    float const w = size.x * s.frame.width, h = size.y * s.frame.height;
    float const left = (size.x - w) * 0.5f, top = (size.y - h) * 0.5f;
    context.fillRectangle({left + w * 0.04f,left + w * 0.96f,
        top + h * 0.095f,top + h * 0.095f + 0.5f},{0.19f,0.19f,0.19f,1.0f},0.6f);
    auto handle = client.$getFontHandle(); auto& font = handle.getFont();
    auto text = [&](std::string value, float x, float y, float width, float height, float scale, float brightness) {
        TextMeasureData const data{scale,0.0f,false,false,false,::ui::TextAlignment::Left};
        context.drawText(font,{left + x,left + x + width,top + y,top + y + height},std::move(value),{brightness,brightness,brightness,1.0f},
            1.0f,::ui::TextAlignment::Left,data,CaretMeasureData{-1,false});
    };
    auto title = hud::fitHudText(i18n::tr(i18n::TextKey::PreviewOpen),w * 0.82f,
        [&](std::string const& value) { return context.getLineLength(font,value,0.9f,false); });
    text(std::move(title),w * 0.04f,h * 0.035f,w * 0.82f,h * 0.06f,0.9f,1.0f);
    bool const hover = s.pointerX >= kPreviewCloseLeft && s.pointerX <= kPreviewCloseRight
        && s.pointerY >= kPreviewCloseTop && s.pointerY <= kPreviewCloseBottom;
    if (hover || s.closePressed) context.fillRectangle(
        {left + w * kPreviewCloseLeft,left + w * kPreviewCloseRight,top + h * kPreviewCloseTop,top + h * kPreviewCloseBottom},
        {0.13f,0.13f,0.13f,1.0f},1.0f);
    auto const close = std::string{i18n::tr(i18n::TextKey::ButtonClose)};
    auto const closeWidth = context.getLineLength(font,close,0.8f,false);
    text(close,w * (kPreviewCloseLeft + kPreviewCloseRight) * 0.5f - closeWidth * 0.5f,
        h * 0.035f,closeWidth + 1,h * 0.05f,0.8f,1.0f);
    if (!s.error.empty()) text(s.error,w * 0.08f,h * 0.4f,w * 0.84f,h * 0.3f,0.85f,0.8f);
    else if (s.loading) text(i18n::tr(i18n::TextKey::TasksLoading),w * 0.43f,h * 0.45f,w * 0.4f,h * 0.08f,0.85f,0.65f);
    if (s.hasMetadata) {
        auto filename = hud::fitHudText(i18n::format({i18n::TextKey::PreviewFile,{s.name}}),w * 0.92f,
            [&](std::string const& value) { return context.getLineLength(font,value,0.65f,false); });
        text(std::move(filename),w * 0.04f,h * 0.845f,w * 0.92f,h * 0.03f,0.65f,1.0f);
        auto const dimensions = std::to_string(s.dimensions[0]) + "x" + std::to_string(s.dimensions[1])
            + "x" + std::to_string(s.dimensions[2]);
        text(i18n::format({i18n::TextKey::PreviewDimensions,{dimensions}}),
            w * 0.04f,h * 0.88f,w * 0.92f,h * 0.03f,0.65f,1.0f);
        text(i18n::format({i18n::TextKey::PreviewBlocks,{std::to_string(s.blockCount)}}),
            w * 0.04f,h * 0.915f,w * 0.92f,h * 0.03f,0.65f,1.0f);
    }
    auto hint = hud::fitHudText(i18n::tr(i18n::TextKey::PreviewControls),w * 0.92f,
        [&](std::string const& value) { return context.getLineLength(font,value,0.65f,false); });
    auto const hintWidth = context.getLineLength(font,hint,0.65f,false);
    text(std::move(hint),(w - hintWidth) * 0.5f,h * 0.94f,hintWidth + 1,h * 0.05f,0.65f,1.0f);
    context.flushText(0,std::nullopt);
}
}
