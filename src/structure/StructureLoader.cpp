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

#include "structure/StructureLoader.h"
#include "input/MaterialHudPageRequest.h"
#include "input/MenuOpenRequest.h"

#include "i18n/Message.h"
#include "input/ViewMoveBasis.h"
#include "input/HotkeyCapture.h"
#include "settings/SettingsStore.h"
#include "structure/MaterialTracker.h"
#include "structure/StructureFileLoader.h"
#include "structure/StructureSession.h"
#include "structure/StructurePaths.h"
#include "structure/StructureUiState.h"
#include "ui/HotkeyFormat.h"
#include "structure/ProjectionActions.h"
#include "structure/TransformActions.h"
#include "ui/MenuController.h"
#include "structure/capture/StructureCapture.h"
#include "structure/capture/CaptureActions.h"
#include "structure/java_to_bedrock/JavaToBedrock.h"
#include "ui/FluentTheme.h"
#include "ui/LHoloMenu.h"
#include "place/PlaceHelper.h"
#include "plugin/LHolo.h"
#include "projection/Projection.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include <Windows.h>

#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "imgui.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/Level.h"

namespace lholo::structure {
namespace {

constexpr std::size_t kGuiHotkeyIndex = input::hotkeyIndex(input::HotkeyId::Gui);
constexpr std::size_t kLayerIncreaseHotkeyIndex = input::hotkeyIndex(input::HotkeyId::LayerIncrease);
constexpr std::size_t kLayerDecreaseHotkeyIndex = input::hotkeyIndex(input::HotkeyId::LayerDecrease);
constexpr float kActionHintVerticalScreenRatio = 0.80f;
// Background alpha for the legacy transient action hint. Native HUD styling
// belongs to ui/hud/NativeHudRenderer.
constexpr float kOverlayWindowBgAlpha = 0.68f;
auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

auto& uiState() {
    return detail::StructureUiState::getInstance();
}

std::filesystem::path settingsPath() {
    return LHolo::getInstance().getSelf().getConfigDir() / "config.json";
}

unsigned int currentHotkeyModifiers() {
    return uiState().currentHotkeyModifiers();
}

// Resolve one move hotkey into a world-space step and queue it. The direction
// depends on where the player is facing, so it is resolved here - the only
// layer that may touch game objects - while the rules themselves live in the
// pure input/ViewMoveBasis module the logic tests cover.
bool queueViewRelativeMove(input::HotkeyId move) {
    auto client = ll::service::getClientInstance();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player) return false;
    auto const step = input::viewRelativeMoveStep(move, player->getRotation().y);
    if (!step.valid) return false;
    uiState().queueOffsetDelta(step.dx, step.dy, step.dz);
    return true;
}

} // namespace

void requestMaterialList() {
    detail::requestMaterialListRefresh();
}

void closeLegacyGui() {
    uiState().setGuiVisible(false);
    uiState().setBlockGameInputUntil(GetTickCount64() + 180);
}

bool isGuiVisible() { return uiState().guiVisible(); }

bool shouldShowProjectedBlockName() {
    auto const hud = uiState().hud();
    return hud.enabled && hud.showProjectedBlockName;
}

bool isInputTransitionBlocked() {
    return GetTickCount64() <= uiState().blockGameInputUntil();
}

bool isMenuInputCaptured() {
    return isGuiVisible() || isInputTransitionBlocked();
}

