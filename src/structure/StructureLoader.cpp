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

#include "place/PlaceHelper.h"
#include "plugin/LHolo.h"
#include "projection/Projection.h"
#include "settings/SettingsStore.h"
#include "structure/MaterialTracker.h"
#include "structure/StructurePaths.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"
#include "structure/capture/StructureCapture.h"
#include "structure/formats/StructureFormatLoaders.h"
#include "structure/java_to_bedrock/JavaToBedrock.h"
#include "ui/FileDialog.h"
#include "ui/FluentTheme.h"
#include "ui/HotkeyFormat.h"
#include "ui/LHoloMenu.h"
#include "ui/MenuController.h"
#include "ui/MenuWidgets.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
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

#include "imgui.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/Level.h"

namespace lholo::structure {
namespace {

constexpr std::size_t kGuiHotkeyIndex =
    input::hotkeyIndex(input::HotkeyId::Gui);
constexpr std::size_t kStructureOffsetHotkeyIndex =
    input::hotkeyIndex(input::HotkeyId::StructureOffset);
constexpr std::size_t kLayerIncreaseHotkeyIndex =
    input::hotkeyIndex(input::HotkeyId::LayerIncrease);
constexpr std::size_t kLayerDecreaseHotkeyIndex =
    input::hotkeyIndex(input::HotkeyId::LayerDecrease);
constexpr std::size_t kLoadProjectionHotkeyIndex =
    input::hotkeyIndex(input::HotkeyId::LoadProjection);
constexpr std::size_t kCloseProjectionHotkeyIndex =
    input::hotkeyIndex(input::HotkeyId::CloseProjection);
constexpr float kActionHintVerticalScreenRatio = 0.80f;
auto &logger() { return LHolo::getInstance().getSelf().getLogger(); }

bool hudContextAvailable() {
  auto client = ll::service::getClientInstance();
  return client && client->getLocalPlayer() &&
         !projection::isDimensionSuspended();
}

auto &uiState() { return detail::StructureUiState::getInstance(); }

std::filesystem::path settingsPath() {
  return LHolo::getInstance().getSelf().getConfigDir() / "config.json";
}

void resetWorldSession();

unsigned int currentHotkeyModifiers() {
  return uiState().currentHotkeyModifiers();
}

} // namespace

bool handleMouseWheelDelta(int wheelDelta) {
  if (wheelDelta == 0)
    return false;
  if (isGuiVisible() || isInputTransitionBlocked())
    return false;

  auto const structureOffsetHotkey =
      uiState().inputHotkey(kStructureOffsetHotkeyIndex);
  auto const currentModifiers = uiState().currentHotkeyModifiers();
  bool hasStructureOffsetModifier = false;
  switch (structureOffsetHotkey.key) {
  case VK_MENU:
  case VK_LMENU:
  case VK_RMENU:
    hasStructureOffsetModifier =
        (currentModifiers & lholo::ui::kHotkeyModifierAlt) != 0;
    break;
  case VK_CONTROL:
  case VK_LCONTROL:
  case VK_RCONTROL:
    hasStructureOffsetModifier =
        (currentModifiers & lholo::ui::kHotkeyModifierControl) != 0;
    break;
  case VK_SHIFT:
  case VK_LSHIFT:
  case VK_RSHIFT:
    hasStructureOffsetModifier =
        (currentModifiers & lholo::ui::kHotkeyModifierShift) != 0;
    break;
  default:
    hasStructureOffsetModifier =
        (currentModifiers & structureOffsetHotkey.modifiers) ==
        structureOffsetHotkey.modifiers;
    break;
  }
  if (!hasStructureOffsetModifier)
    return false;

  auto client = ll::service::getClientInstance();
  auto *player = client ? client->getLocalPlayer() : nullptr;
  if (!player)
    return false;

  Vec3 const view = player->getViewVector(1.0f);
  int offsetX{};
  int offsetY{};
  int offsetZ{};
  if (!computeRelativeOffsetFromView(view.x, view.y, view.z, wheelDelta,
                                     offsetX, offsetY, offsetZ))
    return false;

  uiState().queueOffsetDelta(offsetX, offsetY, offsetZ);
  return true;
}

void requestMaterialList() { detail::requestMaterialListRefresh(); }

void requestOpenGui() {
  auto const opening = uiState().toggleGuiVisible();
  if (opening) {
    uiState().setOpeningInputBlockFrames(3);
  } else {
    // Consume the release half of the key/click that closed the menu.
    // Without this, Minecraft receives an unmatched Esc or mouse-up after
    // the ImGui window has already disappeared.
    uiState().setBlockGameInputUntil(GetTickCount64() + 180);
  }
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
  if (virtualKey == VK_CONTROL || virtualKey == VK_LCONTROL ||
      virtualKey == VK_RCONTROL) {
    uiState().setControlHeld(true);
  } else if (virtualKey == VK_MENU || virtualKey == VK_LMENU ||
             virtualKey == VK_RMENU) {
    uiState().setAltHeld(true);
  } else if (virtualKey == VK_SHIFT || virtualKey == VK_LSHIFT ||
             virtualKey == VK_RSHIFT) {
    uiState().setShiftHeld(true);
  }

  auto const captureIndex = uiState().capturingHotkey();
  if (captureIndex) {
    // F11 belongs to Minecraft's fullscreen toggle. Never capture or
    // consume it as a mod shortcut, including while rebinding controls.
    if (virtualKey == VK_F11)
      return false;
    if (virtualKey == VK_ESCAPE) {
      uiState().stopHotkeyCapture();
    } else if (virtualKey == VK_DELETE || virtualKey == VK_BACK) {
      uiState().clearHotkey(*captureIndex);
      uiState().stopHotkeyCapture();
      uiState().requestSettingsSave();
    } else if (!modifierKey) {
      auto const modifiers = currentHotkeyModifiers();
      uiState().bindCapturedHotkey(*captureIndex, virtualKey, modifiers);
      uiState().setIgnoreHotkeyUntil(GetTickCount64() + 250);
      uiState().requestSettingsSave();
    }
    return true;
  }

  if (modifierKey)
    return false;

  auto const modifiers = currentHotkeyModifiers();
  auto const guiHotkey = uiState().inputHotkey(kGuiHotkeyIndex);
  if (guiHotkey.key != 0 && virtualKey == guiHotkey.key &&
      modifiers == guiHotkey.modifiers) {
    if (GetTickCount64() >= uiState().ignoreHotkeyUntil() &&
        uiState().tryPressHotkey(kGuiHotkeyIndex)) {
      requestOpenGui();
    }
    return true;
  }
  if (isGuiVisible())
    return false;

  for (std::size_t index = 0; index < input::kMoveHotkeyCount; ++index) {
    auto const hotkey = uiState().inputHotkey(index + input::kMoveHotkeyFirst);
    if (hotkey.key == virtualKey && hotkey.modifiers == modifiers) {
      if (GetTickCount64() >= uiState().ignoreHotkeyUntil() &&
          uiState().tryPressHotkey(index + input::kMoveHotkeyFirst)) {
        uiState().queueMove(index);
      }
      return true;
    }
  }

  auto const layerIncreaseHotkey =
      uiState().inputHotkey(kLayerIncreaseHotkeyIndex);
  if (layerIncreaseHotkey.key != 0 && virtualKey == layerIncreaseHotkey.key &&
      modifiers == layerIncreaseHotkey.modifiers) {
    if (!detail::StructureSession::getInstance().layerDisplayEnabled())
      return false;
    if (GetTickCount64() >= uiState().ignoreHotkeyUntil() &&
        uiState().tryPressHotkey(kLayerIncreaseHotkeyIndex)) {
      uiState().queueLayerDelta(1);
    }
    return true;
  }
  auto const layerDecreaseHotkey =
      uiState().inputHotkey(kLayerDecreaseHotkeyIndex);
  if (layerDecreaseHotkey.key != 0 && virtualKey == layerDecreaseHotkey.key &&
      modifiers == layerDecreaseHotkey.modifiers) {
    if (!detail::StructureSession::getInstance().layerDisplayEnabled())
      return false;
    if (GetTickCount64() >= uiState().ignoreHotkeyUntil() &&
        uiState().tryPressHotkey(kLayerDecreaseHotkeyIndex)) {
      uiState().queueLayerDelta(-1);
    }
    return true;
  }
  auto const loadProjectionHotkey =
      uiState().inputHotkey(kLoadProjectionHotkeyIndex);
  if (loadProjectionHotkey.key != 0 && virtualKey == loadProjectionHotkey.key &&
      modifiers == loadProjectionHotkey.modifiers) {
    if (GetTickCount64() >= uiState().ignoreHotkeyUntil() &&
        uiState().tryPressHotkey(kLoadProjectionHotkeyIndex)) {
      uiState().queueLoadProjection();
    }
    return true;
  }
  auto const closeProjectionHotkey =
      uiState().inputHotkey(kCloseProjectionHotkeyIndex);
  if (closeProjectionHotkey.key != 0 &&
      virtualKey == closeProjectionHotkey.key &&
      modifiers == closeProjectionHotkey.modifiers) {
    if (GetTickCount64() >= uiState().ignoreHotkeyUntil() &&
        uiState().tryPressHotkey(kCloseProjectionHotkeyIndex)) {
      uiState().queueCloseProjection();
    }
    return true;
  }
  return false;
}

bool handleGuiHotkeyKeyUp(unsigned int virtualKey) {
  if (virtualKey == VK_CONTROL || virtualKey == VK_LCONTROL ||
      virtualKey == VK_RCONTROL) {
    uiState().setControlHeld(false);
    return false;
  }
  if (virtualKey == VK_MENU || virtualKey == VK_LMENU ||
      virtualKey == VK_RMENU) {
    uiState().setAltHeld(false);
    return false;
  }
  if (virtualKey == VK_SHIFT || virtualKey == VK_LSHIFT ||
      virtualKey == VK_RSHIFT) {
    uiState().setShiftHeld(false);
    return false;
  }

  return uiState().releaseHotkeysForKey(virtualKey, GetTickCount64());
}

void resetHotkeyState() { uiState().resetHotkeyState(); }

void processPendingActions() {
  if (projection::consumeWorldExitRequest()) {
    place::resetWorldSession();
    capture::clear();
    resetWorldSession();
    showActionHint("已退出世界，投影已关闭",
                   kProjectionLifecycleHintDurationMs);
    return;
  }

  auto &session = detail::StructureSession::getInstance();
  auto const pending = uiState().consumePendingHotkeyActions();
  auto const layerActionEnabled =
      pending.layerDelta != 0 && session.transform().layerDisplayMode != 0;
  bool changed = pending.offsetX != 0 || pending.offsetY != 0 ||
                 pending.offsetZ != 0 || layerActionEnabled;

  if (pending.offsetX != 0 || pending.offsetY != 0 || pending.offsetZ != 0) {
    uiState().addOffsetPreview(pending.offsetX, pending.offsetY,
                               pending.offsetZ);
    uiState().setOffsetPreviewDeadline(GetTickCount64() + 1000);
  }

  if (uiState().offsetPreviewActive() &&
      GetTickCount64() >= uiState().offsetPreviewDeadline()) {
    auto const preview = uiState().consumeOffsetPreview();
    session.adjustOffsets(preview[0], preview[1], preview[2]);
    changed = true;
  }

  if (layerActionEnabled)
    session.adjustDisplayLayer(pending.layerDelta);

  if (pending.loadProjection) {
    restoreSavedProjection();
    showActionHint("加载投影");
  }
  if (pending.closeProjection) {
    clear();
    saveSettings();
    showActionHint("关闭投影");
  }

  changed = pending.settingsSave || changed;
  if (changed)
    saveSettings();
}

void resetDimensionSession() { uiState().clearMaterialHud(); }

bool hasHudInfo() {
  if (!hudContextAvailable())
    return false;
  if (!detail::StructureSession::getInstance().hasLoaded())
    return false;
  // The material HUD renders independently of the projection HUD, so the
  // overlay must draw when it is enabled even if the projection HUD is off.
  if (materialHudEnabled())
    return true;
  auto const hud = uiState().hud();
  if (!hud.enabled)
    return false;
  if (!hud.showFileName && !hud.showLayer && !hud.showOverallProgress &&
      !hud.showProgress && !hud.showWrongState && !hud.showWrongType &&
      !hud.showProjectedBlockName)
    return false;
  return true;
}

namespace {
// Render-thread only. renderHud records its rect + corner each frame so
// renderMaterialHud (drawn right after, same frame) can stack clear of it when
// they share a corner. `frame` guards against stale reads.
struct ProjectionHudLayout {
  int frame{-1};
  int position{1};
  float topY{0.0f};
  float bottomY{0.0f};
};
ProjectionHudLayout gProjectionHudLayout;
} // namespace

bool experimentalConsentGiven() { return uiState().experimentalConsentGiven(); }

void setExperimentalConsentGiven(bool given) {
  uiState().setExperimentalConsentGiven(given);
}

bool materialHudEnabled() { return uiState().materialHudEnabled(); }

void setMaterialHudEnabled(bool enabled) {
  uiState().setMaterialHudEnabled(enabled);
}

int materialHudPosition() { return uiState().materialHudPosition(); }

void setMaterialHudPosition(int position) {
  uiState().setMaterialHudPosition(position);
}

void showActionHint(std::string text, std::uint64_t durationMs) {
  uiState().setActionHint(std::move(text), GetTickCount64() + durationMs);
}

bool actionHintActive() {
  return GetTickCount64() < uiState().actionHintExpiry();
}

void renderActionHint() {
  auto const now = GetTickCount64();
  auto const hint = uiState().actionHint();
  auto const expiry = hint.expiry;
  if (now >= expiry)
    return;
  if (hint.text.empty())
    return;

  auto const remaining = expiry - now;
  float const alpha =
      remaining < 300 ? static_cast<float>(remaining) / 300.0f : 1.0f;
  auto const displaySize = ImGui::GetIO().DisplaySize;
  float const uiScale = std::clamp(
      std::min(displaySize.x / 1920.0f, displaySize.y / 1080.0f), 1.0f, 5.0f);
  auto const metrics = lholo::ui::calculateMetrics(displaySize, uiScale);
  lholo::ui::applyFluentTheme(metrics);

  // Centered horizontally, sitting just above the hotbar like JE's action bar.
  ImGui::SetNextWindowPos(
      ImVec2(displaySize.x * 0.5f,
             displaySize.y * kActionHintVerticalScreenRatio),
      ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        ImVec4(0.36f, 0.20f, 0.42f, 0.86f * alpha));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, alpha));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, metrics.rounding * 0.6f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(metrics.sectionPadding, metrics.gap));
  constexpr auto flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavInputs |
      ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoInputs;
  if (ImGui::Begin("##LHoloActionHint", nullptr, flags)) {
    ImGui::TextUnformatted(hint.text.c_str());
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
}

