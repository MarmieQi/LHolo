// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "settings/SettingsStore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <system_error>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <nlohmann/json.hpp>

namespace lholo::settings {
namespace {
using Json = nlohmann::ordered_json;
std::mutex writeMutex;
constexpr std::array<char const*, 5> Anchors{"topLeft", "bottomLeft", "topRight", "bottomRight", "topCenter"};

struct Binding { char const* name; int& key; int& modifiers; };
std::array<Binding, input::kHotkeyCount> bindings(Settings& s) {
    return {{{"openMenu", s.guiHotkey, s.guiHotkeyModifiers},
        {"moveLeft", s.moveHotkeys[0], s.moveHotkeyModifiers[0]},
        {"moveRight", s.moveHotkeys[1], s.moveHotkeyModifiers[1]},
        {"moveForward", s.moveHotkeys[2], s.moveHotkeyModifiers[2]},
        {"moveBackward", s.moveHotkeys[3], s.moveHotkeyModifiers[3]},
        {"moveUp", s.moveHotkeys[4], s.moveHotkeyModifiers[4]},
        {"moveDown", s.moveHotkeys[5], s.moveHotkeyModifiers[5]},
        {"layerIncrease", s.layerIncreaseHotkey, s.layerIncreaseHotkeyModifiers},
        {"layerDecrease", s.layerDecreaseHotkey, s.layerDecreaseHotkeyModifiers},
        {"materialHudPreviousPage", s.materialHudPreviousPageHotkey, s.materialHudPreviousPageHotkeyModifiers},
        {"materialHudNextPage", s.materialHudNextPageHotkey, s.materialHudNextPageHotkeyModifiers}}};
}
struct NamedKey { int code; char const* name; };
constexpr NamedKey NamedKeys[]{
    {1,"MouseLeft"},{2,"MouseRight"},{4,"MouseMiddle"},{5,"MouseSide1"},{6,"MouseSide2"},
    {8,"Backspace"},{9,"Tab"},{13,"Enter"},{19,"Pause"},{20,"CapsLock"},{27,"Escape"},{32,"Space"},
    {33,"PageUp"},{34,"PageDown"},{35,"End"},{36,"Home"},{37,"Left"},{38,"Up"},{39,"Right"},{40,"Down"},
    {45,"Insert"},{46,"Delete"},{106,"NumpadMultiply"},{107,"NumpadAdd"},{109,"NumpadSubtract"},
    {110,"NumpadDecimal"},{111,"NumpadDivide"},{144,"NumLock"},{145,"ScrollLock"}
};
std::string lower(std::string value) {
    auto const first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    value = value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
std::string keyName(int code) {
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) return std::string(1, static_cast<char>(code));
    if (code >= 112 && code <= 135) return "F" + std::to_string(code - 111);
    if (code >= 96 && code <= 105) return "Numpad" + std::to_string(code - 96);
    for (auto const& key : NamedKeys) if (key.code == code) return key.name;
    return "VK_" + std::to_string(code);
}
int parseKey(std::string const& name) {
    auto const token = lower(name);
    if (token.size() == 1 && ((token[0] >= 'a' && token[0] <= 'z') || (token[0] >= '0' && token[0] <= '9')))
        return std::toupper(static_cast<unsigned char>(token[0]));
    for (auto const& key : NamedKeys) if (lower(key.name) == token) return key.code;
    for (int code = 1; code <= 255; ++code) if (lower(keyName(code)) == token) return code;
    throw std::invalid_argument("Unknown hotkey name: " + name);
}
Json encodeBinding(Binding const& binding) {
    if (!binding.key) return nullptr;
    std::string value;
    if (binding.modifiers & 1) value += "Ctrl+";
    if (binding.modifiers & 2) value += "Alt+";
    if (binding.modifiers & 4) value += "Shift+";
    return value + keyName(binding.key);
}
void decodeBinding(Json const& value, Binding binding) {
    if (value.is_null()) { binding.key = binding.modifiers = 0; return; }
    auto const chord = value.get<std::string>();
    int modifiers{};
    std::size_t start{};
    for (auto end = chord.find('+'); end != std::string::npos; end = chord.find('+', start)) {
        auto const modifier = lower(chord.substr(start, end - start));
        auto const bit = modifier == "ctrl" ? 1 : modifier == "alt" ? 2 : modifier == "shift" ? 4 : 0;
        if (!bit || (modifiers & bit)) throw std::invalid_argument("Invalid hotkey modifiers: " + chord);
        modifiers |= bit;
        start = end + 1;
    }
    binding.key = parseKey(chord.substr(start));
    binding.modifiers = modifiers;
}
Json section(Json const& parent, char const* name) {
    auto result = parent.value(name, Json::object());
    if (!result.is_object()) throw std::invalid_argument(std::string{name} + " must be an object");
    return result;
}
int integer(Json const& parent, char const* name, int fallback, int minimum, int maximum) {
    if (!parent.contains(name)) return fallback;
    auto const& number = parent.at(name);
    if (!number.is_number_integer()) throw std::invalid_argument(std::string{name} + " must be an integer");
    if (number < minimum) return minimum;
    if (number > maximum) return maximum;
    return number.get<int>();
}
int version(Json const& data) {
    if (!data.is_object()) throw std::invalid_argument("Settings root must be an object");
    auto const number = data.value("version", Json(0));
    if (!number.is_number_integer() || number < 0 || number > kSettingsVersion)
        throw std::invalid_argument("Unsupported settings version");
    return number.get<int>();
}
void normalize(Settings& s) {
    auto finite = [](float value, float lo, float hi) {
        if (!std::isfinite(value)) throw std::invalid_argument("Settings contain a non-finite number");
        return std::clamp(value, lo, hi);
    };
    s.correctionFillOpacity = finite(s.correctionFillOpacity, 0, 1);
    s.correctionOutlineOpacity = finite(s.correctionOutlineOpacity, 0, 1);
    s.materialHudMaxItems = std::clamp(s.materialHudMaxItems, kMinMaterialHudItems, kMaxMaterialHudItems);
    s.writeHudPosition = std::clamp(s.writeHudPosition, 0, kTopCenterHudPosition);
    s.hudPosition = std::clamp(s.hudPosition, 0, 3);
    s.materialHudPosition = std::clamp(s.materialHudPosition, 0, 3);
    s.placementRadius = std::clamp(s.placementRadius, 1, 4);
    s.autoPlacementBreakCooldownSeconds = std::clamp(s.autoPlacementBreakCooldownSeconds, 0, 60);
    for (std::size_t n = 0; n < kHudPanelCount; ++n) {
        s.hudScales[n] = std::clamp(s.hudScales[n], 50, 200);
        s.hudBackgroundOpacities[n] = std::clamp(s.hudBackgroundOpacities[n], 0, 100);
        s.hudPlacements[n].x = finite(s.hudPlacements[n].x, 0, 1);
        s.hudPlacements[n].y = finite(s.hudPlacements[n].y, 0, 1);
    }
    for (auto binding : bindings(s)) {
        binding.key = std::clamp(binding.key, 0, 255);
        binding.modifiers = binding.key ? std::clamp(binding.modifiers, 0, 7) : 0;
    }
}
void readLegacy(Json const& json, Settings& out) {
    if (auto const language = json.find("language");
        language != json.end() && language->is_string()) {
        out.language = language->get<std::string>();
    } else {
        // Language preferences intentionally have no old integer migration:
        // missing or malformed values use the default locale.
        out.language = "zh_CN";
    }
    out.correctionFillOpacity = json.value("correctionFillOpacity", out.correctionFillOpacity);
    out.correctionOutlineOpacity = json.value("correctionOutlineOpacity", out.correctionOutlineOpacity);
    out.structureBoundsEnabled = json.value("structureBoundsEnabled", out.structureBoundsEnabled);
    out.correctionSeeThrough = json.value("correctionSeeThrough", out.correctionSeeThrough);
    out.missingSeeThrough = json.value("missingSeeThrough", out.missingSeeThrough);
    out.experimentalConsent = json.value("experimentalConsent", out.experimentalConsent);
    out.materialHudEnabled = json.value("materialHudEnabled", out.materialHudEnabled);
    out.materialHudStackConversion = json.value("materialHudStackConversion", out.materialHudStackConversion);
    out.materialHudMaxItems = std::clamp(json.value("materialHudMaxItems", out.materialHudMaxItems), kMinMaterialHudItems, kMaxMaterialHudItems);
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
    out.hudShowProjectedBlockName = json.contains("hudShowProjectedBlockName")
        ? json.at("hudShowProjectedBlockName").get<bool>()
        : json.value("hudShowBlockEntity", out.hudShowProjectedBlockName);
    out.hudPosition = json.value("hudPosition", out.hudPosition);
    auto const layout = json.value("hudLayout", nlohmann::json::object());
    std::array<char const*, 2> const names{"projection", "materials"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        auto const item = layout.value(names[index], nlohmann::json::object());
        out.hudScales[index] = std::clamp(item.value("scale", 100), 50, 200);
        out.hudBackgroundOpacities[index] = std::clamp(item.value("backgroundOpacity", 60), 0, 100);
        out.hudPlacements[index] = {item.value("custom", false),
            std::clamp(item.value("x", 0.0f), 0.0f, 1.0f), std::clamp(item.value("y", 0.0f), 0.0f, 1.0f)};
    }
    out.guiHotkey = json.value("guiHotkey", out.guiHotkey);
    out.guiHotkeyModifiers = json.value("guiHotkeyModifiers", out.guiHotkeyModifiers);
    out.layerIncreaseHotkey = json.value("layerIncreaseHotkey", out.layerIncreaseHotkey);
    out.layerDecreaseHotkey = json.value("layerDecreaseHotkey", out.layerDecreaseHotkey);
    out.layerIncreaseHotkeyModifiers
        = json.value("layerIncreaseHotkeyModifiers", out.layerIncreaseHotkeyModifiers);
    out.layerDecreaseHotkeyModifiers
        = json.value("layerDecreaseHotkeyModifiers", out.layerDecreaseHotkeyModifiers);
    out.materialHudPreviousPageHotkey = json.value("materialHudPreviousPageHotkey", out.materialHudPreviousPageHotkey);
    out.materialHudPreviousPageHotkeyModifiers = json.value("materialHudPreviousPageHotkeyModifiers", out.materialHudPreviousPageHotkeyModifiers);
    out.materialHudNextPageHotkey = json.value("materialHudNextPageHotkey", out.materialHudNextPageHotkey);
    out.materialHudNextPageHotkeyModifiers = json.value("materialHudNextPageHotkeyModifiers", out.materialHudNextPageHotkeyModifiers);
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
        out.moveHotkeys[index] = json.contains(moveKeyNames[index])
            ? json.at(moveKeyNames[index]).get<int>()
            : json.value(legacyMoveKeyNames[index], out.moveHotkeys[index]);
        out.moveHotkeyModifiers[index] = json.contains(moveModifierNames[index])
            ? json.at(moveModifierNames[index]).get<int>()
            : json.value(legacyMoveModifierNames[index], out.moveHotkeyModifiers[index]);
    }
}
void readLayout(Json const& panel, Settings& out, std::size_t n, int& anchor) {
    out.hudScales[n] = integer(panel, "scalePercent", out.hudScales[n], 50, 200);
    out.hudBackgroundOpacities[n] = integer(panel, "backgroundOpacityPercent", out.hudBackgroundOpacities[n], 0, 100);
    auto const position = section(panel, "position");
    auto const name = position.value("anchor", std::string{Anchors[anchor]});
    auto const found = std::find(Anchors.begin(), Anchors.end(), name);
    if (found == Anchors.end() || (n != static_cast<std::size_t>(HudPanelId::Write) && found - Anchors.begin() > 3)) throw std::invalid_argument("Unknown HUD anchor: " + name);
    anchor = static_cast<int>(found - Anchors.begin());
    if (position.contains("custom")) {
        auto const custom = section(position, "custom");
        out.hudPlacements[n] = {true, custom.at("x").get<float>(), custom.at("y").get<float>()};
    }
}
void readCurrent(Json const& data, Settings& out) {
    auto const interfaceSettings = section(data, "interface");
    out.language = interfaceSettings.value("language", out.language);
    auto const rendering = section(data, "rendering");
    out.structureBoundsEnabled = rendering.value("structureBounds", out.structureBoundsEnabled);
    out.missingSeeThrough = rendering.value("missingSeeThrough", out.missingSeeThrough);
    auto const correction = section(rendering, "correction");
    out.correctionFillOpacity = correction.value("fillOpacityPercent", out.correctionFillOpacity * 100.0f) / 100.0f;
    out.correctionOutlineOpacity = correction.value("outlineOpacityPercent", out.correctionOutlineOpacity * 100.0f) / 100.0f;
    out.correctionSeeThrough = correction.value("seeThrough", out.correctionSeeThrough);
    auto const hud = section(data, "hud");
    auto const projection = section(hud, "projection");
    out.hudEnabled = projection.value("enabled", out.hudEnabled);
    auto const show = section(projection, "show");
    out.hudShowFileName = show.value("fileName", out.hudShowFileName);
    out.hudShowLayer = show.value("layer", out.hudShowLayer);
    out.hudShowOverallProgress = show.value("overallProgress", out.hudShowOverallProgress);
    out.hudShowProgress = show.value("progress", out.hudShowProgress);
    out.hudShowWrongState = show.value("wrongState", out.hudShowWrongState);
    out.hudShowWrongType = show.value("wrongType", out.hudShowWrongType);
    out.hudShowExtraBlocks = show.value("extraBlocks", out.hudShowExtraBlocks);
    out.hudShowProjectedBlockName = show.value("projectedBlockName", out.hudShowProjectedBlockName);
    readLayout(projection, out, 0, out.hudPosition);
    auto const materials = section(hud, "materials");
    out.materialHudEnabled = materials.value("enabled", out.materialHudEnabled);
    out.materialHudStackConversion = materials.value("convertToStacks", out.materialHudStackConversion);
    out.materialHudMaxItems = integer(materials, "itemsPerPage", out.materialHudMaxItems, kMinMaterialHudItems, kMaxMaterialHudItems);
    readLayout(materials, out, 1, out.materialHudPosition);
    auto const write = section(hud, "write");
    out.writeHudEnabled = write.value("enabled", out.writeHudEnabled);
    readLayout(write, out, 2, out.writeHudPosition);
    auto const input = section(data, "input");
    out.altWheelOffsetEnabled = input.value("altWheelOffset", out.altWheelOffsetEnabled);
    auto const hotkeys = section(input, "hotkeys");
    for (auto binding : bindings(out)) if (hotkeys.contains(binding.name)) decodeBinding(hotkeys.at(binding.name), binding);
    auto const placement = section(data, "placement");
    out.experimentalConsent = placement.value("consentGiven", out.experimentalConsent);
    out.placementRadius = integer(placement, "radius", out.placementRadius, 1, 4);
    out.autoPlacementBreakCooldownSeconds = integer(placement, "breakCooldownSeconds", out.autoPlacementBreakCooldownSeconds, 0, 60);
}
Json position(Settings const& s, std::size_t n, int anchor) {
    Json result{{"anchor", Anchors[anchor]}};
    if (s.hudPlacements[n].custom)
        result["custom"] = {{"x", std::round(s.hudPlacements[n].x * 1000000.0) / 1000000.0},
                            {"y", std::round(s.hudPlacements[n].y * 1000000.0) / 1000000.0}};
    return result;
}
Json percent(float opacity) {
    auto const value = std::round(opacity * 10000.0) / 100.0;
    return value == std::floor(value) ? Json(static_cast<int>(value)) : Json(value);
}
Json encode(Settings s) {
    normalize(s);
    Json hotkeys = Json::object();
    for (auto binding : bindings(s)) hotkeys[binding.name] = encodeBinding(binding);
    return {
        {"version", kSettingsVersion},
        {"interface", {{"language", s.language}}},
        {"rendering", {{"structureBounds", s.structureBoundsEnabled},
            {"correction", {{"fillOpacityPercent", percent(s.correctionFillOpacity)},
                {"outlineOpacityPercent", percent(s.correctionOutlineOpacity)}, {"seeThrough", s.correctionSeeThrough}}},
            {"missingSeeThrough", s.missingSeeThrough}}},
        {"hud", {
            {"projection", {{"enabled", s.hudEnabled},
                {"show", {{"fileName", s.hudShowFileName}, {"layer", s.hudShowLayer},
                    {"overallProgress", s.hudShowOverallProgress}, {"progress", s.hudShowProgress},
                    {"wrongState", s.hudShowWrongState}, {"wrongType", s.hudShowWrongType},
                    {"extraBlocks", s.hudShowExtraBlocks}, {"projectedBlockName", s.hudShowProjectedBlockName}}},
                {"position", position(s, 0, s.hudPosition)},
                {"scalePercent", s.hudScales[0]}, {"backgroundOpacityPercent", s.hudBackgroundOpacities[0]}}},
            {"materials", {{"enabled", s.materialHudEnabled}, {"convertToStacks", s.materialHudStackConversion},
                {"itemsPerPage", s.materialHudMaxItems}, {"position", position(s, 1, s.materialHudPosition)},
                {"scalePercent", s.hudScales[1]}, {"backgroundOpacityPercent", s.hudBackgroundOpacities[1]}}},
            {"write", {{"enabled", s.writeHudEnabled}, {"position", position(s, 2, s.writeHudPosition)},
                {"scalePercent", s.hudScales[2]}, {"backgroundOpacityPercent", s.hudBackgroundOpacities[2]}}}}},
        {"input", {{"altWheelOffset", s.altWheelOffsetEnabled}, {"hotkeys", std::move(hotkeys)}}},
        {"placement", {{"consentGiven", s.experimentalConsent}, {"radius", s.placementRadius},
            {"breakCooldownSeconds", s.autoPlacementBreakCooldownSeconds}}}
    };
}
Json read(std::filesystem::path const& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open settings file");
    return Json::parse(stream, nullptr, true, true);
}
Settings decode(Json const& data) {
    Settings candidate;
    if (version(data) == kSettingsVersion) readCurrent(data, candidate);
    else readLegacy(data, candidate);
    normalize(candidate);
    return candidate;
}
} // namespace

bool loadSettingsFile(std::filesystem::path const& path, Settings& out) {
    if (!std::filesystem::exists(path)) return false;
    auto candidate = decode(read(path));
    out = std::move(candidate);
    return true;
}

void saveSettingsFile(std::filesystem::path const& path, Settings const& settings) {
    std::lock_guard lock(writeMutex);
    auto const data = encode(settings).dump(4) + '\n';
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    if (std::filesystem::exists(path)) {
        auto const original = read(path);
        (void)decode(original); // Never replace an invalid or future configuration.
        if (version(original) < kSettingsVersion) {
            auto backup = path;
            backup += L".legacy.bak";
            std::filesystem::copy_file(path, backup, std::filesystem::copy_options::skip_existing);
        }
    }
    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream << data;
        stream.close();
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Saving settings");
}
} // namespace lholo::settings