bool handleGuiHotkeyKeyDown(unsigned int virtualKey) {
    auto const modifierKey = ui::isModifierKey(virtualKey);
    if (virtualKey == VK_CONTROL || virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL) {
        uiState().setControlHeld(true);
    } else if (virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU) {
        uiState().setAltHeld(true);
    } else if (virtualKey == VK_SHIFT || virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT) {
        uiState().setShiftHeld(true);
    }

    if (input::hotkeyCaptureActive())
        return input::captureHotkeyKey(virtualKey, currentHotkeyModifiers());

    // The projection-offset trigger is the fixed Alt key. Only claim the bare
    // key while the gesture is enabled and an active projection can actually
    // consume its wheel; otherwise preserve Minecraft and system Alt handling
    // unchanged.
    if ((virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU)
        && uiState().altWheelOffsetEnabled()
        && detail::StructureSession::getInstance().hasLoaded()) {
        return true;
    }

    if (modifierKey) return false;

    auto const modifiers = currentHotkeyModifiers();
    auto const guiHotkey = uiState().inputHotkey(kGuiHotkeyIndex);
    if (guiHotkey.key != 0 && virtualKey == guiHotkey.key
        && modifiers == guiHotkey.modifiers) {
        if (GetTickCount64() >= uiState().ignoreHotkeyUntil()
            && uiState().tryPressHotkey(kGuiHotkeyIndex)) {
            input::requestMenuOpen();
        }
        return true;
    }
    if (isGuiVisible()) return false;

    for (std::size_t index = 0; index < input::kMoveHotkeyCount; ++index) {
        auto const hotkey = uiState().inputHotkey(index + input::kMoveHotkeyFirst);
        if (hotkey.key == virtualKey && hotkey.modifiers == modifiers) {
            if (GetTickCount64() >= uiState().ignoreHotkeyUntil()
                && uiState().tryPressHotkey(index + input::kMoveHotkeyFirst)) {
                queueViewRelativeMove(
                    static_cast<input::HotkeyId>(index + input::kMoveHotkeyFirst)
                );
            }
            return true;
        }
    }

    auto const layerIncreaseHotkey = uiState().inputHotkey(kLayerIncreaseHotkeyIndex);
    if (layerIncreaseHotkey.key != 0 && virtualKey == layerIncreaseHotkey.key
        && modifiers == layerIncreaseHotkey.modifiers) {
        if (!detail::StructureSession::getInstance().layerDisplayEnabled()) return false;
        if (GetTickCount64() >= uiState().ignoreHotkeyUntil()
            && uiState().tryPressHotkey(kLayerIncreaseHotkeyIndex)) {
            uiState().queueLayerDelta(1);
        }
        return true;
    }
    auto const layerDecreaseHotkey = uiState().inputHotkey(kLayerDecreaseHotkeyIndex);
    if (layerDecreaseHotkey.key != 0 && virtualKey == layerDecreaseHotkey.key
        && modifiers == layerDecreaseHotkey.modifiers) {
        if (!detail::StructureSession::getInstance().layerDisplayEnabled()) return false;
        if (GetTickCount64() >= uiState().ignoreHotkeyUntil()
            && uiState().tryPressHotkey(kLayerDecreaseHotkeyIndex)) {
            uiState().queueLayerDelta(-1);
        }
        return true;
    }
    if (uiState().materialHudEnabled() && detail::StructureSession::getInstance().hasLoaded()
        && !projection::isDimensionSuspended()) {
        for (auto const id : {input::HotkeyId::MaterialHudPreviousPage, input::HotkeyId::MaterialHudNextPage}) {
            auto const slot = input::hotkeyIndex(id);
            auto const hotkey = uiState().inputHotkey(slot);
            if (hotkey.key == 0 || virtualKey != hotkey.key || modifiers != hotkey.modifiers) continue;
            if (GetTickCount64() >= uiState().ignoreHotkeyUntil() && uiState().tryPressHotkey(slot))
                input::requestMaterialHudPage(id == input::HotkeyId::MaterialHudPreviousPage ? -1 : 1);
            return true;
        }
    }
    return false;
}

bool handleGuiHotkeyKeyUp(unsigned int virtualKey) {
    if (virtualKey == VK_CONTROL || virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL) {
        uiState().setControlHeld(false);
        return false;
    }
    if (virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU) {
        uiState().setAltHeld(false);
        return false;
    }
    if (virtualKey == VK_SHIFT || virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT) {
        uiState().setShiftHeld(false);
        return false;
    }

    return uiState().releaseHotkeysForKey(virtualKey, GetTickCount64());
}

