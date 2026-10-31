#include "ui/hud/HudContent.h"
#include "input/MaterialHudPageRequest.h"
#include "structure/StructureUiState.h"
#include "ui/hud/HudLayout.h"
#include "ui/hud/HudText.h"
#include "ui/hud/editor/HudEditor.h"
#include "ui/MaterialFormat.h"
#include <algorithm>
#include "i18n/LanguageStore.h"
#include <cstdio>
#include <cmath>
#include <limits>

int runNativeHudTests() {
    using namespace lholo;
    using namespace ui::hud;
    int checks{}, failures{};
    auto check = [&](bool ok, char const* message) {
        ++checks;
        if (!ok) { ++failures; std::printf("NativeHudTests: %s\n", message); }
    };
    auto const oldLanguage = i18n::language();
    i18n::setLanguageByCode("en_US");
    WriteHudData write{77, 0, 226270, 471212, true, true, "name%中文.litematic", i18n::TextKey::WriteWorldCommandRunning};
    auto writePanel = makeWriteHud(write, settings::kTopCenterHudPosition, 1000);
    check(writePanel.lines.size() == 3 && writePanel.position == 4
        && writePanel.lines[0].text == "Writing file: name%中文.litematic"
        && writePanel.lines[1].text == "Completed: 226270 / 471212 (48%)", "independent write panel formats real file, progress and percentage");
    write.phase = i18n::TextKey::WriteWorldCommandPaused;
    check(makeWriteHud(write, 4, 999999).lines.size() == 3, "paused write remains visible without a projection");
    write.active = false; write.finishedAt = 2000;
    check(!makeWriteHud(write, 4, 4499).lines.empty() && makeWriteHud(write, 4, 4500).lines.empty(),
        "terminal write feedback expires from its actual completion time");
    write.operation = 0;
    check(makeWriteHud(write, 4, 2100).lines.empty(), "world reset or no accepted operation hides write feedback");
    write.operation = 1; write.active = true; write.progressKnown = false;
    check(makeWriteHud(write, 4, 2100).lines.size() == 2, "atomic local placement does not invent per-block progress");
    auto const centered = placeHudPanel(1000, 600, 250, 18, 3, 4);
    auto const resizedCenter = placeHudPanel(1400, 800, 250, 18, 3, 4);
    check(centered.y == kHudMargin && centered.x == (1000 - centered.width) / 2
        && resizedCenter.x == (1400 - resizedCenter.width) / 2, "default write anchor stays at the top center after resize");
    structure::detail::HudStateSnapshot settings;
    ProjectionHudData data;
    data.fileName = "name%中文.mcstructure";
    data.layer = 3; data.maxLayer = 7;
    data.progress = {11, 100, 3, 20, 2, 4, 5};
    data.aimedBlock = "Stone"; data.placementMode = "Manual";
    settings.showOverallProgress = true;
    auto panel = makeProjectionHud(settings, data);
    check(panel.lines.size() == 9 && panel.lines[0].text == "File: name%中文.mcstructure", "all ordinary fields, literal filename");
    check(panel.lines[2].text == "Overall: 11 / 100" && panel.lines[3].text == "Progress: 3 / 20", "overall and visible progress remain distinct");
    check(panel.lines[4].tone == TextTone::WrongState && panel.lines[5].tone == TextTone::WrongType
        && panel.lines[6].tone == TextTone::Extra, "error semantics retain their colors");
    check(panel.lines[7].text == "Projected block: Stone" && panel.lines[8].text == "Assist: Manual", "aimed block and assist mode");
    check(std::none_of(panel.lines.begin(), panel.lines.end(), [](auto const& line) { return line.iconId.has_value(); }),
        "ordinary HUD has no icon column without a resolved target item");
    auto aimedToken = std::make_shared<int>(7);
    data.aimedBlockIconId = "minecraft:bed";
    data.aimedBlockIconItem = std::shared_ptr<ItemStack const>(aimedToken, reinterpret_cast<ItemStack const*>(aimedToken.get()));
    panel = makeProjectionHud(settings, data);
    check(!panel.lines[7].iconId && !panel.lines[7].iconItem && panel.lines[7].iconPrefix.empty()
        && panel.lines[7].text == "Projected block: Stone",
        "aimed block icon is temporarily disabled even when native item data is available");
    data.aimedBlock.clear();
    panel = makeProjectionHud(settings, data);
    check(std::none_of(panel.lines.begin(), panel.lines.end(), [](auto const& line) { return line.iconId.has_value(); }),
        "absent target hides its icon even when old display data remains");
    data.aimedBlock = "Stone";
    settings.enabled = false;
    check(makeProjectionHud(settings, data).lines.empty(), "ordinary HUD disabled");
    settings.enabled = true;
    settings.showFileName = settings.showOverallProgress = settings.showProgress = settings.showWrongState
        = settings.showWrongType = settings.showExtraBlocks = settings.showProjectedBlockName = false;
    data.placementMode.clear();
    struct Case { structure::LayerAxis axis; structure::LayerDisplayMode mode; char const* expected; };
    using structure::LayerAxis; using structure::LayerDisplayMode;
    for (auto const& value : {
        Case{LayerAxis::Y, LayerDisplayMode::All, "Range: whole structure"},
        Case{LayerAxis::X, LayerDisplayMode::Single, "Current layer: 3 / 7 (X axis)"},
        Case{LayerAxis::Y, LayerDisplayMode::UpToCurrent, "Range: layers 0-3 (Y axis)"},
        Case{LayerAxis::Y, LayerDisplayMode::FromCurrent, "Range: layers 3-7 (Y axis)"},
        Case{LayerAxis::Material, LayerDisplayMode::All, "Material filter: all"},
        Case{LayerAxis::Material, LayerDisplayMode::Single, "Material filter: #4 of 8"},
        Case{LayerAxis::Material, LayerDisplayMode::UpToCurrent, "Material filter: #1–4"},
        Case{LayerAxis::Material, LayerDisplayMode::FromCurrent, "Material filter: #4-8"}}) {
        data.axis = value.axis; data.mode = value.mode;
        panel = makeProjectionHud(settings, data);
        check(panel.lines.size() == 1 && panel.lines.front().text == value.expected, "axis and display-range mapping");
    }
    data.layer = 99; data.mode = LayerDisplayMode::Single;
    check(makeProjectionHud(settings, data).lines[0].text == "Material filter: #8 of 8", "stale current layer clamps to maximum");
    settings.showLayer = false; data.placementMode = "Manual";
    check(makeProjectionHud(settings, data).lines.empty(), "all ordinary fields disabled hides assist-only panel");

    structure::detail::MaterialHudSnapshot material;
    check(makeMaterialHud(material, 3).lines[1].tone == TextTone::Muted, "material scan pending");
    material.ready = true;
    check(makeMaterialHud(material, 3).lines[1].tone == TextTone::Success, "empty ready requirements complete");
    structure::detail::MaterialRequirement stone;
    stone.displayName = "Stone"; stone.count = 111;
    stone.itemId = "minecraft:stone"; stone.typeName = "minecraft:stone";
    auto signs = stone; signs.displayName = "Signs"; signs.count = 18; signs.stackSize = 16;
    signs.itemId = "minecraft:oak_sign"; signs.typeName = "minecraft:standing_sign";
    auto dirt = stone; dirt.displayName = "Dirt"; dirt.count = 3;
    dirt.itemId = "minecraft:dirt"; dirt.typeName = "minecraft:dirt";
    material.requirements = {signs, stone, dirt}; material.available = {1, 0, 4};
    panel = makeMaterialHud(material, 3);
    check(panel.position == 3 && panel.lines.size() == 3 && panel.lines[1].text == "Stone" && panel.lines[1].quantity == "111"
        && panel.lines[2].text == "Signs" && panel.lines[2].quantity == "17", "raw inventory deficits by default, sorted without satisfied rows");
    auto converted = makeMaterialHud(material, 3, true);
    check(converted.lines[1].text == "Stone" && converted.lines[1].quantity == "1x64+47"
        && converted.lines[2].text == "Signs" && converted.lines[2].quantity == "1x16+1",
        "conversion replaces raw count and respects each item stack limit");
    check(ui::formatMaterialHudCount(43 * 64 + 34, 64, true) == "43x64+34", "requested stack format");
    check(ui::formatMaterialHudCount(43 * 64 + 34, 64, false) == "2786", "disabled conversion only shows total");
    check(ui::formatMaterialHudCount(128, 64, true) == "2x64", "whole stacks omit empty remainder");
    check(ui::formatMaterialHudCount(34, 64, true) == "34" && ui::formatMaterialHudCount(0, 64, true) == "0", "sub-stack quantities stay raw");
    check(ui::formatMaterialHudCount(3, 1, true) == "3x1" && ui::formatMaterialHudCount(65, 0, true) == "1x64+1", "unstackable items and invalid stack fallback");
    check(!panel.lines.front().iconId && panel.lines[1].iconId == "minecraft:stone"
        && panel.lines[2].iconId == "minecraft:oak_sign", "sorted icons follow their material, preferring placement item identity");
    check(panel.lines[1].tone == TextTone::Normal && panel.lines[2].tone == TextTone::Normal,
        "material icons retain default white text");
    auto redBed = stone; redBed.displayName = "Red Bed"; redBed.itemId = "minecraft:bed";
    // Opaque native item payload: presentation must retain ownership without
    // invoking game APIs or reconstructing the item from its bare type ID.
    auto nativeToken = std::make_shared<int>(1);
    redBed.iconItem = std::shared_ptr<ItemStack const>(nativeToken, reinterpret_cast<ItemStack const*>(nativeToken.get()));
    auto whiteBed = redBed; whiteBed.displayName = "White Bed"; whiteBed.count = 1; whiteBed.iconItem.reset();
    auto beds = makeMaterialHud({{whiteBed, redBed}, {0, 0}, true}, 0);
    check(beds.lines[1].text == "Red Bed" && beds.lines[1].iconId == "minecraft:bed"
        && beds.lines[2].text == "White Bed",
        "sorted material labels retain their shared item type");
    check(beds.lines[1].iconItem == redBed.iconItem && !beds.lines[2].iconItem,
        "full native item payload follows its material through deficit sorting");
    auto liquid = dirt; liquid.itemId.clear(); liquid.typeName = "minecraft:water";
    structure::detail::MaterialHudSnapshot liquidSnapshot{{liquid}, {0}, true};
    check(makeMaterialHud(liquidSnapshot, 0).lines[1].iconId == "minecraft:water" && liquid.itemId.empty(),
        "liquid fallback is display-only and does not invent an inventory item");
    check(ui::materialIconId(std::string{"addon:custom_item"}, std::string{"addon:custom_block"}) == "addon:custom_item",
        "shared icon identity retains custom namespaces");
    check(!makeMaterialHud({}, 0).lines[1].iconId
        && !makeMaterialHud({{}, {}, true}, 0).lines[1].iconId, "pending/complete messages have no item icon");
    material.available = {-1};
    check(makeMaterialHud(material, 0).lines.size() == 4, "short availability vector and negative counts stay safe");
    material.requirements.assign(16, dirt); material.available.clear();
    for (std::size_t index = 0; index < material.requirements.size(); ++index)
        material.requirements[index].displayName = "Dirt" + std::to_string(index);
    material.requirements.back().count = 42;
    panel = makeMaterialHud(material, 2);
    check(panel.lines.size() == 6 && panel.lines.back().iconId.has_value(), "default shows five materials without a summary row");
    auto limited = makeMaterialHud(material, 2, false, 2);
    check(limited.lines.size() == 3 && limited.lines[1].text == "Dirt15"
        && limited.lines[2].text == "Dirt0" && limited.lines.back().quantity == "3",
        "configured limit preserves top deficits without an extra summary");
    check(makeMaterialHud(material, 2, false, 0).lines.size() == 2, "item count clamps to at least one");
    check(makeMaterialHud(material, 2, false, 500).lines.size() == 17, "large limit displays all available materials");
    MaterialHudPaging paging;
    std::vector<std::string> pagedNames;
    for (int n = 0; n < 4; ++n) {
        auto current = makeMaterialHud(material, 2, false, 5, &paging, n == 0 ? 0 : 1);
        check(paging.index == static_cast<std::size_t>(n) && paging.count == 4,
            "sixteen materials form four pages with five rows per page");
        check(current.lines.front().text == "Missing materials"
            && current.lines.front().quantity == std::to_string(n + 1) + "/4" && !current.lines.front().iconId,
            "localized title and right-aligned page indicator remain independent");
        for (std::size_t row = 1; row < current.lines.size(); ++row) pagedNames.push_back(current.lines[row].text);
    }
    check(pagedNames.size() == 16 && pagedNames.front() == "Dirt15", "paging reaches every material");
    bool orderedPages = pagedNames.size() == 16;
    for (std::size_t n = 1; n < pagedNames.size(); ++n)
        orderedPages = orderedPages && pagedNames[n] == "Dirt" + std::to_string(n - 1);
    check(orderedPages, "stable ties have no gaps or duplicates across page boundaries");
    (void)makeMaterialHud(material, 2, false, 5, &paging, 100);
    check(paging.index == 3, "next at last page stays at last page");
    (void)makeMaterialHud(material, 2, false, 5, &paging, -100);
    check(paging.index == 0, "previous clamps to first page");
    (void)makeMaterialHud(material, 2, false, 5, &paging, 1);
    i18n::setLanguageByCode("zh_CN");
    auto translated = makeMaterialHud(material, 2, false, 5, &paging);
    check(translated.lines.front().text == "缺失材料" && translated.lines.front().quantity == "2/4",
        "language switches update pagination text without resetting the page");
    i18n::setLanguageByCode("en_US");
    auto fewer = material;
    fewer.requirements.resize(3);
    (void)makeMaterialHud(material, 2, false, 5, &paging, 100);
    auto shrunk = makeMaterialHud(fewer, 2, false, 5, &paging);
    check(paging.index == 0 && paging.count == 1 && shrunk.lines.size() == 4,
        "shrinking deficits clamp page index before rendering");
    fewer.available.assign(3, 1000);
    auto complete = makeMaterialHud(fewer, 2, false, 5, &paging);
    check(complete.lines.size() == 2 && complete.lines.front().text == "Missing materials" && complete.lines.front().quantity == "1/1",
        "completed materials have one empty page with existing completion text");
    (void)makeMaterialHud(material, 2, false, 5, &paging, 2);
    check(paging.setContext({100, 0, 5, 0, 0, 0}) && paging.index == 0, "new structure resets page");
    paging.update(16, 5, 2);
    check(!paging.setContext({100, 0, 5, 0, 0, 0}) && paging.index == 2, "unchanged context retains page");
    check(paging.setContext({100, 1, 5, 0, 0, 0}) && paging.index == 0, "selecting another file resets page");
    paging.update(16, 5, 2);
    check(paging.setContext({100, 1, 10, 0, 0, 0}) && paging.index == 0, "page size changes reset page");
    paging.update(16, 5, 2);
    check(paging.setContext({100, 1, 10, 1, 1, 3}) && paging.index == 0, "layer changes reset page");
    input::clearMaterialHudPageRequest();
    input::requestMaterialHudPage(1); input::requestMaterialHudPage(1);
    check(input::consumeMaterialHudPageRequest() == 2 && input::consumeMaterialHudPageRequest() == 0,
        "input intent is accumulated and consumed once without touching HUD state");
    input::requestMaterialHudPage(1);
    structure::detail::StructureUiState::getInstance().resetWorldSession();
    check(input::consumeMaterialHudPageRequest() == 0, "world reset clears stale page input");
    auto const nameWidth = hudNameWidth(200, 20, 80, 10);
    check(nameWidth == 90, "name width reserves icon, quantity and gap");
    check(20 + nameWidth + 10 + 80 <= 200, "name and quantity remain separated within the panel");
    check(hudWidthScale(300, 200) == 1.0f && hudWidthScale(300, 600) == 0.5f,
        "full names grow panel naturally and scale down only beyond viewport width");
    auto const fitted = hudWidthScale(300, 600);
    check(600 * fitted <= 300 && hudWidthScale(300, 600 * fitted) == 1.0f,
        "adaptive material panel fits without shortening names");
    check(hudNameWidth(30, 0, 80, 10) == 0,
        "narrow viewport preserves quantity space without overlap");
    bool stableTopRows = panel.lines[1].text == "Dirt15" && panel.lines[1].quantity == "42";
    for (std::size_t index = 2; index <= 5; ++index)
        stableTopRows = stableTopRows && panel.lines[index].text == "Dirt" + std::to_string(index - 2) && panel.lines[index].quantity == "3";
    check(stableTopRows, "top-five selection includes largest shortage and preserves equal-count tracker order");
    check(!makeMaterialHud(material, 2).lines.empty() && makeProjectionHud(settings, data).lines.empty(), "material HUD independent of ordinary HUD");
    check(makeMaterialHud(material, 2).compactRows && !makeProjectionHud(settings, data).compactRows,
        "only material HUD selects compact rows");

    auto const material100 = placeHudPanel(1000, 1000, 100, 10, 4, 0, nullptr, true);
    auto const material200 = placeHudPanel(1000, 1000, 200, 20, 4, 0, nullptr, true, 2.0f);
    check(std::abs(material200.width - material100.width * 2) < 0.001f
        && std::abs(material200.height - material100.height * 2) < 0.001f,
        "material scale doubles text padding and compact row spacing together");
    auto const largeAtEdge = applyHudPlacement(material200, 1000, 1000, {true, 0.99f, 0.99f});
    check(largeAtEdge.x + largeAtEdge.width <= 1000 && largeAtEdge.y + largeAtEdge.height <= 1000,
        "scaled HUD remains inside viewport at a saved edge position");
    for (int corner = 0; corner < 4; ++corner) {
        auto const primary = placeHudPanel(320, 240, 90, 10, 4, corner);
        auto const secondary = placeHudPanel(320, 240, 120, 10, 16, corner, &primary, true);
        check(primary.rows == 4 && secondary.rows > 0, "both panels fit in every corner");
        check(std::abs(primary.height - 560.0f / 9.0f) < 0.001f, "legacy HUD padding and spacing proportions");
        check(primary.x >= 12 && primary.y >= 12 && secondary.x >= 12 && secondary.y >= 12
            && primary.x + primary.width <= 308 && secondary.y + secondary.height <= 228, "panel viewport bounds");
        check((corner & 1) ? secondary.y + secondary.height + hudStyle(10).lineGap <= primary.y + 0.001f
            : primary.y + primary.height + hudStyle(10).lineGap <= secondary.y + 0.001f, "same-corner panels stack inward without overlap");
        check(corner < 2 ? primary.x == 12 : primary.x + primary.width == 308, "left/right anchoring");
    }
    auto const compact = placeHudPanel(320, 240, 90, 10, 4, 0, nullptr, true);
    auto const moved = applyHudPlacement(compact, 320, 240, {true, 0.5f, 0.25f});
    check(moved.x == 160 && moved.y == 60 && moved.width == compact.width && moved.height == compact.height,
        "custom placement moves without changing HUD size");
    auto const edge = applyHudPlacement(compact, 320, 240, {true, 1, 1});
    check(std::abs(edge.x + edge.width - 320) < 0.001f && std::abs(edge.y + edge.height - 240) < 0.001f, "custom positions reach right and bottom screen edges");
    auto const origin = applyHudPlacement(compact, 320, 240, {true, 0, 0});
    check(origin.x == 0 && origin.y == 0, "custom positions reach top and left screen edges");
    auto const resized = applyHudPlacement(compact, 640, 480, {true, 0.5f, 0.25f});
    check(resized.x == 320 && resized.y == 120, "saved positions follow viewport resize");
    check(std::abs(compact.height - (40.0f + 6.0f + 80.0f / 9.0f)) < 0.001f,
        "compact material rows use two-unit gaps and unchanged outer padding");
    check(placeHudPanel(20, 20, 100, 10, 2, 0).rows == 0, "insufficient viewport skips drawing");
    check(placeHudPanel(320, 240, 10000, 10, 200, 3).width == 296, "long text width bounded");
    check(placeHudPanel(320, 240, 100, std::numeric_limits<float>::quiet_NaN(), 2, 0).rows == 0, "non-finite line height rejected");
    auto const full = placeHudPanel(320, 100, 100, 10, 20, 0);
    check(placeHudPanel(320, 100, 100, 10, 20, 0, &full).rows == 0, "full first panel leaves no overlapping second panel");
    int measurements{};
    auto measure = [&](std::string const& value) {
        ++measurements;
        return std::count_if(value.begin(), value.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; });
    };
    check(fitHudText("石英石英石英石英", 7, measure) == "石英石英...", "text fitting preserves UTF-8 character boundaries");
    check(fitHudText("Stone", 5, measure) == "Stone" && fitHudText("Stone", 2, measure).empty(), "exact fit and too-narrow text areas");
    measurements = 0;
    check(fitHudText(std::string(10000, 'x'), 20, measure) == std::string(17, 'x') + "..."
        && measurements <= 18, "long labels use logarithmic font measurement calls");
    auto& uiState = structure::detail::StructureUiState::getInstance();
    auto const originalPositions = uiState.hudPlacements();
    auto const originalScales = uiState.hudScales();
    uiState.setHudScales(settings::kDefaultHudScales);
    auto const originalOpacities = uiState.hudBackgroundOpacities();
    uiState.setHudBackgroundOpacities(settings::kDefaultHudOpacities);
    uiState.setHudPlacements({});
    shutdownHudLayoutEditor();
    auto publishButtons = [] {
        auto const view = hudEditorSnapshot();
        publishHudEditorButtons({HudEditorBounds{0.2f, 0.92f, 0.15f, 0.06f},
            {0.4f, 0.92f, 0.15f, 0.06f}, {0.6f, 0.92f, 0.15f, 0.06f},
            view.selected >= 0 ? HudEditorBounds{0.1f, 0.05f, 0.25f, 0.06f} : HudEditorBounds{},
            view.selected >= 0 ? HudEditorBounds{0.4f, 0.05f, 0.25f, 0.06f} : HudEditorBounds{},
            view.selected >= 0 ? HudEditorBounds{0.75f, 0.05f, 0.2f, 0.06f} : HudEditorBounds{}},
            view.slider != HudEditorSlider::None ? HudEditorBounds{0.36f, 0.12f, 0.36f, 0.07f} : HudEditorBounds{},
            view.slider != HudEditorSlider::None ? HudEditorBounds{0.4f, 0.14f, 0.2f, 0.04f} : HudEditorBounds{});
    };
    auto beginEditor = [&] {
        processHudLayoutEditor(true, true, true);
        publishHudEditorBounds(0, {0.1f, 0.2f, 0.2f, 0.1f});
        publishHudEditorBounds(1, {0.7f, 0.6f, 0.15f, 0.2f});
        publishHudEditorBounds(2, {0.4f, 0.4f, 0.2f, 0.1f});
        publishButtons();
    };
    auto clickButton = [&](int index) {
        auto const box = hudEditorSnapshot().buttons[index];
        moveHudEditorPointer(box.x + box.width / 2, box.y + box.height / 2);
        pressHudEditorPointer(true); pressHudEditorPointer(false);
    };
    auto dragTo = [](int index, float x, float y) {
        auto const box = hudEditorSnapshot().bounds[index];
        moveHudEditorPointer(box.x + 0.01f, box.y + 0.01f);
        pressHudEditorPointer(true);
        moveHudEditorPointer(x + 0.01f, y + 0.01f);
        pressHudEditorPointer(false);
    };
    check(!processHudLayoutEditor(true, false, true) && !hudLayoutEditorActive(), "native editor requires a world");
    beginEditor();
    check(hudEditorSnapshot().selected == -1 && hudEditorSnapshot().buttons[3].width == 0,
        "single reset is absent until a HUD is selected");
    dragTo(0, -1, -1);
    check(effectiveHudPlacements()[0].x == 0 && effectiveHudPlacements()[0].y == 0
        && effectiveHudPlacements()[1].x == 0.7f, "native pointer drag reaches edge and freezes the other HUD");
    clickButton(1);
    check(!processHudLayoutEditor(false, true, true) && uiState.hudPlacements() == lholo::settings::HudPlacements{},
        "native cancel button discards the draft");
    beginEditor();
    dragTo(0, 2, 2);
    clickButton(2);
    check(processHudLayoutEditor(false, true, true) && uiState.hudPlacements()[0].custom
        && uiState.hudPlacements()[0].x == 0.8f && uiState.hudPlacements()[0].y == 0.9f,
        "native done button commits an in-bounds placement");
    beginEditor();
    auto const preservedMaterial = effectiveHudPlacements()[1];
    moveHudEditorPointer(0.11f, 0.21f); pressHudEditorPointer(true); pressHudEditorPointer(false);
    publishButtons(); clickButton(3); clickButton(2); processHudLayoutEditor(false, true, true);
    check(!uiState.hudPlacements()[0].custom && uiState.hudPlacements()[1] == preservedMaterial,
        "context button resets only the selected HUD");
    lholo::settings::HudPlacements const savedCustom{{{true, 0.3f, 0.4f}, {true, 0.6f, 0.2f}}};
    uiState.setHudPlacements(savedCustom);
    beginEditor(); clickButton(0);
    check(effectiveHudPlacements() == lholo::settings::HudPlacements{} && uiState.hudPlacements() == savedCustom,
        "reset all restores both presets without writing saved preferences");
    clickButton(1); processHudLayoutEditor(false, true, true);
    check(uiState.hudPlacements() == savedCustom, "cancel after reset all preserves saved positions");
    beginEditor();
    moveHudEditorPointer(0.11f, 0.21f); pressHudEditorPointer(true);
    moveHudEditorPointer(0.21f, 0.31f);
    auto const dragged = effectiveHudPlacements()[0];
    pressHudEditorPointer(true); // Native Down may repeat while held.
    releaseHudEditorPointer();
    moveHudEditorPointer(0.5f, 0.5f);
    check(effectiveHudPlacements()[0] == dragged, "focus loss ends dragging without saving or moving again");
    publishButtons();
    moveHudEditorPointer(0.95f, 0.1f); pressHudEditorPointer(true); pressHudEditorPointer(false);
    check(hudEditorSnapshot().selected == -1 && hudEditorSnapshot().buttons[3].width == 0,
        "blank click clears selection and its contextual button");
    clickButton(2); moveHudEditorPointer(0.5f, 0.5f); // finished request remains explicit
    processHudLayoutEditor(false, true, true);
    beginEditor(); clickButton(0);
    auto const resetBox = hudEditorSnapshot().buttons[2];
    moveHudEditorPointer(resetBox.x + 0.01f, resetBox.y + 0.01f); pressHudEditorPointer(true);
    moveHudEditorPointer(0.95f, 0.1f); pressHudEditorPointer(false);
    check(!processHudLayoutEditor(false, true, true) && hudLayoutEditorActive(), "releasing outside done does not commit");
    cancelHudLayoutEditor(); processHudLayoutEditor(false, true, true);
    auto const example = makeHudEditorContent(1, 2);
    check(example.projection.position == 1 && example.materials.position == 2
        && example.projection.lines.size() == 6 && example.materials.lines.size() == 4,
        "editor examples provide both panels without a loaded projection");
    check(example.materials.lines[1].iconId == "minecraft:stone"
        && example.projection.lines[2].text == "Progress: 128 / 337", "examples reuse localized HUD formatters and icon identities");
    beginEditor();
    check(!processHudLayoutEditor(false, true, false) && !hudLayoutEditorActive(), "removing the native scene cancels editing");
    beginEditor();
    moveHudEditorPointer(0.11f, 0.21f); pressHudEditorPointer(true); pressHudEditorPointer(false);
    publishButtons(); clickButton(4); publishButtons();
    check((hudEditorSnapshot().slider != HudEditorSlider::None), "opacity button opens slider popup");
    auto dragOpacity = [](float start, float end) {
        moveHudEditorPointer(start, 0.16f); pressHudEditorPointer(true);
        moveHudEditorPointer(end, 0.16f); pressHudEditorPointer(false);
    };
    auto const beforeSliderPositions = effectiveHudPlacements();
    dragOpacity(0.5f, 0.53f);
    check(effectiveHudBackgroundOpacities() == lholo::settings::HudPercentages{65, 60, 60}
        && uiState.hudBackgroundOpacities() == lholo::settings::HudPercentages{60, 60, 60}, "slider changes only selected draft at one-percent precision");
    check(effectiveHudPlacements() == beforeSliderPositions, "slider drag leaves HUD positions unchanged");
    moveHudEditorPointer(0.5f, 0.13f); pressHudEditorPointer(true); pressHudEditorPointer(false);
    check(hudEditorSnapshot().selected == 0 && (hudEditorSnapshot().slider != HudEditorSlider::None), "popup percentage preserves selection");
    clickButton(4); publishButtons();
    check(!(hudEditorSnapshot().slider != HudEditorSlider::None) && hudEditorSnapshot().sliderTrack.width == 0, "opacity button toggles slider closed");
    clickButton(2); processHudLayoutEditor(false, true, true);
    check(uiState.hudBackgroundOpacities() == lholo::settings::HudPercentages{65, 60, 60}, "done persists selected opacity");
    beginEditor();
    moveHudEditorPointer(0.71f, 0.61f); pressHudEditorPointer(true); pressHudEditorPointer(false);
    publishButtons(); clickButton(4); publishButtons();
    dragOpacity(0.5f, -0.1f);
    check(effectiveHudBackgroundOpacities() == lholo::settings::HudPercentages{65, 0, 60}, "slider drag stops at zero outside track");
    dragOpacity(0.5f, 1.1f);
    check(effectiveHudBackgroundOpacities() == lholo::settings::HudPercentages{65, 100, 60}, "slider drag stops at one hundred outside track");
    moveHudEditorPointer(0.5f, 0.16f); pressHudEditorPointer(true);
    releaseHudEditorPointer();
    auto const stoppedOpacity = effectiveHudBackgroundOpacities();
    moveHudEditorPointer(0.55f, 0.16f);
    check(effectiveHudBackgroundOpacities() == stoppedOpacity && !hudEditorSnapshot().sliderDragging,
        "focus loss stops slider drag");
    clickButton(3);
    check(effectiveHudBackgroundOpacities() == lholo::settings::HudPercentages{65, 60, 60}, "selected reset restores opacity while preserving other HUD");
    clickButton(0);
    check(effectiveHudBackgroundOpacities() == lholo::settings::HudPercentages{60, 60, 60}, "reset all restores both default opacities");
    clickButton(1); processHudLayoutEditor(false, true, true);
    check(uiState.hudBackgroundOpacities() == lholo::settings::HudPercentages{65, 60, 60}, "cancel discards opacity and reset changes");
    beginEditor();
    moveHudEditorPointer(0.11f, 0.21f); pressHudEditorPointer(true); pressHudEditorPointer(false);
    publishButtons(); clickButton(4); publishButtons();
    moveHudEditorPointer(0.71f, 0.61f); pressHudEditorPointer(true); pressHudEditorPointer(false);
    check(hudEditorSnapshot().selected == 1 && !(hudEditorSnapshot().slider != HudEditorSlider::None), "selecting another HUD closes old slider popup");
    cancelHudLayoutEditor(); processHudLayoutEditor(false, true, true);
    beginEditor();
    moveHudEditorPointer(0.11f, 0.21f); pressHudEditorPointer(true); pressHudEditorPointer(false);
    publishButtons(); clickButton(5); publishButtons();
    check(hudEditorSnapshot().slider == HudEditorSlider::Scale, "scale button opens the shared slider for scale");
    dragOpacity(0.5f, 0.55f);
    check(effectiveHudScales() == lholo::settings::HudPercentages{163, 100, 100}
        && uiState.hudScales() == lholo::settings::HudPercentages{100, 100, 100}, "scale preview changes only selected draft");
    clickButton(4); publishButtons();
    check(hudEditorSnapshot().slider == HudEditorSlider::Opacity, "opacity button switches the shared popup from scale");
    check(effectiveHudBackgroundOpacities() == lholo::settings::HudPercentages{65, 60, 60}, "scale leaves opacity unchanged");
    clickButton(5); publishButtons();
    dragOpacity(0.5f, -1);
    check(effectiveHudScales()[0] == 50, "scale slider clamps at fifty percent");
    dragOpacity(0.5f, 2);
    check(effectiveHudScales()[0] == 200, "scale slider clamps at two hundred percent");
    clickButton(2); processHudLayoutEditor(false, true, true);
    check(uiState.hudScales() == lholo::settings::HudPercentages{200, 100, 100}, "done saves independent scale");
    beginEditor();
    moveHudEditorPointer(0.11f, 0.21f); pressHudEditorPointer(true); pressHudEditorPointer(false);
    publishButtons(); clickButton(3);
    check(effectiveHudScales()[0] == 100 && effectiveHudBackgroundOpacities()[0] == 60
        && !effectiveHudPlacements()[0].custom, "selected reset restores position opacity and scale");
    clickButton(1); processHudLayoutEditor(false, true, true);
    check(uiState.hudScales()[0] == 200, "cancel discards scale reset");
    beginEditor(); clickButton(0);
    check(effectiveHudScales() == lholo::settings::HudPercentages{100, 100, 100}, "reset all restores both default scales");
    cancelHudLayoutEditor(); processHudLayoutEditor(false, true, true);
    beginEditor();
    auto const precedingPanels = effectiveHudPlacements();
    moveHudEditorPointer(0.41f, 0.41f); pressHudEditorPointer(true);
    moveHudEditorPointer(0.51f, 0.51f); pressHudEditorPointer(false);
    check(hudEditorSnapshot().selected == 2 && effectiveHudPlacements()[2].custom
        && effectiveHudPlacements()[0] == precedingPanels[0] && effectiveHudPlacements()[1] == precedingPanels[1],
        "write HUD drags independently without freezing or moving other panels");
    publishButtons(); clickButton(3);
    check(!effectiveHudPlacements()[2].custom && effectiveHudScales()[2] == 100
        && effectiveHudBackgroundOpacities()[2] == 60, "write panel shares selected reset defaults");
    clickButton(2); processHudLayoutEditor(false, true, true);
    shutdownHudLayoutEditor();
    uiState.setHudScales(originalScales);
    uiState.setHudBackgroundOpacities(originalOpacities);
    uiState.setHudPlacements(originalPositions);
    i18n::setLanguage(oldLanguage);
    std::printf("NativeHudTests: %d checks, %d failures\n", checks, failures);
    return failures;
}
