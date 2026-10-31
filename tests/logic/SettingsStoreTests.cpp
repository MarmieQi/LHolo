#include "settings/SettingsStore.h"
#include "nlohmann/json.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

int runSettingsStoreTests() {
    using namespace lholo::settings;
    using Json = nlohmann::ordered_json;
    int checks{}, failures{};
    auto check = [&](bool ok, char const* message) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "SettingsStoreTests: %s\n", message); }
    };
    auto const path = std::filesystem::temp_directory_path() / "lholo_settings_schema_test.json";
    auto backup = path; backup += L".legacy.bak";
    auto temporary = path; temporary += L".tmp";
    auto cleanup = [&] {
        std::error_code error;
        for (auto const& file : {path, backup, temporary}) std::filesystem::remove(file, error);
    };
    cleanup();
    auto write = [&](std::string const& value) { std::ofstream(path, std::ios::binary | std::ios::trunc) << value; };
    auto contents = [](std::filesystem::path const& file) {
        std::ifstream stream(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), {});
    };
    Settings defaults, loaded;
    check(!loadSettingsFile(path, loaded), "missing settings return false");
    saveSettingsFile(path, defaults);
    auto canonical = contents(path);
    auto json = Json::parse(canonical);
    check(json.at("version") == kSettingsVersion && json.size() == 6, "six canonical top-level sections");
    check(json.at("input").at("hotkeys").at("openMenu") == "Alt+M", "readable default shortcut");
    check(json["input"]["hotkeys"]["materialHudPreviousPage"].is_null()
        && json["input"]["hotkeys"]["materialHudNextPage"].is_null(), "paging remains unbound by default");
    check(json["hud"]["materials"]["convertToStacks"] == false && json["hud"]["materials"]["itemsPerPage"] == 5,
        "material defaults preserved");
    check(json["rendering"]["correction"]["fillOpacityPercent"] == 15
        && json["rendering"]["correction"]["outlineOpacityPercent"] == 100, "clean percent values");
    check(!json["hud"]["projection"]["position"].contains("custom"), "unused custom coordinates omitted");
    check(canonical.ends_with('\n') && canonical.find("\n    \"interface\"") != std::string::npos, "ordered indented JSON with final newline");
    check(loadSettingsFile(path, loaded) && loaded == defaults, "all defaults round-trip");
    check(loaded.writeHudEnabled && loaded.writeHudPosition == kTopCenterHudPosition
        && json["hud"]["write"]["position"]["anchor"] == "topCenter", "independent write HUD defaults persist at top center");
    saveSettingsFile(path, loaded);
    check(contents(path) == canonical && !std::filesystem::exists(temporary), "stable output and completed atomic replace");

    Settings custom;
    custom.language = "en_US";
    custom.correctionFillOpacity = 0.3f; custom.correctionOutlineOpacity = 0.7f;
    custom.structureBoundsEnabled = false; custom.correctionSeeThrough = true; custom.missingSeeThrough = true;
    custom.experimentalConsent = true;
    custom.materialHudEnabled = true; custom.materialHudStackConversion = true; custom.materialHudMaxItems = 23;
    custom.materialHudPosition = 2; custom.hudPosition = 0;
    custom.placementRadius = 2; custom.autoPlacementBreakCooldownSeconds = 27;
    custom.hudEnabled = false; custom.hudShowFileName = false; custom.hudShowLayer = false;
    custom.hudShowOverallProgress = true; custom.hudShowProgress = false; custom.hudShowWrongState = false;
    custom.hudShowWrongType = false; custom.hudShowExtraBlocks = false; custom.hudShowProjectedBlockName = false;
    custom.hudPlacements = {{{true, 0.2f, 0.3f}, {true, 0.7f, 0.6f}, {true, 0.4f, 0.05f}}};
    custom.hudScales = {75, 160, 125}; custom.hudBackgroundOpacities = {35, 80, 45};
    custom.writeHudEnabled = false;
    custom.hudPlacements[2] = {true, 0.4f, 0.05f};
    custom.guiHotkey = 'L'; custom.guiHotkeyModifiers = 1;
    custom.layerIncreaseHotkey = 135; custom.layerIncreaseHotkeyModifiers = 3; // F24
    custom.layerDecreaseHotkey = 101; custom.layerDecreaseHotkeyModifiers = 4; // Numpad5
    custom.materialHudPreviousPageHotkey = 33; custom.materialHudPreviousPageHotkeyModifiers = 2;
    custom.materialHudNextPageHotkey = 6; custom.materialHudNextPageHotkeyModifiers = 1;
    custom.moveHotkeys = {186, 'X', 'Y', 'Z', 'W', 'S'};
    custom.moveHotkeyModifiers = {0, 1, 2, 3, 4, 7}; custom.altWheelOffsetEnabled = false;
    saveSettingsFile(path, custom);
    check(loadSettingsFile(path, loaded) && loaded == custom, "every preference and hotkey survives structured round-trip");
    json = Json::parse(contents(path));
    check(json["input"]["hotkeys"]["layerIncrease"] == "Ctrl+Alt+F24"
        && json["input"]["hotkeys"]["layerDecrease"] == "Shift+Numpad5"
        && json["input"]["hotkeys"]["moveLeft"] == "VK_186", "function, numpad and uncommon keys retain identity");
    json["input"]["hotkeys"]["openMenu"] = " ctrl + l ";
    write(json.dump());
    check(loadSettingsFile(path, loaded) && loaded.guiHotkey == 'L' && loaded.guiHotkeyModifiers == 1,
        "hand-edited shortcuts accept case and surrounding whitespace");

    std::string const legacy = R"({
  "version":12,"language":"en_US","uiScale":"retired","lastStructurePath":null,
  "savedAnchorX":"retired","savedStructurePath":[],"hasSavedProjection":true,
  "hudShowBlockEntity":false,"hudShowExtraBlocks":false,"hudPosition":2,
  "materialHudEnabled":true,"materialHudStackConversion":true,"materialHudMaxItems":9,
  "materialHudPreviousPageHotkey":33,"materialHudPreviousPageHotkeyModifiers":2,
  "correctionFillOpacity":0.3,"correctionOutlineOpacity":0.7,"correctionSeeThrough":true,
  "autoPlacementBreakCooldownSeconds":27,"experimentalConsent":true,
  "altWheelOffsetEnabled":false,"moveXMinusHotkey":65,"moveXMinusHotkeyModifiers":2,"moveYPlusHotkey":87,
  "hudLayout":{"projection":{"custom":true,"x":0.2,"y":0.3,"scale":75,"backgroundOpacity":35}},
  "opacity":"retired","toggleManualHotkey":82
})";
    write(legacy);
    check(loadSettingsFile(path, loaded), "legacy preference file loads despite malformed retired fields");
    check(loaded.language == "en_US" && !loaded.hudShowProjectedBlockName && !loaded.hudShowExtraBlocks
        && loaded.hudPosition == 2 && loaded.hudScales[0] == 75 && loaded.hudBackgroundOpacities[0] == 35
        && loaded.hudPlacements[0] == HudPlacement{true, 0.2f, 0.3f}, "legacy HUD/language preferences migrate");
    check(loaded.materialHudEnabled && loaded.materialHudStackConversion && loaded.materialHudMaxItems == 9
        && loaded.materialHudPreviousPageHotkey == 33 && loaded.materialHudPreviousPageHotkeyModifiers == 2,
        "legacy material controls and paging key migrate");
    check(loaded.correctionFillOpacity == 0.3f && loaded.correctionOutlineOpacity == 0.7f
        && loaded.correctionSeeThrough && loaded.autoPlacementBreakCooldownSeconds == 27 && loaded.experimentalConsent,
        "legacy render and placement preferences migrate");
    check(!loaded.altWheelOffsetEnabled && loaded.moveHotkeys[0] == 'A' && loaded.moveHotkeyModifiers[0] == 2
        && loaded.moveHotkeys[4] == 'W', "axis-era move bindings keep user choices");
    auto migrated = loaded;
    saveSettingsFile(path, migrated);
    check(contents(backup) == legacy, "migration keeps an exact one-time legacy backup");
    check(loadSettingsFile(path, loaded) && loaded == migrated, "migration reopens without changing live preferences");
    json = Json::parse(contents(path));
    check(json.size() == 6 && !json.contains("hasSavedProjection") && !json.contains("uiScale")
        && !json.contains("lastStructurePath") && !json.contains("opacity") && !json.contains("toggleManualHotkey"),
        "retired settings disappear from the canonical file");
    saveSettingsFile(path, loaded);
    check(contents(backup) == legacy, "later saves preserve the original backup");
    write(R"({"hudShowProjectedBlockName":true,"hudShowBlockEntity":"retired","moveLeftHotkey":66,"moveXMinusHotkey":null})");
    check(loadSettingsFile(path, loaded) && loaded.hudShowProjectedBlockName && loaded.moveHotkeys[0] == 'B',
        "current legacy names take precedence without evaluating obsolete aliases");
    for (auto const* text : {"{}", "{\"language\":1}", "{\"version\":13}"}) {
        write(text);
        check(loadSettingsFile(path, loaded) && loaded == defaults, "missing current and legacy fields use defaults");
    }
    write(R"({"version":13,"hud":{"materials":{"itemsPerPage":500,"scalePercent":500,"backgroundOpacityPercent":-1,
        "position":{"anchor":"bottomRight","custom":{"x":2,"y":-1}}}},
        "rendering":{"correction":{"fillOpacityPercent":-5,"outlineOpacityPercent":500}},
        "placement":{"radius":100,"breakCooldownSeconds":-5}})");
    check(loadSettingsFile(path, loaded) && loaded.materialHudMaxItems == 50 && loaded.hudScales[1] == 200
        && loaded.hudBackgroundOpacities[1] == 0 && loaded.hudPlacements[1] == HudPlacement{true, 1, 0}
        && loaded.correctionFillOpacity == 0 && loaded.correctionOutlineOpacity == 1
        && loaded.placementRadius == 4 && loaded.autoPlacementBreakCooldownSeconds == 0, "numeric fields clamp at the file boundary");

    for (auto const* text : {"[1]", "{", "{\"version\":14}", "{\"version\":13.5}", "{\"version\":4294967309}",
        "{\"version\":13,\"hud\":{\"materials\":{\"itemsPerPage\":5.5}}}", "{\"version\":\"13\"}",
        "{\"version\":13,\"hud\":null}", "{\"version\":13,\"hud\":{\"materials\":{\"enabled\":1}}}",
        "{\"version\":13,\"hud\":{\"projection\":{\"position\":{\"anchor\":\"oops\"}}}}",
        "{\"version\":13,\"input\":{\"hotkeys\":{\"openMenu\":\"Ctrl+Ctrl+M\"}}}",
        "{\"version\":13,\"input\":{\"hotkeys\":{\"openMenu\":\"Alt+Unknown\"}}}"}) {
        write(text);
        loaded = custom;
        bool rejected{};
        try { (void)loadSettingsFile(path, loaded); } catch (std::exception const&) { rejected = true; }
        check(rejected && loaded == custom && contents(path) == text, "bad input does not partially apply or modify the file");
        rejected = false;
        try { saveSettingsFile(path, defaults); } catch (std::exception const&) { rejected = true; }
        check(rejected && contents(path) == text, "later saves cannot overwrite unreadable or future preferences");
    }
    auto const example = std::filesystem::path{"docs/config.example.json"};
    if (std::filesystem::exists(example)) {
        check(loadSettingsFile(example, loaded) && loaded == defaults, "documented example matches native defaults");
        cleanup();
        saveSettingsFile(path, defaults);
        auto exampleText = contents(example);
        std::erase(exampleText, '\r');
        check(contents(path) == exampleText, "documented example has canonical layout and default values");
    }
    cleanup();
    std::printf("SettingsStoreTests: %d checks, %d failures\n", checks, failures);
    return failures;
}
