// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "settings/SettingsStore.h"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace lholo::settings {

bool loadSettingsFile(std::filesystem::path const& path, Settings& out) {
    if (!std::filesystem::exists(path)) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("无法打开配置文件");
    auto const json = nlohmann::json::parse(input, nullptr, true, true);

    out.lastStructurePath = json.value("lastStructurePath", out.lastStructurePath);
    out.uiScale = json.value("uiScale", out.uiScale);
    out.opacity = json.value("opacity", out.opacity);
    out.correctionFillOpacity = json.value("correctionFillOpacity", out.correctionFillOpacity);
    out.correctionOutlineOpacity = json.value("correctionOutlineOpacity", out.correctionOutlineOpacity);
    out.structureBoundsEnabled = json.value("structureBoundsEnabled", out.structureBoundsEnabled);
    out.correctionSeeThrough = json.value("correctionSeeThrough", out.correctionSeeThrough);
    out.missingSeeThrough = json.value("missingSeeThrough", out.missingSeeThrough);
    out.projectionSeeThrough = json.value("projectionSeeThrough", out.projectionSeeThrough);
    out.experimentalConsent = json.value("experimentalConsent", out.experimentalConsent);
    out.materialHudEnabled = json.value("materialHudEnabled", out.materialHudEnabled);
    out.materialHudPosition = json.value("materialHudPosition", out.materialHudPosition);
    out.placementRadius = json.value("placementRadius", out.placementRadius);
    out.autoPlacementBreakCooldownSeconds = json.value(
        "autoPlacementBreakCooldownSeconds",
        out.autoPlacementBreakCooldownSeconds
    );
    out.hudEnabled = json.value("hudEnabled", out.hudEnabled);
    out.hudShowFileName = json.value("hudShowFileName", out.hudShowFileName);
    out.hudShowLayer = json.value("hudShowLayer", out.hudShowLayer);
    out.hudShowOverallProgress = json.value("hudShowOverallProgress", out.hudShowOverallProgress);
    out.hudShowProgress = json.value("hudShowProgress", out.hudShowProgress);
    out.hudShowWrongState = json.value("hudShowWrongState", out.hudShowWrongState);
    out.hudShowWrongType = json.value("hudShowWrongType", out.hudShowWrongType);
    out.hudShowProjectedBlockName = json.value(
        "hudShowProjectedBlockName",
        json.value("hudShowBlockEntity", out.hudShowProjectedBlockName)
    );
    out.hudPosition = json.value("hudPosition", out.hudPosition);
    out.guiHotkey = json.value("guiHotkey", out.guiHotkey);
    out.guiHotkeyModifiers = json.value("guiHotkeyModifiers", out.guiHotkeyModifiers);
    out.layerIncreaseHotkey = json.value("layerIncreaseHotkey", out.layerIncreaseHotkey);
    out.layerDecreaseHotkey = json.value("layerDecreaseHotkey", out.layerDecreaseHotkey);
    out.layerIncreaseHotkeyModifiers
        = json.value("layerIncreaseHotkeyModifiers", out.layerIncreaseHotkeyModifiers);
    out.layerDecreaseHotkeyModifiers
        = json.value("layerDecreaseHotkeyModifiers", out.layerDecreaseHotkeyModifiers);
    out.toggleManualHotkey = json.value("toggleManualHotkey", out.toggleManualHotkey);
    out.toggleManualHotkeyModifiers
        = json.value("toggleManualHotkeyModifiers", out.toggleManualHotkeyModifiers);
    out.toggleEasyHotkey = json.value("toggleEasyHotkey", out.toggleEasyHotkey);
    out.toggleEasyHotkeyModifiers
        = json.value("toggleEasyHotkeyModifiers", out.toggleEasyHotkeyModifiers);
    out.loadProjectionHotkey = json.value("loadProjectionHotkey", out.loadProjectionHotkey);
    out.loadProjectionHotkeyModifiers
        = json.value("loadProjectionHotkeyModifiers", out.loadProjectionHotkeyModifiers);
    out.closeProjectionHotkey = json.value("closeProjectionHotkey", out.closeProjectionHotkey);
    out.closeProjectionHotkeyModifiers
        = json.value("closeProjectionHotkeyModifiers", out.closeProjectionHotkeyModifiers);

    static char const* moveKeyNames[]{
        "moveXMinusHotkey",
        "moveXPlusHotkey",
        "moveZMinusHotkey",
        "moveZPlusHotkey",
        "moveYPlusHotkey",
        "moveYMinusHotkey"
    };
    static char const* moveModifierNames[]{
        "moveXMinusHotkeyModifiers",
        "moveXPlusHotkeyModifiers",
        "moveZMinusHotkeyModifiers",
        "moveZPlusHotkeyModifiers",
        "moveYPlusHotkeyModifiers",
        "moveYMinusHotkeyModifiers"
    };
    for (std::size_t index = 0; index < out.moveHotkeys.size(); ++index) {
        out.moveHotkeys[index] = json.value(moveKeyNames[index], out.moveHotkeys[index]);
        out.moveHotkeyModifiers[index]
            = json.value(moveModifierNames[index], out.moveHotkeyModifiers[index]);
    }

    out.hasSavedProjection = json.value("hasSavedProjection", out.hasSavedProjection);
    out.savedAnchorX = json.value("savedAnchorX", out.savedAnchorX);
    out.savedAnchorY = json.value("savedAnchorY", out.savedAnchorY);
    out.savedAnchorZ = json.value("savedAnchorZ", out.savedAnchorZ);
    out.savedRotation = json.value("savedRotation", out.savedRotation);
    out.savedMirror = json.value("savedMirror", out.savedMirror);
    out.savedOffsetX = json.value("savedOffsetX", out.savedOffsetX);
    out.savedOffsetY = json.value("savedOffsetY", out.savedOffsetY);
    out.savedOffsetZ = json.value("savedOffsetZ", out.savedOffsetZ);
    out.savedLayerDisplayMode = json.value("savedLayerDisplayMode", out.savedLayerDisplayMode);
    out.savedDisplayLayer = json.value("savedDisplayLayer", out.savedDisplayLayer);
    out.savedLayerAxis = json.value("savedLayerAxis", out.savedLayerAxis);
    out.savedStructurePath = json.value("savedStructurePath", out.savedStructurePath);
    return true;
}