bool handleProjectionOffsetWheel(short wheelDelta) {
    if (isGuiVisible() || !uiState().altWheelOffsetEnabled() || !uiState().altHeld()
        || !detail::StructureSession::getInstance().hasLoaded()) {
        return false;
    }

    auto client = ll::service::getClientInstance();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player) return false;

    auto const steps = static_cast<int>(wheelDelta) / WHEEL_DELTA;
    if (steps == 0) return false;
    auto const view = player->getViewVector(1.0f);
    auto const step = input::viewForwardStep(view.x, view.y, view.z, steps);
    if (!step.valid) return false;
    uiState().queueOffsetDelta(step.dx, step.dy, step.dz);
    return true;
}

void resetHotkeyState() {
    uiState().resetHotkeyState();
}

void processPendingActions() {
    if (projection::consumeWorldExitRequest()) {
        place::resetWorldSession();
        capture::clear();
        resetWorldSession();
        showActionHint(
            i18n::Message{i18n::TextKey::ActionHintWorldExited},
            kProjectionLifecycleHintDurationMs
        );
        return;
    }

    auto& session = detail::StructureSession::getInstance();
    auto const pending = uiState().consumePendingHotkeyActions();
    auto const layerActionEnabled = pending.layerDelta != 0
        && session.transform().layerDisplayMode != LayerDisplayMode::All;
    bool changed = pending.offsetX != 0 || pending.offsetY != 0 || pending.offsetZ != 0 || layerActionEnabled;
    session.adjustOffsets(pending.offsetX, pending.offsetY, pending.offsetZ);
    if (layerActionEnabled) session.adjustDisplayLayer(pending.layerDelta);

    processProjectionActions();
    changed = processTransformEdits() || changed;
    capture::processActions();

    auto loaded = session.loaded();
    if (pending.settingsSave || (changed && (!loaded || !loaded->composed))) saveSettings();
}

void resetDimensionSession() {
    uiState().clearMaterialHud();
}

bool experimentalConsentGiven() {
    return uiState().experimentalConsentGiven();
}

void setExperimentalConsentGiven(bool given) {
    uiState().setExperimentalConsentGiven(given);
}

bool materialHudEnabled() {
    return uiState().materialHudEnabled();
}

void setMaterialHudEnabled(bool enabled) {
    uiState().setMaterialHudEnabled(enabled);
}

int materialHudPosition() {
    return uiState().materialHudPosition();
}

void setMaterialHudPosition(int position) {
    uiState().setMaterialHudPosition(position);
}

void showActionHint(i18n::Message message, std::uint64_t durationMs) {
    uiState().setActionHint(std::move(message), GetTickCount64() + durationMs);
}

i18n::Message makeLoadedStatusMessage(LoadedStructure const& loaded) {
    char sizeText[64]{};
    std::snprintf(
        sizeText, sizeof(sizeText), "%d x %d x %d", loaded.sizeX, loaded.sizeY, loaded.sizeZ
    );
    char blocksText[96]{};
    std::snprintf(
        blocksText, sizeof(blocksText), "%llu  |  Palette %llu",
        static_cast<unsigned long long>(loaded.renderBlocks.size()),
        static_cast<unsigned long long>(loaded.paletteEntries)
    );
    return {
        i18n::TextKey::StatusLoaded,
        {detail::pathToUtf8(loaded.sourcePath.filename()), sizeText, blocksText}
    };
}

bool actionHintActive() {
    return uiState().actionHintActive(GetTickCount64());
}

