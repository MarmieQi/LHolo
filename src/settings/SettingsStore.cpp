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
    if (auto const language = json.find("language");
        language != json.end() && language->is_string()) {
        out.language = language->get<std::string>();
    } else {
        // Language preferences intentionally have no old integer migration:
        // missing or malformed values use the default locale.
        out.language = "zh_CN";
    }
    out.uiScale = json.value("uiScale", out.uiScale);
    out.opacity = json.value("opacity", out.opacity);
    out.correctionFillOpacity = json.value("correctionFillOpacity", out.correctionFillOpacity);
    out.correctionOutlineOpacity = json.value("correctionOutlineOpacity", out.correctionOutlineOpacity);
    out.structureBoundsEnabled = json.value("structureBoundsEnabled", out.structureBoundsEnabled);
    out.correctionSeeThrough = json.value("correctionSeeThrough", out.correctionSeeThrough);
    out.missingSeeThrough = json.value("missingSeeThrough", out.missingSeeThrough);
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
    out.hudShowExtraBlocks = json.value("hudShowExtraBlocks", out.hudShowExtraBlocks);
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
    out.loadProjectionHotkey = json.value("loadProjectionHotkey", out.loadProjectionHotkey);
    out.loadProjectionHotkeyModifiers
        = json.value("loadProjectionHotkeyModifiers", out.loadProjectionHotkeyModifiers);
    out.closeProjectionHotkey = json.value("closeProjectionHotkey", out.closeProjectionHotkey);
    out.closeProjectionHotkeyModifiers
        = json.value("closeProjectionHotkeyModifiers", out.closeProjectionHotkeyModifiers);
    out.altWheelOffsetEnabled
        = json.value("altWheelOffsetEnabled", out.altWheelOffsetEnabled);

    // Slot order: left, right, forward, backward, up, down. These are the
    // directions the slots produce now; configs written while the slots were
    // world-axis offsets carry the old names and are still read as a fallback,
    // so upgrading never resets a binding.
    static char const* moveKeyNames[]{
        "moveLeftHotkey",
        "moveRightHotkey",
        "moveForwardHotkey",
        "moveBackwardHotkey",
        "moveUpHotkey",
        "moveDownHotkey"
    };
    static char const* moveModifierNames[]{
        "moveLeftHotkeyModifiers",
        "moveRightHotkeyModifiers",
        "moveForwardHotkeyModifiers",
        "moveBackwardHotkeyModifiers",
        "moveUpHotkeyModifiers",
        "moveDownHotkeyModifiers"
    };
    static char const* legacyMoveKeyNames[]{
        "moveXMinusHotkey",
        "moveXPlusHotkey",
        "moveZMinusHotkey",
        "moveZPlusHotkey",
        "moveYPlusHotkey",
        "moveYMinusHotkey"
    };
    static char const* legacyMoveModifierNames[]{
        "moveXMinusHotkeyModifiers",
        "moveXPlusHotkeyModifiers",
        "moveZMinusHotkeyModifiers",
        "moveZPlusHotkeyModifiers",
        "moveYPlusHotkeyModifiers",
        "moveYMinusHotkeyModifiers"
    };
    for (std::size_t index = 0; index < out.moveHotkeys.size(); ++index) {
        out.moveHotkeys[index] = json.value(
            moveKeyNames[index],
            json.value(legacyMoveKeyNames[index], out.moveHotkeys[index])
        );
        out.moveHotkeyModifiers[index] = json.value(
            moveModifierNames[index],
            json.value(legacyMoveModifierNames[index], out.moveHotkeyModifiers[index])
        );
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
        {"version", 12},
        {"lastStructurePath", settings.lastStructurePath},
        {"language", settings.language},
        {"uiScale", settings.uiScale},
        {"opacity", settings.opacity},
        {"correctionFillOpacity", settings.correctionFillOpacity},
        {"correctionOutlineOpacity", settings.correctionOutlineOpacity},
        {"structureBoundsEnabled", settings.structureBoundsEnabled},
        {"correctionSeeThrough", settings.correctionSeeThrough},
        {"missingSeeThrough", settings.missingSeeThrough},
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
        {"hudShowExtraBlocks", settings.hudShowExtraBlocks},
        {"hudShowProjectedBlockName", settings.hudShowProjectedBlockName},
        {"hudPosition", settings.hudPosition},
        {"guiHotkey", settings.guiHotkey},
        {"guiHotkeyModifiers", settings.guiHotkeyModifiers},
        {"layerIncreaseHotkey", settings.layerIncreaseHotkey},
        {"layerDecreaseHotkey", settings.layerDecreaseHotkey},
        {"layerIncreaseHotkeyModifiers", settings.layerIncreaseHotkeyModifiers},
        {"layerDecreaseHotkeyModifiers", settings.layerDecreaseHotkeyModifiers},
        {"loadProjectionHotkey", settings.loadProjectionHotkey},
        {"loadProjectionHotkeyModifiers", settings.loadProjectionHotkeyModifiers},
        {"closeProjectionHotkey", settings.closeProjectionHotkey},
        {"closeProjectionHotkeyModifiers", settings.closeProjectionHotkeyModifiers},
        {"altWheelOffsetEnabled", settings.altWheelOffsetEnabled},
        {"moveLeftHotkey", settings.moveHotkeys[0]},
        {"moveRightHotkey", settings.moveHotkeys[1]},
        {"moveForwardHotkey", settings.moveHotkeys[2]},
        {"moveBackwardHotkey", settings.moveHotkeys[3]},
        {"moveUpHotkey", settings.moveHotkeys[4]},
        {"moveDownHotkey", settings.moveHotkeys[5]},
        {"moveLeftHotkeyModifiers", settings.moveHotkeyModifiers[0]},
        {"moveRightHotkeyModifiers", settings.moveHotkeyModifiers[1]},
        {"moveForwardHotkeyModifiers", settings.moveHotkeyModifiers[2]},
        {"moveBackwardHotkeyModifiers", settings.moveHotkeyModifiers[3]},
        {"moveUpHotkeyModifiers", settings.moveHotkeyModifiers[4]},
        {"moveDownHotkeyModifiers", settings.moveHotkeyModifiers[5]},
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
