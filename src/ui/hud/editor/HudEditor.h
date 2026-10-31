#pragma once
#include "settings/HudPlacement.h"
#include "ui/hud/HudContent.h"
#include <cstddef>

namespace lholo::ui::hud {
struct HudEditorBounds {
    float x{}, y{}, width{}, height{};
    bool contains(float px, float py) const {
        return width > 0 && height > 0 && px >= x && py >= y && px < x + width && py < y + height;
    }
};
enum class HudEditorSlider { None, Opacity, Scale };
struct HudEditorSnapshot {
    std::array<HudEditorBounds, settings::kHudPanelCount> bounds{};
    std::array<HudEditorBounds, 6> buttons{}; // reset all, cancel, done, reset selected, opacity, scale
    HudEditorBounds sliderPopup{}, sliderTrack{};
    HudEditorSlider slider{HudEditorSlider::None};
    bool sliderDragging{};
    int selected{-1}, pressed{-1};
    float pointerX{}, pointerY{};
};
void cancelHudLayoutEditor();
void shutdownHudLayoutEditor();
bool hudLayoutEditorActive();
// True only when completion commits the draft; the app persists it.
bool processHudLayoutEditor(bool beginRequested, bool worldAvailable, bool sceneVisible);
settings::HudPlacements effectiveHudPlacements();
settings::HudPercentages effectiveHudBackgroundOpacities();
settings::HudPercentages effectiveHudScales();
void publishHudEditorBounds(std::size_t index, HudEditorBounds bounds);
void publishHudEditorButtons(std::array<HudEditorBounds, 6> buttons, HudEditorBounds sliderPopup = {}, HudEditorBounds sliderTrack = {});
void moveHudEditorPointer(float x, float y);
void pressHudEditorPointer(bool pressed);
void releaseHudEditorPointer();
HudEditorSnapshot hudEditorSnapshot();
// Presentation-only examples, used when no real content is available.
HudContent makeHudEditorContent(int projectionPosition, int materialPosition);
}
