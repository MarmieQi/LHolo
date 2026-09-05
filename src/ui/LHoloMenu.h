// LHolo - Fluent-style menu
#pragma once

#include "ui/FluentTheme.h"
#include "input/HotkeyTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lholo::ui {

using HotkeyId = input::HotkeyId;

enum class MenuPage : std::uint8_t {
    Projection,
    CreateStructure,
    Transform,
    Render,
    Hud,
    Hotkeys,
    UiScale,
    Experimental
};

enum class CapturePointId : std::uint8_t { First, Second };

struct CapturePointModel {
    bool set{};
    int  x{};
    int  y{};
    int  z{};
};

struct CaptureDraftModel {
    int               mode{};
    CapturePointModel first;
    CapturePointModel second;
    bool              includeEntities{};
};

struct HotkeyRow {
    HotkeyId    id{};
    std::string label;
    std::string display;
    bool        capturing{};
};

struct MaterialRow {
    std::string displayName;
    std::string typeName;
    std::uint64_t count{};
    int stackSize{64};
};

struct MenuModel {
    MenuPage page{MenuPage::Projection};
    char* pathBuffer{};
    std::size_t pathBufferSize{};
    bool blockOpeningInput{};
    std::string status;
    bool hasLoadedStructure{};
    bool hasSavedProjection{};
    int savedAnchorX{};
    int savedAnchorY{};
    int savedAnchorZ{};
    float uiScale{1.0f};

    CaptureDraftModel capture;
    std::uint64_t     captureRevision{};
    bool              captureWorldAvailable{};
    std::string       captureStatus;

    bool structureBoundsEnabled{};
    bool correctionSeeThrough{};
    bool missingSeeThrough{};
    bool easyPlaceEnabled{};
    bool manualPlace{};
    bool rangeEnabled{};
    bool experimentalConsent{};
    int placementRadius{4};
    int autoPlacementBreakCooldownSeconds{10};
    int offsetX{};
    int offsetY{};
    int offsetZ{};
    int rotation{};
    int mirror{};

    float opacity{1.0f};
    float correctionFillOpacity{0.15f};
    float correctionOutlineOpacity{1.0f};
    int layerAxis{};
    int layerDisplayMode{};
    int displayLayer{};
    int maxLayerY{};
    int maxLayerX{};
    int materialCount{};

    std::array<HotkeyRow, input::kHotkeyCount> hotkeys{};
    bool hudEnabled{true};
    int hudPosition{1};
    bool hudShowFileName{true};
    bool hudShowLayer{true};
    bool hudShowOverallProgress{false};
    bool hudShowProgress{true};
    bool hudShowWrongState{true};
    bool hudShowWrongType{true};
    bool hudShowExtraBlocks{true};
    bool hudShowProjectedBlockName{true};

    std::vector<MaterialRow> materials;
    bool materialPopupRequested{};
    bool materialHudEnabled{};
    int  materialHudPosition{3};
    bool closeRequested{};
};

struct MenuActions {
    std::function<std::optional<std::string>(std::string_view)> browseStructure;
    std::function<void(std::string_view)> loadStructure;
    std::function<void()> restoreProjection;
    std::function<void()> closeProjection;
    std::function<void()> requestMaterials;
    std::function<void(HotkeyId)> beginHotkeyCapture;
    std::function<void(HotkeyId)> clearHotkey;
    std::function<void(HotkeyId)> resetHotkey;
    std::function<void()> resetHotkeys;
    std::function<void()> resetCorrectionStyle;
    std::function<void()> giveExperimentalConsent;
    std::function<void(CapturePointId)> usePlayerCapturePosition;
    std::function<void()> clearCapture;
    std::function<void(CaptureDraftModel const&)> exportCapture;
};

void renderMenu(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics);

} // namespace lholo::ui