void renderHud() {
  if (isGuiVisible())
    return;
  if (!hudContextAvailable())
    return;
  auto const hud = uiState().hud();
  if (!hud.enabled)
    return;
  auto const showFileName = hud.showFileName;
  auto const showLayer = hud.showLayer;
  auto const showOverallProgress = hud.showOverallProgress;
  auto const showProgress = hud.showProgress;
  auto const showWrongState = hud.showWrongState;
  auto const showWrongType = hud.showWrongType;
  auto const showExtraBlocks = hud.showExtraBlocks;
  auto const showProjectedBlockName = hud.showProjectedBlockName;
  if (!showFileName && !showLayer && !showOverallProgress && !showProgress &&
      !showWrongState && !showWrongType && !showExtraBlocks &&
      !showProjectedBlockName)
    return;

  auto const sessionSnapshot =
      detail::StructureSession::getInstance().snapshot();
  if (!sessionSnapshot.loaded)
    return;
  auto const fileName =
      detail::pathToUtf8(sessionSnapshot.loaded->sourcePath.filename());
  auto const layerAxis = sessionSnapshot.transform.layerAxis;
  auto const maxLayer =
      layerAxis == 1 ? sessionSnapshot.maxLayerX : sessionSnapshot.maxLayerY;

  auto const displaySize = ImGui::GetIO().DisplaySize;
  auto uiScale = hud.uiScale;
  if (uiScale <= 0.0f) {
    uiScale = std::clamp(
        std::min(displaySize.x / 1920.0f, displaySize.y / 1080.0f), 1.0f, 5.0f);
  }
  auto const hudMetrics = lholo::ui::calculateMetrics(displaySize, uiScale);
  lholo::ui::applyFluentTheme(hudMetrics);
  auto const layerMode = sessionSnapshot.transform.layerDisplayMode;
  auto const currentLayer =
      std::clamp(sessionSnapshot.transform.displayLayer, 0, maxLayer);

  auto const hudPosition = std::clamp(hud.position, 0, 3);
  auto const right = hudPosition >= 2;
  auto const bottom = (hudPosition & 1) != 0;
  auto const margin = hudMetrics.outerPadding;
  ImGui::SetNextWindowPos(ImVec2(right ? displaySize.x - margin : margin,
                                 bottom ? displaySize.y - margin : margin),
                          ImGuiCond_Always,
                          ImVec2(right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f));
  ImGui::SetNextWindowBgAlpha(0.68f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, hudMetrics.rounding * 0.7f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(hudMetrics.sectionPadding, hudMetrics.gap));
  constexpr auto flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavInputs |
      ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoInputs;
  if (ImGui::Begin("##LHoloHud", nullptr, flags)) {
    if (showFileName)
      ImGui::Text("投影：%s", fileName.c_str());
    if (showLayer && layerMode == 0) {
      ImGui::TextUnformatted("显示范围：完整结构");
    } else if (showLayer && layerMode == 1) {
      ImGui::Text("当前层：%d / %d（%s 轴）", currentLayer, maxLayer,
                  layerAxis == 1 ? "X" : "Y");
    } else if (showLayer && layerMode == 2) {
      ImGui::Text("显示范围：第 0～%d 层（%s 轴）", currentLayer,
                  layerAxis == 1 ? "X" : "Y");
    } else if (showLayer) {
      ImGui::Text("显示范围：第 %d～%d 层（%s 轴）", currentLayer, maxLayer,
                  layerAxis == 1 ? "X" : "Y");
    }
    auto const showAnyProgress = showOverallProgress || showProgress ||
                                 showWrongState || showWrongType ||
                                 showExtraBlocks;
    projection::BuildProgress progress{};
    if (showAnyProgress)
      progress = projection::getBuildProgress();
    if (showOverallProgress) {
      ImGui::Text("总体进度：%llu / %llu",
                  static_cast<unsigned long long>(progress.placed),
                  static_cast<unsigned long long>(progress.total));
    }
    if (showProgress) {
      ImGui::Text("建造进度：%llu / %llu",
                  static_cast<unsigned long long>(progress.visiblePlaced),
                  static_cast<unsigned long long>(progress.visibleTotal));
    }
    if (showWrongState && progress.wrongState != 0) {
      ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.18f, 1.0f), "朝向错误：%llu",
                         static_cast<unsigned long long>(progress.wrongState));
    }
    if (showWrongType && progress.wrongType != 0) {
      ImGui::TextColored(ImVec4(1.0f, 0.28f, 0.24f, 1.0f), "放置错误：%llu",
                         static_cast<unsigned long long>(progress.wrongType));
    }
    if (showExtraBlocks && progress.extra != 0) {
      ImGui::TextColored(ImVec4(1.0f, 0.30f, 0.90f, 1.0f), "多余方块：%llu",
                         static_cast<unsigned long long>(progress.extra));
    }
    auto const aimedProjectedBlock = place::getAimedProjectedBlockName();
    if (showProjectedBlockName && !aimedProjectedBlock.empty()) {
      ImGui::Text("投影方块：%s", aimedProjectedBlock.c_str());
    }
    // Record our rect + corner so the material HUD (drawn right after) can
    // stack clear of us when it shares this corner, instead of overlapping.
    gProjectionHudLayout.frame = ImGui::GetFrameCount();
    gProjectionHudLayout.position = hudPosition;
    gProjectionHudLayout.topY = ImGui::GetWindowPos().y;
    gProjectionHudLayout.bottomY =
        ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