void saveSettingsFile(std::filesystem::path const& path, Settings const& settings) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) throw std::runtime_error(error.message());

    nlohmann::ordered_json const json{
        {"version", 10},
        {"lastStructurePath", settings.lastStructurePath},
        {"uiScale", settings.uiScale},
        {"opacity", settings.opacity},
        {"correctionFillOpacity", settings.correctionFillOpacity},
        {"correctionOutlineOpacity", settings.correctionOutlineOpacity},
        {"structureBoundsEnabled", settings.structureBoundsEnabled},
        {"correctionSeeThrough", settings.correctionSeeThrough},
        {"missingSeeThrough", settings.missingSeeThrough},
        {"projectionSeeThrough", settings.projectionSeeThrough},
        {"experimentalConsent", settings.experimentalConsent},
        {"materialHudEnabled", settings.materialHudEnabled},
        {"materialHudPosition", settings.materialHudPosition},
        {"placementRadius", settings.placementRadius},
        {"autoPlacementBreakCooldownSeconds", settings.autoPlacementBreakCooldownSeconds},
        {"hudEnabled", settings.hudEnabled},
        {"hudShowFileName", settings.hudShowFileName},
        {"hudShowLayer", settings.hudShowLayer},
        {"hudShowOverallProgress", settings.hudShowOverallProgress},
        {"hudShowProgress", settings.hudShowProgress},
        {"hudShowWrongState", settings.hudShowWrongState},
        {"hudShowWrongType", settings.hudShowWrongType},
        {"hudShowProjectedBlockName", settings.hudShowProjectedBlockName},
        {"hudPosition", settings.hudPosition},
        {"guiHotkey", settings.guiHotkey},
        {"guiHotkeyModifiers", settings.guiHotkeyModifiers},
        {"layerIncreaseHotkey", settings.layerIncreaseHotkey},
        {"layerDecreaseHotkey", settings.layerDecreaseHotkey},
        {"layerIncreaseHotkeyModifiers", settings.layerIncreaseHotkeyModifiers},
        {"layerDecreaseHotkeyModifiers", settings.layerDecreaseHotkeyModifiers},
        {"toggleManualHotkey", settings.toggleManualHotkey},
        {"toggleManualHotkeyModifiers", settings.toggleManualHotkeyModifiers},
        {"toggleEasyHotkey", settings.toggleEasyHotkey},
        {"toggleEasyHotkeyModifiers", settings.toggleEasyHotkeyModifiers},
        {"loadProjectionHotkey", settings.loadProjectionHotkey},
        {"loadProjectionHotkeyModifiers", settings.loadProjectionHotkeyModifiers},
        {"closeProjectionHotkey", settings.closeProjectionHotkey},
        {"closeProjectionHotkeyModifiers", settings.closeProjectionHotkeyModifiers},
        {"moveXMinusHotkey", settings.moveHotkeys[0]},
        {"moveXPlusHotkey", settings.moveHotkeys[1]},
        {"moveZMinusHotkey", settings.moveHotkeys[2]},
        {"moveZPlusHotkey", settings.moveHotkeys[3]},
        {"moveYPlusHotkey", settings.moveHotkeys[4]},
        {"moveYMinusHotkey", settings.moveHotkeys[5]},
        {"moveXMinusHotkeyModifiers", settings.moveHotkeyModifiers[0]},
        {"moveXPlusHotkeyModifiers", settings.moveHotkeyModifiers[1]},
        {"moveZMinusHotkeyModifiers", settings.moveHotkeyModifiers[2]},
        {"moveZPlusHotkeyModifiers", settings.moveHotkeyModifiers[3]},
        {"moveYPlusHotkeyModifiers", settings.moveHotkeyModifiers[4]},
        {"moveYMinusHotkeyModifiers", settings.moveHotkeyModifiers[5]},
        {"hasSavedProjection", settings.hasSavedProjection},
        {"savedAnchorX", settings.savedAnchorX},
        {"savedAnchorY", settings.savedAnchorY},
        {"savedAnchorZ", settings.savedAnchorZ},
        {"savedStructurePath", settings.savedStructurePath},
        {"savedRotation", settings.savedRotation},
        {"savedMirror", settings.savedMirror},
        {"savedOffsetX", settings.savedOffsetX},
        {"savedOffsetY", settings.savedOffsetY},
        {"savedOffsetZ", settings.savedOffsetZ},
        {"savedLayerDisplayMode", settings.savedLayerDisplayMode},
        {"savedDisplayLayer", settings.savedDisplayLayer},
        {"savedLayerAxis", settings.savedLayerAxis}
    };
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("无法写入配置文件");
    output << json.dump(2);
}

} // namespace lholo::settings
