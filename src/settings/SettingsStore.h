// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Settings persistence schema. The store only maps the JSON file to/from a
// plain Settings value; applying values to runtime state stays in the caller.

#pragma once

#include "input/HotkeyTypes.h"

#include <Windows.h>

#include <array>
#include <filesystem>
#include <string>

namespace lholo::settings {

struct Settings {
  std::string lastStructurePath;
  float uiScale{2.0f};
  float opacity{1.0f};
  float correctionFillOpacity{0.15f};
  float correctionOutlineOpacity{1.0f};
  bool structureBoundsEnabled{true};
  bool correctionSeeThrough{false};
  bool missingSeeThrough{false};
  bool experimentalConsent{false};
  bool materialHudEnabled{false};
  int materialHudPosition{3};
  int placementRadius{4};
  int autoPlacementBreakCooldownSeconds{10};
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
  int guiHotkey{'M'};
  int guiHotkeyModifiers{2};
  int structureOffsetHotkey{VK_LMENU};
  int structureOffsetHotkeyModifiers{0};
  int layerIncreaseHotkey{0x26}; // VK_UP
  int layerDecreaseHotkey{0x28}; // VK_DOWN
  int layerIncreaseHotkeyModifiers{2};
  int layerDecreaseHotkeyModifiers{2};
  int loadProjectionHotkey{0};
  int loadProjectionHotkeyModifiers{0};
  int closeProjectionHotkey{0};
  int closeProjectionHotkeyModifiers{0};
  // X-, X+, Z-, Z+, Y+, Y-
  std::array<int, input::kMoveHotkeyCount> moveHotkeys{0x25, 0x27, 0x26,
                                                       0x28, 0x26, 0x28};
  std::array<int, input::kMoveHotkeyCount> moveHotkeyModifiers{1, 1, 1,
                                                               1, 4, 4};
  bool hasSavedProjection{false};
  int savedAnchorX{};
  int savedAnchorY{};
  int savedAnchorZ{};
  int savedRotation{};
  int savedMirror{};
  int savedOffsetX{};
  int savedOffsetY{};
  int savedOffsetZ{};
  int savedLayerDisplayMode{};
  int savedDisplayLayer{};
  int savedLayerAxis{};
  std::string savedStructurePath;
};

// Returns false when the file does not exist. Throws on read or parse errors.
bool loadSettingsFile(std::filesystem::path const &path, Settings &out);

void saveSettingsFile(std::filesystem::path const &path,
                      Settings const &settings);

} // namespace lholo::settings