void renderActionHint() {
    auto const now = GetTickCount64();
    auto const hint = uiState().actionHint(now);
    auto const expiry = hint.expiry;
    if (!hint.persistent && now >= expiry) return;
    if (hint.text.empty()) return;

    float const alpha = !hint.persistent && expiry - now < 300
        ? static_cast<float>(expiry - now) / 300.0f : 1.0f;
    auto const displaySize = ImGui::GetIO().DisplaySize;
    float const uiScale = std::clamp(
        std::min(displaySize.x / 1920.0f, displaySize.y / 1080.0f), 1.0f, 5.0f
    );
    auto const metrics = lholo::ui::calculateMetrics(displaySize, uiScale);
    lholo::ui::applyFluentTheme(metrics);

    // Centered horizontally, sitting just above the hotbar like JE's action bar.
    ImGui::SetNextWindowPos(
        ImVec2(displaySize.x * 0.5f, displaySize.y * kActionHintVerticalScreenRatio),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.5f)
    );
    // Same neutral background as the HUDs: this bar used to carry its own
    // purple accent, which no longer matched either the Fluent theme or the
    // HUD next to it. Only the fade alpha is specific to the hint.
    ImGui::SetNextWindowBgAlpha(kOverlayWindowBgAlpha * alpha);
    auto hintText = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    hintText.w *= alpha;
    ImGui::PushStyleColor(ImGuiCol_Text, hintText);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, metrics.rounding * 0.6f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding, ImVec2(metrics.sectionPadding, metrics.gap)
    );
    constexpr auto flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavInputs
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##LHoloActionHint", nullptr, flags)) {
        ImGui::TextUnformatted(hint.text.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void renderGui() {
    lholo::ui::renderStructureMenu();
}

void loadSettings() {
    auto const path = settingsPath();
    try {
        auto& session = detail::StructureSession::getInstance();
        lholo::settings::Settings settings;
        if (!lholo::settings::loadSettingsFile(path, settings)) {
            saveSettings();
            return;
        }
        if (!i18n::setLanguageByCode(settings.language)) {
            logger().warn(
                "Invalid interface language '{}'; using '{}'",
                settings.language,
                i18n::kDefaultLanguageCode
            );
        }
        projection::setCorrectionFillOpacity(settings.correctionFillOpacity);
        projection::setCorrectionOutlineOpacity(settings.correctionOutlineOpacity);
        projection::setStructureBoundsEnabled(settings.structureBoundsEnabled);
        projection::setCorrectionSeeThrough(settings.correctionSeeThrough);
        projection::setMissingSeeThrough(settings.missingSeeThrough);
        setExperimentalConsentGiven(settings.experimentalConsent);
        setMaterialHudEnabled(settings.materialHudEnabled);
        uiState().setWriteHudEnabled(settings.writeHudEnabled);
        uiState().setWriteHudPosition(settings.writeHudPosition);
        uiState().setMaterialHudStackConversion(settings.materialHudStackConversion);
        uiState().setMaterialHudMaxItems(settings.materialHudMaxItems);
        setMaterialHudPosition(settings.materialHudPosition);
        // Placement and transforms are owned by tasks; global config is preferences only.
        session.resetTransform();
        auto hud = uiState().hud();
        hud.enabled = settings.hudEnabled;
        hud.showFileName = settings.hudShowFileName;
        hud.showLayer = settings.hudShowLayer;
        hud.showOverallProgress = settings.hudShowOverallProgress;
        hud.showProgress = settings.hudShowProgress;
        hud.showWrongState = settings.hudShowWrongState;
        hud.showWrongType = settings.hudShowWrongType;
        hud.showExtraBlocks = settings.hudShowExtraBlocks;
        hud.showProjectedBlockName = settings.hudShowProjectedBlockName;
        hud.position = std::clamp(settings.hudPosition, 0, 3);
        uiState().applyHud(hud);
        uiState().setHudPlacements(settings.hudPlacements);
        uiState().setHudBackgroundOpacities(settings.hudBackgroundOpacities);
        uiState().setHudScales(settings.hudScales);
        // Assisted-placement modes are intentionally session-only. Ignore
        // legacy persisted values and always begin a new game session disabled.
        place::setEnabled(false);
        place::setManualMode(false);
        place::setRangeEnabled(false);
        place::setPlacementRadius(std::clamp(settings.placementRadius, 1, 4));
        place::setAutoPlacementBreakCooldownSeconds(
            std::clamp(settings.autoPlacementBreakCooldownSeconds, 0, 60)
        );
        uiState().setHotkey(
            kGuiHotkeyIndex,
            std::clamp(settings.guiHotkey, 0, 255),
            std::clamp(settings.guiHotkeyModifiers, 0, 7)
        );
        uiState().setAltWheelOffsetEnabled(settings.altWheelOffsetEnabled);
        uiState().setHotkey(input::hotkeyIndex(input::HotkeyId::MaterialHudPreviousPage),
            std::clamp(settings.materialHudPreviousPageHotkey, 0, 255), std::clamp(settings.materialHudPreviousPageHotkeyModifiers, 0, 7));
        uiState().setHotkey(input::hotkeyIndex(input::HotkeyId::MaterialHudNextPage),
            std::clamp(settings.materialHudNextPageHotkey, 0, 255), std::clamp(settings.materialHudNextPageHotkeyModifiers, 0, 7));
        uiState().setHotkey(
            kLayerIncreaseHotkeyIndex,
            std::clamp(settings.layerIncreaseHotkey, 0, 255),
            std::clamp(settings.layerIncreaseHotkeyModifiers, 0, 7)
        );
        uiState().setHotkey(
            kLayerDecreaseHotkeyIndex,
            std::clamp(settings.layerDecreaseHotkey, 0, 255),
            std::clamp(settings.layerDecreaseHotkeyModifiers, 0, 7)
        );
        for (std::size_t index = 0; index < input::kMoveHotkeyCount; ++index) {
            uiState().setHotkey(
                index + input::kMoveHotkeyFirst,
                std::clamp(settings.moveHotkeys[index], 0, 255),
                std::clamp(settings.moveHotkeyModifiers[index], 0, 7)
            );
        }
        saveSettings(); // Canonicalize valid legacy preferences once loaded.
        logger().info("Loaded projection settings from {}", path.string());
    } catch (std::exception const& exception) {
        logger().error("Could not load projection settings {}: {}", path.string(), exception.what());
    }
}

void saveSettings() {
    auto const path = settingsPath();
    try {
        auto& session = detail::StructureSession::getInstance();
        // Hidden legacy restore state remains in memory, outside user preferences.
        session.refreshSavedTransformIfActive();
        auto const hud = uiState().hud();
        lholo::settings::Settings settings;
        settings.language = std::string{i18n::languageCode(i18n::language())};
        settings.correctionFillOpacity = projection::getCorrectionFillOpacity();
        settings.correctionOutlineOpacity = projection::getCorrectionOutlineOpacity();
        settings.structureBoundsEnabled = projection::getStructureBoundsEnabled();
        settings.correctionSeeThrough = projection::getCorrectionSeeThrough();
        settings.missingSeeThrough = projection::getMissingSeeThrough();
        settings.experimentalConsent = experimentalConsentGiven();
        settings.materialHudEnabled = materialHudEnabled();
        settings.materialHudStackConversion = uiState().materialHudStackConversion();
        settings.materialHudMaxItems = uiState().materialHudMaxItems();
        settings.materialHudPosition = materialHudPosition();
        settings.placementRadius = place::getPlacementRadius();
        settings.autoPlacementBreakCooldownSeconds
            = place::getAutoPlacementBreakCooldownSeconds();
        settings.hudEnabled = hud.enabled;
        settings.hudShowFileName = hud.showFileName;
        settings.hudShowLayer = hud.showLayer;
        settings.hudShowOverallProgress = hud.showOverallProgress;
        settings.hudShowProgress = hud.showProgress;
        settings.hudShowWrongState = hud.showWrongState;
        settings.hudShowWrongType = hud.showWrongType;
        settings.hudShowExtraBlocks = hud.showExtraBlocks;
        settings.hudShowProjectedBlockName = hud.showProjectedBlockName;
        settings.hudPosition = hud.position;
        settings.writeHudEnabled = uiState().writeHudEnabled();
        settings.writeHudPosition = uiState().writeHudPosition();
        settings.hudPlacements = uiState().hudPlacements();
        settings.hudBackgroundOpacities = uiState().hudBackgroundOpacities();
        settings.hudScales = uiState().hudScales();
        auto const guiHotkey = uiState().hotkey(kGuiHotkeyIndex);
        auto const layerIncreaseHotkey = uiState().hotkey(kLayerIncreaseHotkeyIndex);
        auto const layerDecreaseHotkey = uiState().hotkey(kLayerDecreaseHotkeyIndex);
        settings.guiHotkey = guiHotkey.key;
        settings.guiHotkeyModifiers = guiHotkey.modifiers;
        settings.layerIncreaseHotkey = layerIncreaseHotkey.key;
        settings.layerDecreaseHotkey = layerDecreaseHotkey.key;
        settings.layerIncreaseHotkeyModifiers = layerIncreaseHotkey.modifiers;
        settings.layerDecreaseHotkeyModifiers = layerDecreaseHotkey.modifiers;
        for (std::size_t index = 0; index < settings.moveHotkeys.size(); ++index) {
            auto const moveHotkey = uiState().hotkey(index + input::kMoveHotkeyFirst);
            settings.moveHotkeys[index] = moveHotkey.key;
            settings.moveHotkeyModifiers[index] = moveHotkey.modifiers;
        }
        auto const previousPage = uiState().hotkey(input::hotkeyIndex(input::HotkeyId::MaterialHudPreviousPage));
        auto const nextPage = uiState().hotkey(input::hotkeyIndex(input::HotkeyId::MaterialHudNextPage));
        settings.materialHudPreviousPageHotkey = previousPage.key;
        settings.materialHudPreviousPageHotkeyModifiers = previousPage.modifiers;
        settings.materialHudNextPageHotkey = nextPage.key;
        settings.materialHudNextPageHotkeyModifiers = nextPage.modifiers;
        settings.altWheelOffsetEnabled = uiState().altWheelOffsetEnabled();
        lholo::settings::saveSettingsFile(path, settings);
    } catch (std::exception const& exception) {
        logger().error("Could not save projection settings {}: {}", path.string(), exception.what());
    }
}

std::shared_ptr<LoadedStructure const> getLoaded() {
    return detail::StructureSession::getInstance().loaded();
}

int getRotationQuarterTurns() {
    return detail::StructureSession::getInstance().transform().rotation;
}

int getMirrorMode() {
    return std::clamp(detail::StructureSession::getInstance().transform().mirror, 0, 2);
}

int getOffsetX() { return detail::StructureSession::getInstance().transform().offsetX; }
int getOffsetY() { return detail::StructureSession::getInstance().transform().offsetY; }
int getOffsetZ() { return detail::StructureSession::getInstance().transform().offsetZ; }
LayerDisplayMode getLayerDisplayMode() {
    return detail::StructureSession::getInstance().transform().layerDisplayMode;
}
int getDisplayLayer() { return detail::StructureSession::getInstance().transform().displayLayer; }
LayerAxis getLayerAxis() { return detail::StructureSession::getInstance().transform().layerAxis; }

void recordProjectionAnchor(int x, int y, int z) {
    auto loaded = getLoaded();
    if (loaded && loaded->composed) return;
    detail::StructureSession::getInstance().recordProjectionAnchor(x, y, z);
    saveSettings();
}

// Hotbar lock for the Alt+wheel projection offset: engages only while the
// gesture is enabled, a projection is loaded AND the Alt key is held.
// Deliberately reads the same event-tracked Alt state the wheel handler uses,
// so the lock and the projection move engage under exactly the same condition
// and cost nothing while idle. The mouse-input hook (input/MenuInputGuard)
// consults this to suppress wheel-driven hotbar changes at the Bedrock input
// boundary, so disabling the gesture releases the wheel immediately instead of
// waiting for a key-up.
bool scrollLockActive() {
    return getLoaded() != nullptr && uiState().altWheelOffsetEnabled() && uiState().altHeld();
}

namespace {

void clearProjectionSession(i18n::Message status) {
    // Withdraw the requested structure before waiting for the mesh worker.
    // Otherwise the render hook can observe the old loaded structure in the gap after
    // projection::disable() and immediately enable the projection again.
    detail::StructureSession::getInstance().clearLoaded(std::move(status));

    // The active projection and in-flight worker keep non-owning Block pointers
    // into the Java mapper registry. Stop them before releasing that registry.
    projection::disable();
    resetJavaBlockMappingCache();
}

} // namespace

void resetWorldSession() {
    resetProjectionActions();
    resetTransformEdits();
    capture::resetActions();
    clearProjectionSession(i18n::Message{i18n::TextKey::StatusWorldExited});
    uiState().resetWorldSession();
}

void clear() {
    clearProjectionSession(i18n::Message{i18n::TextKey::StatusProjectionClosed});
    uiState().clearLoadingHint();
    uiState().clearMaterials();
}

} // namespace lholo::structure
