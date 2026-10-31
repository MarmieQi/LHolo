// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Settings persistence schema. The store only maps the JSON file to/from a
// plain Settings value; applying values to runtime state stays in the caller.

#pragma once

#include "settings/MaterialHudSettings.h"

#include "input/HotkeyTypes.h"
#include "settings/HudPlacement.h"

#include <array>
#include <filesystem>
#include <string>

namespace lholo::settings {

inline constexpr int kSettingsVersion = 13;

struct Settings {
    // Interface language locale code, for example "zh_CN" or "en_US".
    // Runtime language indices are never persisted.
    std::string language{"zh_CN"};
    // Rendering preferences.
    float correctionFillOpacity{0.15f};
    float correctionOutlineOpacity{1.0f};
    bool structureBoundsEnabled{true};
    bool correctionSeeThrough{false};
    bool missingSeeThrough{false};
    // Placement consent and parameters; live placement modes are never saved.
    bool experimentalConsent{false};
    int placementRadius{4};
    int autoPlacementBreakCooldownSeconds{10};

    // Material and projection HUD preferences.
    bool writeHudEnabled{true};
    int writeHudPosition{kTopCenterHudPosition};
    bool materialHudEnabled{false};
    bool materialHudStackConversion{false};
    int materialHudMaxItems{kDefaultMaterialHudItems};
    int materialHudPosition{3};
    bool hudEnabled{true};
    bool hudShowFileName{true};
    bool hudShowLayer{true};
    bool hudShowOverallProgress{false};
    bool hudShowProgress{true};
    bool hudShowWrongState{true};
    bool hudShowWrongType{true};
    bool hudShowExtraBlocks{true};
    bool hudShowProjectedBlockName{true};
    int hudPosition{1};
    HudPlacements hudPlacements{};
    settings::HudPercentages hudBackgroundOpacities{settings::kDefaultHudOpacities};
    settings::HudPercentages hudScales{settings::kDefaultHudScales};
    // Input bindings use native virtual-key values internally only.
    int guiHotkey{'M'};
    int guiHotkeyModifiers{2};
    int layerIncreaseHotkey{0x26}; // VK_UP
    int layerDecreaseHotkey{0x28}; // VK_DOWN
    int layerIncreaseHotkeyModifiers{2};
    int layerDecreaseHotkeyModifiers{2};
    int materialHudPreviousPageHotkey{}, materialHudPreviousPageHotkeyModifiers{};
    int materialHudNextPageHotkey{}, materialHudNextPageHotkeyModifiers{};
    // Fixed-gesture input, not a rebindable slot: while enabled, holding Alt
    // claims the wheel for the projection offset and locks the hotbar for as
    // long as Alt is down. The hotkeys page can turn it off so Alt and the
    // wheel go back to the game.
    bool altWheelOffsetEnabled{true};
    // Move slots in HotkeyId order: left, right, forward, backward, up, down.
    // The first four follow the player's facing; the JSON keys below keep the
    // names the slots had when they were world-axis offsets.
    std::array<int, input::kMoveHotkeyCount> moveHotkeys{0x25, 0x27, 0x26, 0x28, 0x26, 0x28};
    std::array<int, input::kMoveHotkeyCount> moveHotkeyModifiers{1, 1, 1, 1, 4, 4};
    bool operator==(Settings const&) const = default;
};

// Reads current or legacy preferences without modifying the file.
// Returns false when absent; throws on invalid data without changing out.
bool loadSettingsFile(std::filesystem::path const& path, Settings& out);

// Writes only the canonical current schema, backs up legacy input once, and
// atomically replaces the file. Unknown future versions are never overwritten.
void saveSettingsFile(std::filesystem::path const& path, Settings const& settings);

} // namespace lholo::settings
