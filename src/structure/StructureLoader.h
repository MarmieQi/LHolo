// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Block;
class CompoundTag;

namespace lholo::structure {

struct LoadedStructure {
    std::filesystem::path                 sourcePath;
    int                                  sizeX{};
    int                                  sizeY{};
    int                                  sizeZ{};
    std::uint64_t                        volume{};
    std::uint64_t                        primaryBlocks{};
    std::uint64_t                        secondaryBlocks{};
    std::uint64_t                        paletteEntries{};
    std::uint64_t                        generation{};
    struct RenderBlock {
        int          x{};
        int          y{};
        int          z{};
        Block const* block{};
        Block const* liquid{};
        std::shared_ptr<CompoundTag const> blockEntityNbt;
    };
    std::vector<RenderBlock>              renderBlocks;
};

void requestOpenGui();
bool isGuiVisible();
bool shouldShowProjectedBlockName();
bool isInputTransitionBlocked();
bool handleGuiHotkeyKeyDown(unsigned int virtualKey);
bool handleGuiHotkeyKeyUp(unsigned int virtualKey);
void resetHotkeyState();
void processPendingHotkeyActions();
bool hasHudInfo();
void renderHud();
// Material-progress HUD (bottom-left): needed vs. what the inventory holds.
void renderMaterialHud();
// JE-style transient hint shown centered above the hotbar for a moment (e.g.
// when manual mode blocks an action, or a placement hotkey is toggled).
void showActionHint(std::string text, std::uint64_t durationMs = 1600);
void renderActionHint();
// True while a hint is still on screen, so the overlay keeps drawing even with
// no projection loaded and the menu closed.
bool actionHintActive();
// One-time consent for the experimental assisted-placement features (they may be
// flagged as cheating by server anti-cheat). Persisted once acknowledged.
bool experimentalConsentGiven();
void setExperimentalConsentGiven(bool given);
// Opt-in on-screen material-progress HUD, toggled from the material-list popup
// (default off). Independent of the main projection HUD toggle.
bool materialHudEnabled();
void setMaterialHudEnabled(bool enabled);
// Material HUD corner: 0 top-left, 1 bottom-left, 2 top-right, 3 bottom-right
// (same encoding as the projection HUD position). Default 1.
int  materialHudPosition();
void setMaterialHudPosition(int position);
// Ask the menu to jump to the experimental page and open the consent popup (used
// by a placement hotkey pressed before consent). feature: 1 manual, 2 easy.
void requestExperimentalConsentPopup(int feature);
int  consumeExperimentalConsentPopupRequest();
void renderGui();
void requestMaterialList();
void processPendingMaterialList();
void loadSettings();
void saveSettings();
std::shared_ptr<LoadedStructure const> getLoaded();
int getRotationQuarterTurns();
int getMirrorMode();
int getOffsetX();
int getOffsetY();
int getOffsetZ();
int getLayerDisplayMode();
int getDisplayLayer();
int getLayerAxis();
void recordProjectionAnchor(int x, int y, int z);
void clear();
// Reload the last saved projection at its saved anchor/transform. Standalone so
// both the menu action and the load hotkey can trigger it.
void restoreSavedProjection();

} // namespace lholo::structure
