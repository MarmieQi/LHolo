// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "ui/MenuController.h"

#include "place/PlaceHelper.h"
#include "plugin/LHolo.h"
#include "projection/Projection.h"
#include "structure/MaterialTracker.h"
#include "structure/StructureLoader.h"
#include "structure/StructurePaths.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"
#include "structure/capture/StructureCapture.h"
#include "structure/formats/StructureFormatLoaders.h"
#include "ui/FileDialog.h"
#include "ui/FluentTheme.h"
#include "ui/HotkeyFormat.h"
#include "ui/LHoloMenu.h"


#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>


#include <Windows.h>

#include "imgui.h"
#include "ll/api/mod/NativeMod.h"

namespace lholo::ui {
namespace {

auto &logger() { return LHolo::getInstance().getSelf().getLogger(); }

auto &uiState() { return structure::detail::StructureUiState::getInstance(); }

std::array<char, 2048> gPathBuffer{};
bool gPathInitialized{};
MenuPage gActivePage{MenuPage::Projection};

struct HotkeyDefinition {
  HotkeyId id;
  char const *label;
};
constexpr std::array<HotkeyDefinition, input::kHotkeyCount> kHotkeyDefinitions{
    {{HotkeyId::Gui, "打开投影菜单"},
     {HotkeyId::StructureOffset, "快捷结构偏移"},
     {HotkeyId::MoveXMinus, "结构偏移 X -1"},
     {HotkeyId::MoveXPlus, "结构偏移 X +1"},
     {HotkeyId::MoveZMinus, "结构偏移 Z -1"},
     {HotkeyId::MoveZPlus, "结构偏移 Z +1"},
     {HotkeyId::MoveYPlus, "结构偏移 Y +1"},
     {HotkeyId::MoveYMinus, "结构偏移 Y -1"},
     {HotkeyId::LayerIncrease, "上一层"},
     {HotkeyId::LayerDecrease, "下一层"},
     {HotkeyId::LoadProjection, "加载投影"},
     {HotkeyId::CloseProjection, "关闭投影"}}};

} // namespace

MenuModel buildStructureMenuModel(float effectiveUiScale) {
  MenuModel model;
  auto &session = structure::detail::StructureSession::getInstance();
  auto const sessionSnapshot = session.snapshot();
  auto const hud = uiState().hud();
  model.page = gActivePage;
  model.pathBuffer = gPathBuffer.data();
  model.pathBufferSize = gPathBuffer.size();
  model.blockOpeningInput = uiState().openingInputBlocked();
  model.uiScale = effectiveUiScale;
  auto const captureSnapshot = structure::capture::getSnapshot();
  model.capture.mode = static_cast<int>(captureSnapshot.draft.mode);
  model.captureRevision = captureSnapshot.revision;
  model.capture.includeEntities = captureSnapshot.draft.includeEntities;
  model.captureWorldAvailable = captureSnapshot.worldAvailable;
  model.captureStatus = captureSnapshot.status;
  if (captureSnapshot.draft.first) {
    auto const &point = *captureSnapshot.draft.first;
    model.capture.first = {true, point.x, point.y, point.z};
  }
  if (captureSnapshot.draft.second) {
    auto const &point = *captureSnapshot.draft.second;
    model.capture.second = {true, point.x, point.y, point.z};
  }
  model.layerAxis = std::clamp(sessionSnapshot.transform.layerAxis, 0, 1);
  model.status = sessionSnapshot.status;
  model.hasLoadedStructure = static_cast<bool>(sessionSnapshot.loaded);
  model.hasSavedProjection = sessionSnapshot.saved.available;
  model.savedAnchorX = sessionSnapshot.saved.anchorX;
  model.savedAnchorY = sessionSnapshot.saved.anchorY;
  model.savedAnchorZ = sessionSnapshot.saved.anchorZ;
  model.maxLayerY = sessionSnapshot.maxLayerY;
  model.maxLayerX = sessionSnapshot.maxLayerX;
  model.structureBoundsEnabled = projection::getStructureBoundsEnabled();
  model.correctionSeeThrough = projection::getCorrectionSeeThrough();
  model.missingSeeThrough = projection::getMissingSeeThrough();
  model.easyPlaceEnabled = place::isEnabled();
  model.manualPlace = place::isManualMode();
  model.rangeEnabled = place::isRangeEnabled();
  model.experimentalConsent = structure::experimentalConsentGiven();
  model.materialHudEnabled = structure::materialHudEnabled();
  model.materialHudPosition =
      std::clamp(structure::materialHudPosition(), 0, 3);
  model.placementRadius = place::getPlacementRadius();
  model.autoPlacementBreakCooldownSeconds =
      place::getAutoPlacementBreakCooldownSeconds();
  model.offsetX = sessionSnapshot.transform.offsetX;
  model.offsetY = sessionSnapshot.transform.offsetY;
  model.offsetZ = sessionSnapshot.transform.offsetZ;
  model.rotation = std::clamp(sessionSnapshot.transform.rotation, 0, 3);
  model.mirror = std::clamp(sessionSnapshot.transform.mirror, 0, 2);
  model.opacity = projection::getOpacity();
  model.correctionFillOpacity = projection::getCorrectionFillOpacity();
  model.correctionOutlineOpacity = projection::getCorrectionOutlineOpacity();
  model.layerDisplayMode =
      std::clamp(sessionSnapshot.transform.layerDisplayMode, 0, 3);
  model.displayLayer =
      std::clamp(sessionSnapshot.transform.displayLayer, 0,
                 model.layerAxis == 1 ? model.maxLayerX : model.maxLayerY);
  model.hudEnabled = hud.enabled;
  model.hudPosition = std::clamp(hud.position, 0, 3);
  model.hudShowFileName = hud.showFileName;
  model.hudShowLayer = hud.showLayer;
  model.hudShowOverallProgress = hud.showOverallProgress;
  model.hudShowProgress = hud.showProgress;
  model.hudShowWrongState = hud.showWrongState;
  model.hudShowWrongType = hud.showWrongType;
  model.hudShowExtraBlocks = hud.showExtraBlocks;
  model.hudShowProjectedBlockName = hud.showProjectedBlockName;
  std::size_t rowIndex = 0;
  for (auto const &definition : kHotkeyDefinitions) {
    auto const binding =
        uiState().hotkey(static_cast<std::size_t>(definition.id));
    auto &row = model.hotkeys[rowIndex++];
    row.id = definition.id;
    row.label = definition.label;
    row.display = hotkeyChordName(binding.modifiers, binding.key);
    row.capturing = binding.capturing;
  }
  auto const materials = uiState().materialRequirements();
  model.materials.reserve(materials.size());
  for (auto const &material : materials) {
    model.materials.push_back({material.displayName, material.typeName,
                               material.count, material.stackSize});
  }
  return model;
}

void applyStructureMenuModel(MenuModel const &model, float effectiveUiScale) {
  bool changed = false;
  auto &session = structure::detail::StructureSession::getInstance();
  if (std::abs(model.uiScale - effectiveUiScale) > 0.001f) {
    auto const scale = std::clamp(model.uiScale, 1.0f, 5.0f);
    if (std::abs(uiState().hud().uiScale - scale) > 0.001f)
      changed = uiState().setUiScale(scale) || changed;
  }
  if (projection::getStructureBoundsEnabled() != model.structureBoundsEnabled) {
    projection::setStructureBoundsEnabled(model.structureBoundsEnabled);
    changed = true;
  }
  if (projection::getCorrectionSeeThrough() != model.correctionSeeThrough) {
    projection::setCorrectionSeeThrough(model.correctionSeeThrough);
    changed = true;
  }
  if (projection::getMissingSeeThrough() != model.missingSeeThrough) {
    projection::setMissingSeeThrough(model.missingSeeThrough);
    changed = true;
  }
  // Assisted-placement modes are session-only safety controls. Applying a
  // mode must not dirty or rewrite the persistent settings file.
  if (place::isEnabled() != model.easyPlaceEnabled)
    place::setEnabled(model.easyPlaceEnabled);
  if (place::isManualMode() != model.manualPlace)
    place::setManualMode(model.manualPlace);
  if (place::isRangeEnabled() != model.rangeEnabled)
    place::setRangeEnabled(model.rangeEnabled);
  auto const radius = std::clamp(model.placementRadius, 1, 4);
  if (place::getPlacementRadius() != radius) {
    place::setPlacementRadius(radius);
    changed = true;
  }
  auto const breakCooldown =
      std::clamp(model.autoPlacementBreakCooldownSeconds, 0, 60);
  if (place::getAutoPlacementBreakCooldownSeconds() != breakCooldown) {
    place::setAutoPlacementBreakCooldownSeconds(breakCooldown);
    changed = true;
  }
  changed = session.setOffsetX(model.offsetX) || changed;
  changed = session.setOffsetY(model.offsetY) || changed;
  changed = session.setOffsetZ(model.offsetZ) || changed;
  changed = session.setRotation(std::clamp(model.rotation, 0, 3)) || changed;
  changed = session.setMirror(std::clamp(model.mirror, 0, 2)) || changed;

  auto const opacity = std::clamp(model.opacity, 0.0f, 1.0f);
  if (std::abs(projection::getOpacity() - opacity) > 0.0001f) {
    projection::setOpacity(opacity);
    changed = true;
  }
  auto const fill = std::clamp(model.correctionFillOpacity, 0.0f, 1.0f);
  if (std::abs(projection::getCorrectionFillOpacity() - fill) > 0.0001f) {
    projection::setCorrectionFillOpacity(fill);
    changed = true;
  }
  auto const outline = std::clamp(model.correctionOutlineOpacity, 0.0f, 1.0f);
  if (std::abs(projection::getCorrectionOutlineOpacity() - outline) > 0.0001f) {
    projection::setCorrectionOutlineOpacity(outline);
    changed = true;
  }
  auto const layerAxis = std::clamp(model.layerAxis, 0, 1);
  changed = session.setLayerAxis(layerAxis) || changed;
  changed =
      session.setLayerDisplayMode(std::clamp(model.layerDisplayMode, 0, 3)) ||
      changed;
  auto const sessionSnapshot = session.snapshot();
  auto const displayMax =
      layerAxis == 1 ? sessionSnapshot.maxLayerX : sessionSnapshot.maxLayerY;
  changed =
      session.setDisplayLayer(std::clamp(model.displayLayer, 0, displayMax)) ||
      changed;
  auto hud = uiState().hud();
  hud.enabled = model.hudEnabled;
  hud.position = std::clamp(model.hudPosition, 0, 3);
  hud.showFileName = model.hudShowFileName;
  hud.showLayer = model.hudShowLayer;
  hud.showOverallProgress = model.hudShowOverallProgress;
  hud.showProgress = model.hudShowProgress;
  hud.showWrongState = model.hudShowWrongState;
  hud.showWrongType = model.hudShowWrongType;
  hud.showExtraBlocks = model.hudShowExtraBlocks;
  hud.showProjectedBlockName = model.hudShowProjectedBlockName;
  changed = uiState().applyHud(hud) || changed;
  if (structure::materialHudEnabled() != model.materialHudEnabled) {
    structure::setMaterialHudEnabled(model.materialHudEnabled);
    changed = true;
  }
  auto const materialPosition = std::clamp(model.materialHudPosition, 0, 3);
  if (structure::materialHudPosition() != materialPosition) {
    structure::setMaterialHudPosition(materialPosition);
    changed = true;
  }
  structure::capture::Draft captureDraft;
  captureDraft.mode = static_cast<structure::capture::CaptureMode>(
      std::clamp(model.capture.mode, 0, 1));
  captureDraft.includeEntities = model.capture.includeEntities;
  if (model.capture.first.set) {
    captureDraft.first = structure::capture::Point{
        model.capture.first.x, model.capture.first.y, model.capture.first.z};
  }
  if (model.capture.second.set) {
    captureDraft.second = structure::capture::Point{
        model.capture.second.x, model.capture.second.y, model.capture.second.z};
  }
  structure::capture::updateDraft(captureDraft);
  if (changed)
    structure::saveSettings();
}

MenuActions buildStructureMenuActions(bool &refreshModel) {
  MenuActions actions;
  actions.browseStructure =
      [](std::string_view current) -> std::optional<std::string> {
    auto const selected =
        openStructureFile(structure::detail::pathFromUtf8(current));
    return selected ? std::optional<std::string>{structure::detail::pathToUtf8(
                          *selected)}
                    : std::nullopt;
  };
  actions.loadStructure = [&refreshModel](std::string_view pathValue) {
    auto &session = structure::detail::StructureSession::getInstance();
    auto const pathText = std::string{pathValue};
    if (pathText.empty()) {
      session.setStatus("请选择或输入 .mcstructure / .litematic 文件路径");
      return;
    }
    std::string error;
    auto loaded = structure::detail::loadStructureFile(
        structure::detail::pathFromUtf8(pathText), error);
    if (!loaded) {
      session.setStatus("加载失败: " + error);
      logger().error("Could not load structure {}: {}", pathText, error);
      return;
    }
    auto const status = structure::detail::makeStructureStatus(*loaded);
    // A normal file load is a new user intent. Do not let an unconsumed
    // restore request from an earlier failed/pending activation move it.
    projection::cancelNextStructureAnchorRequest();
    session.replaceLoaded(std::move(loaded), pathText, status);
    structure::detail::invalidateMaterialList();
    structure::saveSettings();
    refreshModel = true;
    logger().info("{}", status);
  };
  actions.restoreProjection = [&refreshModel] {
    structure::restoreSavedProjection();
    auto const savedPath = structure::detail::StructureSession::getInstance()
                               .savedProjection()
                               .structurePath;
    std::snprintf(gPathBuffer.data(), gPathBuffer.size(), "%s",
                  savedPath.c_str());
    refreshModel = true;
  };
  actions.closeProjection = [&refreshModel] {
    structure::clear();
    // clear() freezes the active transform before releasing the loaded
    // structure; persist that restore snapshot while closing from the UI.
    structure::saveSettings();
    refreshModel = true;
  };
  actions.requestMaterials = [] { structure::requestMaterialList(); };
  actions.beginHotkeyCapture = [](HotkeyId id) {
    uiState().beginHotkeyCapture(static_cast<std::size_t>(id));
  };
  actions.clearHotkey = [](HotkeyId id) {
    uiState().clearHotkey(static_cast<std::size_t>(id));
    structure::saveSettings();
  };
  actions.resetHotkeys = [] {
    uiState().resetHotkeys();
    structure::resetHotkeyState();
    structure::saveSettings();
  };
  actions.resetCorrectionStyle = [] {
    projection::setCorrectionFillOpacity(0.15f);
    projection::setCorrectionOutlineOpacity(1.0f);
    structure::saveSettings();
  };
  actions.giveExperimentalConsent = [] {
    structure::setExperimentalConsentGiven(true);
    structure::saveSettings();
  };
  actions.usePlayerCapturePosition = [&refreshModel](CapturePointId point) {
    structure::capture::setPointFromPlayer(
        point == CapturePointId::First ? structure::capture::PointSlot::First
                                       : structure::capture::PointSlot::Second);
    refreshModel = true;
  };
  actions.clearCapture = [&refreshModel] {
    structure::capture::clear();
    refreshModel = true;
  };
  actions.exportCapture = [&refreshModel](CaptureDraftModel const &model) {
    auto const output = saveMcstructureFile();
    if (!output)
      return;
    structure::capture::Draft draft;
    draft.mode = static_cast<structure::capture::CaptureMode>(
        std::clamp(model.mode, 0, 1));
    draft.includeEntities = model.includeEntities;
    if (model.first.set) {
      draft.first = structure::capture::Point{model.first.x, model.first.y,
                                              model.first.z};
    }
    if (model.second.set) {
      draft.second = structure::capture::Point{model.second.x, model.second.y,
                                               model.second.z};
    }
    structure::capture::exportStructure(draft, *output);
    refreshModel = true;
  };
  return actions;
}

void renderStructureMenu() {
  if (!structure::isGuiVisible())
    return;
  auto const displaySize = ImGui::GetIO().DisplaySize;
  auto const configuredScale = uiState().hud().uiScale;
  auto const effectiveScale =
      configuredScale > 0.0f ? std::clamp(configuredScale, 1.0f, 5.0f)
                             : std::clamp(std::min(displaySize.x / 1920.0f,
                                                   displaySize.y / 1080.0f),
                                          1.0f, 5.0f);
  if (!gPathInitialized) {
    auto const lastPath =
        structure::detail::StructureSession::getInstance().lastPath();
    std::snprintf(gPathBuffer.data(), gPathBuffer.size(), "%s",
                  lastPath.c_str());
    gPathInitialized = true;
  }
  auto const metrics = calculateMetrics(displaySize, effectiveScale);
  applyFluentTheme(metrics);
  auto model = buildStructureMenuModel(effectiveScale);
  bool refreshModel = false;
  auto const actions = buildStructureMenuActions(refreshModel);
  renderMenu(model, actions, metrics);
  gActivePage = model.page;
  if (!refreshModel)
    applyStructureMenuModel(model, effectiveScale);
  uiState().consumeOpeningInputBlockFrame();
  if (model.closeRequested) {
    uiState().setGuiVisible(false);
    uiState().setBlockGameInputUntil(GetTickCount64() + 180);
  }
}

} // namespace lholo::ui