void renderMaterialHud() {
  if (isGuiVisible())
    return;
  if (!hudContextAvailable())
    return;
  if (!materialHudEnabled())
    return;
  auto const hud = uiState().hud();
  if (!detail::StructureSession::getInstance().hasLoaded())
    return;
  // Both vectors come from one locked copy so they stay index-aligned. The
  // tracker publishes only the current visible layer's missing materials;
  // this present thread performs no structure or inventory scans.
  auto const snapshot = uiState().materialHudSnapshot();
  auto const &materials = snapshot.requirements;
  auto const &available = snapshot.available;

  struct Row {
    std::string const *name;
    std::uint64_t missing;
    int stackSize;
  };
  std::vector<Row> missing;
  for (std::size_t index = 0; index < materials.size(); ++index) {
    auto const need = materials[index].count;
    auto const have = index < available.size() ? available[index] : 0;
    auto const miss = static_cast<std::uint64_t>(have) >= need
                          ? 0ULL
                          : need - static_cast<std::uint64_t>(have);
    if (miss > 0) {
      missing.push_back(
          {&materials[index].displayName, miss, materials[index].stackSize});
    }
  }
  std::sort(missing.begin(), missing.end(),
            [](Row const &a, Row const &b) { return a.missing > b.missing; });

  auto const displaySize = ImGui::GetIO().DisplaySize;
  float uiScale = hud.uiScale;
  if (uiScale <= 0.0f) {
    uiScale = std::clamp(
        std::min(displaySize.x / 1920.0f, displaySize.y / 1080.0f), 1.0f, 5.0f);
  }
  auto const metrics = lholo::ui::calculateMetrics(displaySize, uiScale);
  lholo::ui::applyFluentTheme(metrics);
  auto const margin = metrics.outerPadding;
  auto const position = std::clamp(materialHudPosition(), 0, 3);
  bool const right = position >= 2;
  bool const bottom = (position & 1) != 0;
  float anchorX = right ? displaySize.x - margin : margin;
  float anchorY = bottom ? displaySize.y - margin : margin;
  // When the projection HUD occupies the same corner this frame, stack clear
  // of it — above it for a bottom corner, below it for a top corner.
  if (gProjectionHudLayout.frame == ImGui::GetFrameCount() &&
      gProjectionHudLayout.position == position) {
    if (bottom) {
      anchorY = std::min(anchorY, gProjectionHudLayout.topY - metrics.gap);
    } else {
      anchorY = std::max(anchorY, gProjectionHudLayout.bottomY + metrics.gap);
    }
  }
  ImGui::SetNextWindowPos(ImVec2(anchorX, anchorY), ImGuiCond_Always,
                          ImVec2(right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f));
  ImGui::SetNextWindowBgAlpha(0.68f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, metrics.rounding * 0.7f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(metrics.sectionPadding, metrics.gap));
  constexpr auto flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavInputs |
      ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoInputs;
  if (ImGui::Begin("##LHoloMaterialHud", nullptr, flags)) {
    ImGui::TextUnformatted("缺失材料");
    ImGui::Separator();
    if (!snapshot.ready) {
      ImGui::TextDisabled("正在统计当前显示范围…");
    } else if (missing.empty()) {
      ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.40f, 1.0f),
                         "当前显示范围材料已备齐 ✓");
    } else {
      constexpr std::size_t kMaxRows = 14;
      for (std::size_t index = 0; index < missing.size() && index < kMaxRows;
           ++index) {
        auto const &row = missing[index];
        // JE Litematica style: name then the missing amount broken into
        // stacks, e.g. "白色玻璃  111 (1 x 64 + 47)".
        ImGui::TextColored(
            ImVec4(1.0f, 0.62f, 0.20f, 1.0f), "%s  %s", row.name->c_str(),
            lholo::ui::formatStackCount(row.missing, row.stackSize).c_str());
      }
      if (missing.size() > kMaxRows) {
        ImGui::TextDisabled("…还有 %zu 种材料", missing.size() - kMaxRows);
      }
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

void renderGui() { lholo::ui::renderStructureMenu(); }

void loadSettings() {
  auto const path = settingsPath();
  try {
    auto &session = detail::StructureSession::getInstance();
    lholo::settings::Settings settings;
    if (!lholo::settings::loadSettingsFile(path, settings)) {
      saveSettings();
      return;
    }
    session.setLastPath(settings.lastStructurePath);
    uiState().setUiScale(std::clamp(settings.uiScale, 0.0f, 5.0f));
    projection::setOpacity(settings.opacity);
    projection::setCorrectionFillOpacity(settings.correctionFillOpacity);
    projection::setCorrectionOutlineOpacity(settings.correctionOutlineOpacity);
    projection::setStructureBoundsEnabled(settings.structureBoundsEnabled);
    projection::setCorrectionSeeThrough(settings.correctionSeeThrough);
    projection::setMissingSeeThrough(settings.missingSeeThrough);
    setExperimentalConsentGiven(settings.experimentalConsent);
    setMaterialHudEnabled(settings.materialHudEnabled);
    setMaterialHudPosition(settings.materialHudPosition);
    // Transform and layer state are session-local. Only the explicit
    // "restore last projection" record below is persisted.
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
    // Assisted-placement modes are intentionally session-only. Ignore
    // legacy persisted values and always begin a new game session disabled.
    place::setEnabled(false);
    place::setManualMode(false);
    place::setRangeEnabled(false);
    place::setPlacementRadius(std::clamp(settings.placementRadius, 1, 4));
    place::setAutoPlacementBreakCooldownSeconds(
        std::clamp(settings.autoPlacementBreakCooldownSeconds, 0, 60));
    uiState().setHotkey(kGuiHotkeyIndex, std::clamp(settings.guiHotkey, 0, 255),
                        std::clamp(settings.guiHotkeyModifiers, 0, 7));
    uiState().setHotkey(
        kStructureOffsetHotkeyIndex,
        std::clamp(settings.structureOffsetHotkey, 0, 255),
        std::clamp(settings.structureOffsetHotkeyModifiers, 0, 7));
    uiState().setHotkey(
        kLayerIncreaseHotkeyIndex,
        std::clamp(settings.layerIncreaseHotkey, 0, 255),
        std::clamp(settings.layerIncreaseHotkeyModifiers, 0, 7));
    uiState().setHotkey(
        kLayerDecreaseHotkeyIndex,
        std::clamp(settings.layerDecreaseHotkey, 0, 255),
        std::clamp(settings.layerDecreaseHotkeyModifiers, 0, 7));
    for (std::size_t index = 0; index < input::kMoveHotkeyCount; ++index) {
      uiState().setHotkey(
          index + input::kMoveHotkeyFirst,
          std::clamp(settings.moveHotkeys[index], 0, 255),
          std::clamp(settings.moveHotkeyModifiers[index], 0, 7));
    }
    uiState().setHotkey(
        kLoadProjectionHotkeyIndex,
        std::clamp(settings.loadProjectionHotkey, 0, 255),
        std::clamp(settings.loadProjectionHotkeyModifiers, 0, 7));
    uiState().setHotkey(
        kCloseProjectionHotkeyIndex,
        std::clamp(settings.closeProjectionHotkey, 0, 255),
        std::clamp(settings.closeProjectionHotkeyModifiers, 0, 7));
    session.setSavedProjection(
        {settings.hasSavedProjection,
         settings.savedAnchorX,
         settings.savedAnchorY,
         settings.savedAnchorZ,
         {settings.savedRotation, std::clamp(settings.savedMirror, 0, 2),
          settings.savedOffsetX, settings.savedOffsetY, settings.savedOffsetZ,
          settings.savedLayerDisplayMode, settings.savedDisplayLayer,
          std::clamp(settings.savedLayerAxis, 0, 1)},
         settings.savedStructurePath});
    logger().info("Loaded projection settings from {}", path.string());
  } catch (std::exception const &exception) {
    logger().error("Could not load projection settings {}: {}", path.string(),
                   exception.what());
  }
}

void saveSettings() {
  auto const path = settingsPath();
  try {
    auto &session = detail::StructureSession::getInstance();
    // Only an active projection may update its restore snapshot. At
    // startup the session-local transform/layer values intentionally
    // reset to defaults; copying those values before the user restores
    // a structure would silently destroy the saved state.
    session.refreshSavedTransformIfActive();
    auto const sessionSnapshot = session.snapshot();
    auto const hud = uiState().hud();
    lholo::settings::Settings settings;
    settings.lastStructurePath = sessionSnapshot.lastPath;
    settings.uiScale = hud.uiScale;
    settings.opacity = projection::getOpacity();
    settings.correctionFillOpacity = projection::getCorrectionFillOpacity();
    settings.correctionOutlineOpacity =
        projection::getCorrectionOutlineOpacity();
    settings.structureBoundsEnabled = projection::getStructureBoundsEnabled();
    settings.correctionSeeThrough = projection::getCorrectionSeeThrough();
    settings.missingSeeThrough = projection::getMissingSeeThrough();
    settings.experimentalConsent = experimentalConsentGiven();
    settings.materialHudEnabled = materialHudEnabled();
    settings.materialHudPosition = materialHudPosition();
    settings.placementRadius = place::getPlacementRadius();
    settings.autoPlacementBreakCooldownSeconds =
        place::getAutoPlacementBreakCooldownSeconds();
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
    auto const guiHotkey = uiState().hotkey(kGuiHotkeyIndex);
    auto const structureOffsetHotkey =
        uiState().hotkey(kStructureOffsetHotkeyIndex);
    auto const layerIncreaseHotkey =
        uiState().hotkey(kLayerIncreaseHotkeyIndex);
    auto const layerDecreaseHotkey =
        uiState().hotkey(kLayerDecreaseHotkeyIndex);
    settings.guiHotkey = guiHotkey.key;
    settings.guiHotkeyModifiers = guiHotkey.modifiers;
    settings.structureOffsetHotkey = structureOffsetHotkey.key;
    settings.structureOffsetHotkeyModifiers = structureOffsetHotkey.modifiers;
    settings.layerIncreaseHotkey = layerIncreaseHotkey.key;
    settings.layerDecreaseHotkey = layerDecreaseHotkey.key;
    settings.layerIncreaseHotkeyModifiers = layerIncreaseHotkey.modifiers;
    settings.layerDecreaseHotkeyModifiers = layerDecreaseHotkey.modifiers;
    for (std::size_t index = 0; index < settings.moveHotkeys.size(); ++index) {
      auto const moveHotkey = uiState().hotkey(index + input::kMoveHotkeyFirst);
      settings.moveHotkeys[index] = moveHotkey.key;
      settings.moveHotkeyModifiers[index] = moveHotkey.modifiers;
    }
    auto const loadProjectionHotkey =
        uiState().hotkey(kLoadProjectionHotkeyIndex);
    auto const closeProjectionHotkey =
        uiState().hotkey(kCloseProjectionHotkeyIndex);
    settings.loadProjectionHotkey = loadProjectionHotkey.key;
    settings.loadProjectionHotkeyModifiers = loadProjectionHotkey.modifiers;
    settings.closeProjectionHotkey = closeProjectionHotkey.key;
    settings.closeProjectionHotkeyModifiers = closeProjectionHotkey.modifiers;
    settings.hasSavedProjection = sessionSnapshot.saved.available;
    settings.savedAnchorX = sessionSnapshot.saved.anchorX;
    settings.savedAnchorY = sessionSnapshot.saved.anchorY;
    settings.savedAnchorZ = sessionSnapshot.saved.anchorZ;
    settings.savedRotation = sessionSnapshot.saved.transform.rotation;
    settings.savedMirror = sessionSnapshot.saved.transform.mirror;
    settings.savedOffsetX = sessionSnapshot.saved.transform.offsetX;
    settings.savedOffsetY = sessionSnapshot.saved.transform.offsetY;
    settings.savedOffsetZ = sessionSnapshot.saved.transform.offsetZ;
    settings.savedLayerDisplayMode =
        sessionSnapshot.saved.transform.layerDisplayMode;
    settings.savedDisplayLayer = sessionSnapshot.saved.transform.displayLayer;
    settings.savedLayerAxis = sessionSnapshot.saved.transform.layerAxis;
    settings.savedStructurePath = sessionSnapshot.saved.structurePath;
    lholo::settings::saveSettingsFile(path, settings);
  } catch (std::exception const &exception) {
    logger().error("Could not save projection settings {}: {}", path.string(),
                   exception.what());
  }
}

std::shared_ptr<LoadedStructure const> getLoaded() {
  return detail::StructureSession::getInstance().loaded();
}

int getRotationQuarterTurns() {
  return detail::StructureSession::getInstance().transform().rotation;
}

int getMirrorMode() {
  return std::clamp(detail::StructureSession::getInstance().transform().mirror,
                    0, 2);
}

int getOffsetX() {
  return detail::StructureSession::getInstance().transform().offsetX;
}
int getOffsetY() {
  return detail::StructureSession::getInstance().transform().offsetY;
}
int getOffsetZ() {
  return detail::StructureSession::getInstance().transform().offsetZ;
}
int getLayerDisplayMode() {
  return detail::StructureSession::getInstance().transform().layerDisplayMode;
}
int getDisplayLayer() {
  return detail::StructureSession::getInstance().transform().displayLayer;
}
int getLayerAxis() {
  return detail::StructureSession::getInstance().transform().layerAxis;
}

void recordProjectionAnchor(int x, int y, int z) {
  detail::StructureSession::getInstance().recordProjectionAnchor(x, y, z);
  saveSettings();
}

void restoreSavedProjection() {
  auto &session = detail::StructureSession::getInstance();
  auto const saved = session.savedProjection();
  auto const &savedPath = saved.structurePath;
  std::string error;
  auto loaded =
      detail::loadStructureFile(detail::pathFromUtf8(savedPath), error);
  if (!loaded) {
    session.setStatus("恢复失败: " + error);
    logger().error("Could not restore structure {}: {}", savedPath, error);
    return;
  }
  session.setRotation(saved.transform.rotation);
  session.setMirror(std::clamp(saved.transform.mirror, 0, 2));
  session.setOffsetX(saved.transform.offsetX);
  session.setOffsetY(saved.transform.offsetY);
  session.setOffsetZ(saved.transform.offsetZ);
  session.setLayerDisplayMode(saved.transform.layerDisplayMode);
  session.setDisplayLayer(saved.transform.displayLayer);
  session.setLayerAxis(saved.transform.layerAxis);
  projection::requestNextStructureAnchor(saved.anchorX, saved.anchorY,
                                         saved.anchorZ);
  session.replaceLoaded(std::move(loaded), savedPath,
                        "已恢复上次投影记录，等待进入渲染");
  detail::invalidateMaterialList();
  logger().info("Restoring projection {} at ({}, {}, {})", savedPath,
                saved.anchorX, saved.anchorY, saved.anchorZ);
}

namespace {

void clearProjectionSession(std::string status) {
  // Withdraw the requested structure before waiting for the mesh worker.
  // Otherwise the render hook can observe the old loaded structure in the gap
  // after projection::disable() and immediately enable the projection again.
  detail::StructureSession::getInstance().clearLoaded(std::move(status));

  // The active projection and in-flight worker keep non-owning Block pointers
  // into the Java mapper registry. Stop them before releasing that registry.
  projection::disable();
  resetJavaBlockMappingCache();
}

void resetWorldSession() {
  clearProjectionSession("已退出世界");
  uiState().resetWorldSession();
}

} // namespace

void clear() {
  clearProjectionSession("已关闭投影");
  uiState().clearMaterials();
}

} // namespace lholo::structure
