#pragma once
#include "ui/hud/HudContent.h"
#include "ui/hud/HudLayout.h"
#include <array>
class ClientInstance;
class UIRenderContext;
namespace lholo::ui::hud {
void paintHudBackground(UIRenderContext& context, PanelPlacement const& bounds, float rounding,
    float opacity);
struct NativeHudFrame {
    float width{}, height{}, lineHeight{};
    std::array<PanelPlacement, settings::kHudPanelCount> panels{};
};
NativeHudFrame paintNativeHud(UIRenderContext& context, ClientInstance& client,
    HudContent content, settings::HudPlacements const& placements, settings::HudPercentages const& opacities, settings::HudPercentages const& scales);
}
