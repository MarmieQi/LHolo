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

inline constexpr std::uint64_t kProjectionLifecycleHintDurationMs = 2500;

struct LoadedStructure {
    struct RegionBox {
        int x{};
        int y{};
        int z{};
        int sizeX{};
        int sizeY{};
        int sizeZ{};
    };

    std::filesystem::path                 sourcePath;
    int                                  sizeX{};
    int                                  sizeY{};
    int                                  sizeZ{};
    std::uint64_t                        volume{};
    std::uint64_t                        primaryBlocks{};
    std::uint64_t                        secondaryBlocks{};
    std::uint64_t                        paletteEntries{};
    std::uint64_t                        generation{};
    std::size_t                          materialCount{};
    // Cells covered by the source format. mcstructure contributes one box;
    // litematic contributes one per region so gaps between regions are not
    // mistaken for schematic air by projection correction.
    std::vector<RegionBox>               regions;
    struct RenderBlock {
        int          x{};
        int          y{};
        int          z{};
        Block const* block{};
        Block const* liquid{};
        std::shared_ptr<CompoundTag const> blockEntityNbt;
        int          materialIndex{-1};
        int          liquidMaterialIndex{-1};
    };
    std::vector<RenderBlock>              renderBlocks;
};

void requestOpenGui();
bool isGuiVisible();
bool shouldShowProjectedBlockName();
bool isInputTransitionBlocked();
// True while the LHolo menu owns keyboard and mouse input. Keep input guards
// on every platform-facing path behind this single policy predicate.
bool isMenuInputCaptured();
bool handleGuiHotkeyKeyDown(unsigned int virtualKey);
bool handleGuiHotkeyKeyUp(unsigned int virtualKey);
bool handleProjectionOffsetWheel(short wheelDelta);
void resetHotkeyState();
// Present-frame control plane for deferred world-lifecycle and hotkey actions.
void processPendingActions();
// Invalidates world-derived HUD snapshots after a dimension change while
// preserving the loaded structure and all HUD preferences.
void resetDimensionSession();
bool hasHudInfo();
void renderHud();
// Current-visible-range material HUD: missing projected cells vs. inventory.
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
// Opt-in on-screen material-progress HUD, configured on the HUD settings page
// (default off). Independent of the main projection HUD toggle.
bool materialHudEnabled();
void setMaterialHudEnabled(bool enabled);
// Material HUD corner: 0 top-left, 1 bottom-left, 2 top-right, 3 bottom-right
// (same encoding as the projection HUD position). Default 3.
int  materialHudPosition();
void setMaterialHudPosition(int position);
void renderGui();
void requestMaterialList();
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
