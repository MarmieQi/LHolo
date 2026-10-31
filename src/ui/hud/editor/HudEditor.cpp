#include "ui/hud/editor/HudEditor.h"
#include "structure/StructureUiState.h"
#include "i18n/Translator.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

namespace lholo::ui::hud {
namespace {
enum class Command { None, Cancel, Finish };
std::atomic<Command> command{};
std::atomic_bool editing{};
std::mutex mutex;
settings::HudPlacements draft{};
settings::HudPercentages draftOpacities{settings::kDefaultHudOpacities};
settings::HudPercentages draftScales{settings::kDefaultHudScales};
HudEditorSnapshot view;
int dragged{-1};
bool pointerHeld{};
float dragX{}, dragY{};
HudEditorBounds dragStart;
void movePanel(std::size_t index, float x, float y) {
    if (index < 2) {
        auto const other = 1 - index;
        if (!draft[other].custom) draft[other] = {true, view.bounds[other].x, view.bounds[other].y};
    }
    draft[index] = {true, std::clamp(x, 0.0f, std::max(0.0f, 1.0f - view.bounds[index].width)),
        std::clamp(y, 0.0f, std::max(0.0f, 1.0f - view.bounds[index].height))};
}
void updateSlider(float x) {
    auto const fraction = std::clamp((x - view.sliderTrack.x) / view.sliderTrack.width, 0.0f, 1.0f);
    if (view.slider == HudEditorSlider::Opacity)
        draftOpacities[view.selected] = static_cast<int>(std::lround(fraction * 100));
    else
        draftScales[view.selected] = 50 + static_cast<int>(std::lround(fraction * 150));
}
void resetPanel(std::size_t index) {
    if (index < 2) {
        auto const other = 1 - index;
        if (!draft[other].custom) draft[other] = {true, view.bounds[other].x, view.bounds[other].y};
    }
    draft[index] = {};
    draftOpacities[index] = settings::kDefaultHudOpacities[index];
    draftScales[index] = settings::kDefaultHudScales[index];
}
auto& state() { return structure::detail::StructureUiState::getInstance(); }
}
void cancelHudLayoutEditor() { command = Command::Cancel; }
bool hudLayoutEditorActive() { return editing.load(); }
void shutdownHudLayoutEditor() {
    std::lock_guard lock(mutex);
    command = Command::None;
    editing = false;
    view = {};
    dragged = -1;
    pointerHeld = false;
}
bool processHudLayoutEditor(bool beginRequested, bool worldAvailable, bool sceneVisible) {
    std::lock_guard lock(mutex);
    auto const action = command.exchange(Command::None);
    if (!worldAvailable || !sceneVisible) { editing = false; return false; }
    if (beginRequested && !editing) {
        draft = state().hudPlacements();
        draftOpacities = state().hudBackgroundOpacities();
        draftScales = state().hudScales();
        view = {};
        dragged = -1;
        pointerHeld = false;
        editing = true;
    }
    if (editing && (action == Command::Finish || action == Command::Cancel)) {
        if (action == Command::Finish) {
            state().setHudPlacements(draft);
            state().setHudBackgroundOpacities(draftOpacities);
            state().setHudScales(draftScales);
        }
        editing = false;
        return action == Command::Finish;
    }
    return false;
}
settings::HudPercentages effectiveHudScales() {
    std::lock_guard lock(mutex);
    return editing ? draftScales : state().hudScales();
}
settings::HudPercentages effectiveHudBackgroundOpacities() {
    std::lock_guard lock(mutex);
    return editing ? draftOpacities : state().hudBackgroundOpacities();
}
settings::HudPlacements effectiveHudPlacements() {
    std::lock_guard lock(mutex);
    return editing ? draft : state().hudPlacements();
}
void publishHudEditorBounds(std::size_t index, HudEditorBounds value) {
    std::lock_guard lock(mutex);
    view.bounds[index] = value;
}
HudEditorSnapshot hudEditorSnapshot() {
    std::lock_guard lock(mutex);
    return view;
}
void publishHudEditorButtons(std::array<HudEditorBounds, 6> buttons, HudEditorBounds sliderPopup, HudEditorBounds sliderTrack) {
    std::lock_guard lock(mutex);
    view.buttons = buttons;
    view.sliderPopup = sliderPopup;
    view.sliderTrack = sliderTrack;
}
void moveHudEditorPointer(float x, float y) {
    std::lock_guard lock(mutex);
    view.pointerX = x; view.pointerY = y;
    if (view.sliderDragging) updateSlider(x);
    if (dragged >= 0) movePanel(dragged, dragStart.x + x - dragX, dragStart.y + y - dragY);
}
void pressHudEditorPointer(bool pressed) {
    std::lock_guard lock(mutex);
    if (!editing) return;
    if (pressed == pointerHeld) return;
    pointerHeld = pressed;
    auto hitButton = [&] {
        for (int index = 0; index < 6; ++index)
            if (view.buttons[index].contains(view.pointerX, view.pointerY)) return index;
        return -1;
    };
    if (pressed) {
        view.pressed = hitButton();
        if (view.slider != HudEditorSlider::None && view.sliderTrack.contains(view.pointerX, view.pointerY)) {
            view.sliderDragging = true;
            updateSlider(view.pointerX);
            return;
        }
        if (view.pressed >= 0 || (view.slider != HudEditorSlider::None && view.sliderPopup.contains(view.pointerX, view.pointerY))) return;
        view.slider = HudEditorSlider::None;
        view.sliderTrack = {};
        view.selected = -1;
        for (int index = 3; index < 6; ++index) view.buttons[index] = {};
        view.sliderPopup = {};
        // Hit-test in reverse shared Painter order, including the independent write panel.
        for (int index = static_cast<int>(settings::kHudPanelCount) - 1; index >= 0; --index) {
            if (!view.bounds[index].contains(view.pointerX, view.pointerY)) continue;
            view.selected = dragged = index;
            dragStart = view.bounds[index]; dragX = view.pointerX; dragY = view.pointerY;
            break;
        }
    } else {
        dragged = -1;
        view.sliderDragging = false;
        if (view.pressed >= 0 && view.pressed == hitButton()) {
            switch (view.pressed) {
            case 0: draft = {}; draftOpacities = settings::kDefaultHudOpacities; draftScales = settings::kDefaultHudScales; break;
            case 1: command = Command::Cancel; break;
            case 2: command = Command::Finish; break;
            case 3: resetPanel(static_cast<std::size_t>(view.selected)); break;
            case 4: view.slider = view.slider == HudEditorSlider::Opacity ? HudEditorSlider::None : HudEditorSlider::Opacity; break;
            case 5: view.slider = view.slider == HudEditorSlider::Scale ? HudEditorSlider::None : HudEditorSlider::Scale; break;
            }
        }
        view.pressed = -1;
    }
}
void releaseHudEditorPointer() {
    std::lock_guard lock(mutex);
    view.sliderDragging = false;
    dragged = view.pressed = -1;
    pointerHeld = false;
}
HudContent makeHudEditorContent(int projectionPosition, int materialPosition) {
    using i18n::TextKey;
    structure::detail::HudStateSnapshot display;
    display.position = projectionPosition;
    display.showProjectedBlockName = false;
    ProjectionHudData example;
    example.fileName = "demo.mcstructure";
    example.progress = {128, 337, 128, 337, 1, 2, 3};
    structure::detail::MaterialHudSnapshot materials;
    materials.ready = true;
    materials.requirements = {
        {"", TextKey::HudLayoutStone, "minecraft:stone", "minecraft:stone", 192},
        {"", TextKey::HudLayoutPlanks, "minecraft:planks", "minecraft:planks", 96},
        {"", TextKey::HudLayoutGlass, "minecraft:glass", "minecraft:glass", 24},
    };
    materials.available = {64, 32, 8};
    return {0, makeProjectionHud(display, example), makeMaterialHud(materials, materialPosition, state().materialHudStackConversion(), state().materialHudMaxItems()),
        makeWriteHud({1, 0, 226270, 471212, true, true, "demo.mcstructure", TextKey::WriteWorldCommandRunning}, state().writeHudPosition(), 0)};
}
}
