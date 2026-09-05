// LHolo logic tests: pure projection rules and progress publication.
// Run with: xmake r LHoloLogicTests

#include <cstdio>
#include <fstream>
#include <sstream>

#include "block/BlockPlacementRules.h"
#include "place/PlacementState.h"
#include "projection/core/ProjectionRules.h"
#include "projection/runtime/ProjectionProgress.h"
#include "settings/SettingsStore.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"
#include "structure/java_to_bedrock/JavaBlockEntityToBedrock.h"
#include "ui/HotkeyFormat.h"

#include <Windows.h>

namespace {

int gChecks = 0;
int gFailures = 0;

#define LHOLO_CHECK(cond)                                     \
    do {                                                      \
        ++gChecks;                                            \
        if (!(cond)) {                                        \
            ++gFailures;                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n",          \
                __FILE__, __LINE__, #cond);                   \
        }                                                     \
    } while (false)

using namespace lholo::projection::detail;
using lholo::structure::LoadedStructure;

bool expectBlockPos(BlockPos const& pos, int x, int y, int z) {
    return pos.x == x && pos.y == y && pos.z == z;
}

void testLayoutRules() {
    LHOLO_CHECK(isVanillaSaplingType("minecraft:oak_sapling"));
    LHOLO_CHECK(isVanillaSaplingType("minecraft:dark_oak_sapling"));
    LHOLO_CHECK(isVanillaSaplingType("minecraft:bamboo_sapling"));
    LHOLO_CHECK(!isVanillaSaplingType("minecraft:oak_log"));
    LHOLO_CHECK(!isVanillaSaplingType("minecraft:flower_pot"));
    LHOLO_CHECK(!isVanillaSaplingType("example:oak_sapling"));

    LHOLO_CHECK(getProjectionMirror(0) == Mirror::None);
    LHOLO_CHECK(getProjectionMirror(1) == Mirror::Z);
    LHOLO_CHECK(getProjectionMirror(2) == Mirror::X);
    LHOLO_CHECK(getProjectionMirror(9) == Mirror::None);

    LHOLO_CHECK(getProjectionRotation(0) == Rotation::None);
    LHOLO_CHECK(getProjectionRotation(1) == Rotation::Clockwise90);
    LHOLO_CHECK(getProjectionRotation(2) == Rotation::Clockwise180);
    LHOLO_CHECK(getProjectionRotation(3) == Rotation::CounterClockwise90);
    LHOLO_CHECK(getProjectionRotation(4) == Rotation::None);
    LHOLO_CHECK(getProjectionRotation(5) == Rotation::Clockwise90);
    LHOLO_CHECK(getProjectionRotation(-1) == Rotation::CounterClockwise90);

    LoadedStructure loaded;
    loaded.sizeX = 4;
    loaded.sizeY = 3;
    loaded.sizeZ = 5;
    LoadedStructure::RenderBlock const entry{1, 2, 3, nullptr, nullptr, nullptr};

    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 0, 0), 1, 2, 3));
    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 1, 0), 2, 2, 3));
    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 2, 0), 1, 2, 1));
    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 0, 1), 1, 2, 1));
    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 0, 2), 2, 2, 1));
    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 0, 3), 3, 2, 2));
    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 1, 1), 1, 2, 2));

    for (int mirror = 0; mirror <= 2; ++mirror) {
        for (int rotation = 0; rotation < 4; ++rotation) {
            for (int x = 0; x < loaded.sizeX; ++x) {
                for (int z = 0; z < loaded.sizeZ; ++z) {
                    BlockPos const local{x, 1, z};
                    auto const transformed = transformStructurePosition(
                        local, loaded, mirror, rotation
                    );
                    auto const restored = inverseTransformStructurePosition(
                        transformed, loaded, mirror, rotation
                    );
                    LHOLO_CHECK(expectBlockPos(restored, x, 1, z));
                }
            }
        }
    }

    loaded.regions = {
        {0, 0, 0, 2, 3, 2},
        {3, 0, 3, 1, 1, 2},
    };
    LHOLO_CHECK(isStructureCellCovered(loaded, BlockPos{1, 2, 1}));
    LHOLO_CHECK(isStructureCellCovered(loaded, BlockPos{3, 0, 4}));
    LHOLO_CHECK(!isStructureCellCovered(loaded, BlockPos{2, 0, 2}));
    LHOLO_CHECK(!isStructureCellCovered(loaded, BlockPos{4, 0, 4}));

    LHOLO_CHECK(isLayerVisible(3, 0, 0));
    LHOLO_CHECK(!isLayerVisible(3, 1, 2));
    LHOLO_CHECK(isLayerVisible(2, 1, 2));
    LHOLO_CHECK(isLayerVisible(2, 2, 3));
    LHOLO_CHECK(!isLayerVisible(4, 2, 3));
    LHOLO_CHECK(isLayerVisible(4, 3, 3));
    LHOLO_CHECK(!isLayerVisible(2, 3, 3));
    LHOLO_CHECK(isLayerVisible(0, 0, 0, 2, -1, 2));
    LHOLO_CHECK(isLayerVisible(0, 1, 2, 2, -1, 2));
    LHOLO_CHECK(!isLayerVisible(0, 1, 1, 2, -1, 2));
    LHOLO_CHECK(isLayerVisible(0, 2, 2, 1, 3, 2));
    LHOLO_CHECK(!isLayerVisible(0, 2, 2, 3, 4, 2));
    LHOLO_CHECK(isLayerVisible(0, 3, 3, -1, 3, 2));
    LHOLO_CHECK(!isLayerVisible(0, 3, 2, 1, 1, 2));

    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerOpaque) == RenderBucket::Opaque);
    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerSeasonsOpaque) == RenderBucket::Opaque);
    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerBlend) == RenderBucket::Blend);
    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerBlendToOpaque) == RenderBucket::Blend);
    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerAlphatestSingleSide) == RenderBucket::AlphaOneSided);
    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerAlphatest) == RenderBucket::Alpha);
    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerDoubleSided) == RenderBucket::Alpha);
}

void testProgress() {
    initializePublishedBuildProgress(100);
    auto progress = getPublishedBuildProgress();
    LHOLO_CHECK(progress.total == 100);
    LHOLO_CHECK(progress.visibleTotal == 100);
    LHOLO_CHECK(progress.placed == 0);

    publishPlacedProgress(120);
    progress = getPublishedBuildProgress();
    LHOLO_CHECK(progress.placed == 100);

    publishVisibleProgress(60, 80);
    publishErrorProgress(130, 5, 140);
    progress = getPublishedBuildProgress();
    LHOLO_CHECK(progress.visiblePlaced == 60);
    LHOLO_CHECK(progress.visibleTotal == 80);
    LHOLO_CHECK(progress.wrongType == 100);
    LHOLO_CHECK(progress.wrongState == 5);
    LHOLO_CHECK(progress.extra == 140);

    resetPublishedBuildProgress();
    progress = getPublishedBuildProgress();
    LHOLO_CHECK(progress.total == 0);
    LHOLO_CHECK(progress.placed == 0);
    LHOLO_CHECK(progress.visibleTotal == 0);

    initializePublishedBuildProgress(50);
    publishVisibleProgress(40, 30);
    progress = getPublishedBuildProgress();
    LHOLO_CHECK(progress.visiblePlaced == 30);
    LHOLO_CHECK(progress.total == 50);

    resetPublishedBuildProgressCounts();
    progress = getPublishedBuildProgress();
    LHOLO_CHECK(progress.total == 50);
    LHOLO_CHECK(progress.placed == 0);
    LHOLO_CHECK(progress.wrongType == 0);
    LHOLO_CHECK(progress.wrongState == 0);
    LHOLO_CHECK(progress.extra == 0);
}

void testSettingsStore() {
    auto const path = std::filesystem::temp_directory_path() / "lholo_settings_test.json";
    std::error_code error;
    std::filesystem::remove(path, error);

    lholo::settings::Settings settings;
    settings.uiScale = 1.25f;
    settings.guiHotkey = 'L';
    settings.guiHotkeyModifiers = 1;
    settings.hudShowProjectedBlockName = false;
    settings.hudShowExtraBlocks = false;
    settings.autoPlacementBreakCooldownSeconds = 27;
    settings.correctionSeeThrough = true;
    settings.materialHudEnabled = true;
    settings.materialHudPosition = 3;
    settings.moveHotkeys[4] = 0x57; // W
    settings.hasSavedProjection = true;
    settings.savedAnchorX = 12;
    settings.savedAnchorZ = -34;
    lholo::settings::saveSettingsFile(path, settings);
    {
        std::ifstream saved(path);
        std::ostringstream contents;
        contents << saved.rdbuf();
        LHOLO_CHECK(contents.str().find("\"version\": 10") != std::string::npos);
        LHOLO_CHECK(contents.str().find("toggleManualHotkey") == std::string::npos);
        LHOLO_CHECK(contents.str().find("toggleEasyHotkey") == std::string::npos);
        LHOLO_CHECK(contents.str().find("toggleRangeHotkey") == std::string::npos);
    }

    lholo::settings::Settings loaded;
    LHOLO_CHECK(lholo::settings::loadSettingsFile(path, loaded));
    LHOLO_CHECK(loaded.uiScale == 1.25f);
    LHOLO_CHECK(loaded.guiHotkey == 'L');
    LHOLO_CHECK(loaded.guiHotkeyModifiers == 1);
    LHOLO_CHECK(!loaded.hudShowProjectedBlockName);
    LHOLO_CHECK(!loaded.hudShowExtraBlocks);
    LHOLO_CHECK(loaded.autoPlacementBreakCooldownSeconds == 27);
    LHOLO_CHECK(loaded.correctionSeeThrough);
    LHOLO_CHECK(loaded.materialHudEnabled);
    LHOLO_CHECK(loaded.materialHudPosition == 3);
    LHOLO_CHECK(loaded.moveHotkeys[4] == 0x57);
    LHOLO_CHECK(loaded.hasSavedProjection);
    LHOLO_CHECK(loaded.savedAnchorX == 12);
    LHOLO_CHECK(loaded.savedAnchorZ == -34);

    // Existing configs keep their preference when the old, narrower
    // block-entity label migrates to the projected-block label.
    {
        std::ofstream legacy(path, std::ios::trunc);
        legacy << R"({"hudShowBlockEntity":false,"toggleManualHotkey":82,"toggleEasyHotkey":70,"toggleRangeHotkey":89})";
    }
    lholo::settings::Settings migrated;
    LHOLO_CHECK(lholo::settings::loadSettingsFile(path, migrated));
    LHOLO_CHECK(!migrated.hudShowProjectedBlockName);
    LHOLO_CHECK(migrated.hudShowExtraBlocks);
    LHOLO_CHECK(migrated.autoPlacementBreakCooldownSeconds == 10);
    LHOLO_CHECK(!migrated.correctionSeeThrough);
    LHOLO_CHECK(!migrated.materialHudEnabled);
    LHOLO_CHECK(migrated.materialHudPosition == 3);

    lholo::settings::Settings missing;
    std::filesystem::remove(path, error);
    LHOLO_CHECK(!lholo::settings::loadSettingsFile(path, missing));
    std::filesystem::remove(path, error);
}

void testStructureSession() {
    using lholo::structure::detail::SavedProjectionSnapshot;
    using lholo::structure::detail::StructureSession;

    auto& session = StructureSession::getInstance();
    session.clearLoaded("尚未加载结构文件");
    session.resetTransform();
    session.setLastPath("initial.mcstructure");
    session.setSavedProjection(SavedProjectionSnapshot{});

    auto snapshot = session.snapshot();
    LHOLO_CHECK(!snapshot.loaded);
    LHOLO_CHECK(snapshot.status == "尚未加载结构文件");
    LHOLO_CHECK(snapshot.lastPath == "initial.mcstructure");
    LHOLO_CHECK(snapshot.transform.rotation == 0);
    LHOLO_CHECK(!snapshot.saved.available);

    auto loaded = std::make_shared<LoadedStructure>();
    loaded->sizeX = 7;
    loaded->sizeY = 5;
    loaded->sizeZ = 3;
    session.replaceLoaded(loaded, "active.mcstructure", "loaded");
    LHOLO_CHECK(session.setRotation(2));
    LHOLO_CHECK(!session.setRotation(2));
    LHOLO_CHECK(session.setMirror(1));
    LHOLO_CHECK(session.setOffsetX(12));
    LHOLO_CHECK(session.setOffsetY(-4));
    LHOLO_CHECK(session.setLayerDisplayMode(1));
    LHOLO_CHECK(session.setDisplayLayer(4));
    LHOLO_CHECK(session.setLayerAxis(1));

    snapshot = session.snapshot();
    LHOLO_CHECK(snapshot.loaded == loaded);
    LHOLO_CHECK(snapshot.maxLayerY == 4);
    LHOLO_CHECK(snapshot.maxLayerX == 6);
    LHOLO_CHECK(snapshot.transform.offsetX == 12);
    LHOLO_CHECK(snapshot.transform.offsetY == -4);

    session.recordProjectionAnchor(10, 20, 30);
    auto const saved = session.savedProjection();
    LHOLO_CHECK(saved.available);
    LHOLO_CHECK(saved.anchorX == 10);
    LHOLO_CHECK(saved.anchorY == 20);
    LHOLO_CHECK(saved.anchorZ == 30);
    LHOLO_CHECK(saved.transform.rotation == 2);
    LHOLO_CHECK(saved.transform.mirror == 1);
    LHOLO_CHECK(saved.structurePath == "active.mcstructure");

    auto layered = std::make_shared<LoadedStructure>();
    layered->sizeX = 3;
    layered->sizeY = 8;
    layered->sizeZ = 4;
    session.replaceLoaded(layered, "layered.mcstructure", "layered");
    session.setLayerDisplayMode(2);
    session.setDisplayLayer(7);
    session.setLayerAxis(0);
    session.recordProjectionAnchor(40, 50, 60);
    session.clearLoaded("closed");
    session.setDisplayLayer(0); // Empty-menu clamping must not alter the saved layer.
    auto const layeredSaved = session.savedProjection();
    LHOLO_CHECK(layeredSaved.transform.layerDisplayMode == 2);
    LHOLO_CHECK(layeredSaved.transform.displayLayer == 7);
    LHOLO_CHECK(layeredSaved.transform.layerAxis == 0);

    session.setLayerDisplayMode(layeredSaved.transform.layerDisplayMode);
    session.setDisplayLayer(layeredSaved.transform.displayLayer);
    session.setLayerAxis(layeredSaved.transform.layerAxis);
    session.replaceLoaded(layered, layeredSaved.structurePath, "restored");
    snapshot = session.snapshot();
    LHOLO_CHECK(snapshot.loaded == layered);
    LHOLO_CHECK(snapshot.maxLayerY == 7);
    LHOLO_CHECK(snapshot.transform.displayLayer == 7);

    session.clearLoaded("closed");
}

void testPlacementState() {
    using lholo::place::detail::FailedPlanKey;
    using lholo::place::detail::PlacementState;

    auto& state = PlacementState::getInstance();
    state.setEnabled(true);
    state.setRangeEnabled(true);
    state.setManualMode(true);
    state.setRadius(3);
    state.setAutoPlacementBreakCooldownSeconds(12);
    state.setManualPressAt(100);
    state.setLastManualPlaceAt(80);
    state.setManualPlaceRequested(true);
    state.setManualHeld(true);
    state.setNextPlaceAt(140);
    state.setNextSwapAt(150);

    LHOLO_CHECK(state.enabled());
    LHOLO_CHECK(state.rangeEnabled());
    LHOLO_CHECK(state.manualMode());
    LHOLO_CHECK(state.radius() == 3);
    LHOLO_CHECK(state.autoPlacementBreakCooldownSeconds() == 12);
    LHOLO_CHECK(state.manualPressAt() == 100);
    LHOLO_CHECK(state.lastManualPlaceAt() == 80);
    LHOLO_CHECK(state.manualPlaceRequested());
    LHOLO_CHECK(state.manualHeld());
    LHOLO_CHECK(state.nextPlaceAt() == 140);
    LHOLO_CHECK(state.nextSwapAt() == 150);

    constexpr std::int64_t recentCell = 0x123456789LL;
    state.recordRecentPlacement(recentCell, 100, 150);
    LHOLO_CHECK(state.recentPlacementActive(recentCell, 149));
    LHOLO_CHECK(!state.recentPlacementActive(recentCell, 150));

    constexpr std::int64_t suppressedCell = 0x23456789ALL;
    LHOLO_CHECK(!state.autoPlacementSuppressionsActive(100));
    state.suppressAutoPlacement(suppressedCell, 200);
    LHOLO_CHECK(state.autoPlacementSuppressionsActive(100));
    LHOLO_CHECK(state.autoPlacementSuppressed(suppressedCell, 199));
    LHOLO_CHECK(!state.autoPlacementSuppressionsActive(200));
    LHOLO_CHECK(!state.autoPlacementSuppressed(suppressedCell, 200));

    // Extending the current earliest entry may leave the cheap expiry hint at
    // its old value, but the first boundary refresh must retain the live entry.
    state.suppressAutoPlacement(suppressedCell, 300);
    state.suppressAutoPlacement(suppressedCell, 350);
    LHOLO_CHECK(state.autoPlacementSuppressionsActive(300));
    LHOLO_CHECK(state.autoPlacementSuppressed(suppressedCell, 349));
    LHOLO_CHECK(!state.autoPlacementSuppressionsActive(350));

    FailedPlanKey const failedKey{recentCell, 42, 7, 1, 2, 3, 4, 5, 6};
    state.cacheFailedPlan(failedKey, 200, 250);
    LHOLO_CHECK(state.failedPlanCached(failedKey, 249));
    LHOLO_CHECK(!state.failedPlanCached(failedKey, 250));

    state.setAimedProjectedBlockName("Test projected block");
    LHOLO_CHECK(state.aimedProjectedBlockName() == "Test projected block");

    state.recordRecentPlacement(recentCell, 400, 500);
    state.suppressAutoPlacement(suppressedCell, 500);
    state.cacheFailedPlan(failedKey, 400, 500);
    state.resetDimensionSession();
    LHOLO_CHECK(state.enabled());
    LHOLO_CHECK(state.rangeEnabled());
    LHOLO_CHECK(state.manualMode());
    LHOLO_CHECK(!state.manualHeld());
    LHOLO_CHECK(!state.manualPlaceRequested());
    LHOLO_CHECK(state.manualPressAt() == 0);
    LHOLO_CHECK(state.lastManualPlaceAt() == 0);
    LHOLO_CHECK(state.nextPlaceAt() == 0);
    LHOLO_CHECK(state.nextSwapAt() == 0);
    LHOLO_CHECK(!state.recentPlacementActive(recentCell, 400));
    LHOLO_CHECK(!state.autoPlacementSuppressionsActive(400));
    LHOLO_CHECK(!state.autoPlacementSuppressed(suppressedCell, 400));
    LHOLO_CHECK(!state.failedPlanCached(failedKey, 400));
    LHOLO_CHECK(state.aimedProjectedBlockName().empty());
    LHOLO_CHECK(state.radius() == 3);
    LHOLO_CHECK(state.autoPlacementBreakCooldownSeconds() == 12);

    state.setManualHeld(true);
    state.setManualPlaceRequested(true);
    state.setAimedProjectedBlockName("World projected block");
    state.suppressAutoPlacement(suppressedCell, 500);
    state.resetWorldSession();
    LHOLO_CHECK(!state.enabled());
    LHOLO_CHECK(!state.rangeEnabled());
    LHOLO_CHECK(!state.manualMode());
    LHOLO_CHECK(!state.manualHeld());
    LHOLO_CHECK(!state.manualPlaceRequested());
    LHOLO_CHECK(state.manualPressAt() == 0);
    LHOLO_CHECK(state.lastManualPlaceAt() == 0);
    LHOLO_CHECK(state.nextPlaceAt() == 0);
    LHOLO_CHECK(state.nextSwapAt() == 0);
    LHOLO_CHECK(!state.recentPlacementActive(recentCell, 0));
    LHOLO_CHECK(!state.autoPlacementSuppressionsActive(0));
    LHOLO_CHECK(!state.autoPlacementSuppressed(suppressedCell, 0));
    LHOLO_CHECK(!state.failedPlanCached(failedKey, 0));
    LHOLO_CHECK(state.aimedProjectedBlockName().empty());
    // User configuration survives a world transition.
    LHOLO_CHECK(state.radius() == 3);
    LHOLO_CHECK(state.autoPlacementBreakCooldownSeconds() == 12);

    state.setRadius(4);
    state.setAutoPlacementBreakCooldownSeconds(10);
}

void testStructureUiState() {
    using lholo::structure::detail::HudStateSnapshot;
    using lholo::structure::detail::StructureUiState;

    auto& state = StructureUiState::getInstance();
    state.resetHotkeys();
    state.resetHotkeyState();
    state.stopHotkeyCapture();
    (void)state.consumePendingHotkeyActions();
    state.clearMaterials();

    auto hud = state.hud();
    hud.enabled = false;
    hud.showLayer = false;
    hud.showProjectedBlockName = false;
    hud.position = 3;
    hud.uiScale = 1.5f;
    LHOLO_CHECK(state.applyHud(hud));
    LHOLO_CHECK(!state.applyHud(hud));
    auto const appliedHud = state.hud();
    LHOLO_CHECK(!appliedHud.enabled);
    LHOLO_CHECK(!appliedHud.showLayer);
    LHOLO_CHECK(!appliedHud.showProjectedBlockName);
    LHOLO_CHECK(appliedHud.position == 3);
    LHOLO_CHECK(appliedHud.uiScale == 1.5f);

    state.resetHotkeys();
    auto const guiSlot = lholo::input::hotkeyIndex(lholo::input::HotkeyId::Gui);
    auto const moveXMinusSlot = lholo::input::hotkeyIndex(lholo::input::HotkeyId::MoveXMinus);
    auto const moveXPlusSlot = lholo::input::hotkeyIndex(lholo::input::HotkeyId::MoveXPlus);
    auto const layerIncreaseSlot = lholo::input::hotkeyIndex(lholo::input::HotkeyId::LayerIncrease);
    auto const loadProjectionSlot = lholo::input::hotkeyIndex(lholo::input::HotkeyId::LoadProjection);
    auto const closeProjectionSlot = lholo::input::hotkeyIndex(lholo::input::HotkeyId::CloseProjection);
    LHOLO_CHECK(state.hotkey(guiSlot).key == 'M');
    LHOLO_CHECK(state.hotkey(guiSlot).modifiers == lholo::ui::kHotkeyModifierAlt);
    LHOLO_CHECK(state.hotkey(moveXMinusSlot).key == VK_LEFT);
    LHOLO_CHECK(state.hotkey(layerIncreaseSlot).key == VK_UP);
    LHOLO_CHECK(state.hotkey(loadProjectionSlot).key == 0);
    LHOLO_CHECK(state.hotkey(closeProjectionSlot).key == 0);

    state.beginHotkeyCapture(moveXMinusSlot);
    LHOLO_CHECK(state.capturingHotkey() == moveXMinusSlot);
    state.setHotkey(moveXPlusSlot, 'K', lholo::ui::kHotkeyModifierControl);
    state.bindCapturedHotkey(moveXMinusSlot, 'K', lholo::ui::kHotkeyModifierControl);
    LHOLO_CHECK(state.hotkey(moveXMinusSlot).key == 'K');
    LHOLO_CHECK(state.hotkey(moveXPlusSlot).key == 0);
    LHOLO_CHECK(!state.capturingHotkey());

    state.setControlHeld(true);
    state.setShiftHeld(true);
    LHOLO_CHECK(
        state.currentHotkeyModifiers()
        == (lholo::ui::kHotkeyModifierControl | lholo::ui::kHotkeyModifierShift)
    );
    state.setControlHeld(false);
    state.setShiftHeld(false);

    state.resetHotkeys();
    LHOLO_CHECK(state.tryPressHotkey(guiSlot));
    LHOLO_CHECK(!state.tryPressHotkey(guiSlot));
    LHOLO_CHECK(state.releaseHotkeysForKey('M', 100));
    LHOLO_CHECK(state.releaseHotkeysForKey('M', 150));
    LHOLO_CHECK(!state.releaseHotkeysForKey('M', 201));

    state.queueMove(0);
    state.queueMove(4);
    state.queueLayerDelta(-1);
    state.queueLoadProjection();
    state.queueCloseProjection();
    state.requestSettingsSave();
    auto const pending = state.consumePendingHotkeyActions();
    LHOLO_CHECK(pending.offsetX == -1);
    LHOLO_CHECK(pending.offsetY == 1);
    LHOLO_CHECK(pending.offsetZ == 0);
    LHOLO_CHECK(pending.layerDelta == -1);
    LHOLO_CHECK(pending.loadProjection);
    LHOLO_CHECK(pending.closeProjection);
    LHOLO_CHECK(pending.settingsSave);

    state.clearMaterials();
    LHOLO_CHECK(!state.materialListReady());
    state.requestMaterialList();
    LHOLO_CHECK(state.consumeMaterialListRequest());
    LHOLO_CHECK(!state.consumeMaterialListRequest());
    state.replaceMaterialRequirements({{"Stone", "minecraft:stone", "minecraft:stone", 12}});
    LHOLO_CHECK(state.materialListReady());
    // Reopening a completed list must not queue another full structure scan.
    state.requestMaterialList();
    LHOLO_CHECK(!state.consumeMaterialListRequest());
    auto const materials = state.materialRequirements();
    LHOLO_CHECK(materials.size() == 1);
    LHOLO_CHECK(materials[0].typeName == "minecraft:stone");
    LHOLO_CHECK(materials[0].itemId == "minecraft:stone");
    LHOLO_CHECK(materials[0].count == 12);

    auto hudMaterials = state.materialHudSnapshot();
    LHOLO_CHECK(!hudMaterials.ready);
    state.replaceMaterialHudSnapshot(
        {{"Glass", "minecraft:glass", "minecraft:glass", 5}},
        {2}
    );
    hudMaterials = state.materialHudSnapshot();
    LHOLO_CHECK(hudMaterials.ready);
    LHOLO_CHECK(hudMaterials.requirements.size() == 1);
    LHOLO_CHECK(hudMaterials.requirements[0].count == 5);
    LHOLO_CHECK(hudMaterials.available.size() == 1);
    LHOLO_CHECK(hudMaterials.available[0] == 2);
    // Updating the current-layer HUD must not replace the whole-structure list.
    LHOLO_CHECK(state.materialRequirements()[0].typeName == "minecraft:stone");
    state.clearMaterialHud();
    LHOLO_CHECK(!state.materialHudSnapshot().ready);

    state.setExperimentalConsentGiven(true);
    state.setMaterialHudEnabled(true);
    state.setMaterialHudPosition(3);
    state.setActionHint("test", 1234);
    LHOLO_CHECK(state.experimentalConsentGiven());
    LHOLO_CHECK(state.materialHudEnabled());
    LHOLO_CHECK(state.materialHudPosition() == 3);
    auto const hint = state.actionHint();
    LHOLO_CHECK(hint.text == "test");
    LHOLO_CHECK(hint.expiry == 1234);

    state.setGuiVisible(false);
    LHOLO_CHECK(state.toggleGuiVisible());
    LHOLO_CHECK(state.guiVisible());
    state.setOpeningInputBlockFrames(1);
    LHOLO_CHECK(state.openingInputBlocked());
    state.consumeOpeningInputBlockFrame();
    LHOLO_CHECK(!state.openingInputBlocked());

    state.setGuiVisible(true);
    state.setOpeningInputBlockFrames(3);
    state.setBlockGameInputUntil(900);
    state.beginHotkeyCapture(moveXMinusSlot);
    state.setControlHeld(true);
    state.queueMove(1);
    state.queueLayerDelta(1);
    state.queueLoadProjection();
    state.queueCloseProjection();
    state.requestSettingsSave();
    state.replaceMaterialRequirements({{"Stone", "minecraft:stone", "minecraft:stone", 4}});
    state.replaceMaterialHudSnapshot(
        {{"Glass", "minecraft:glass", "minecraft:glass", 2}},
        {1}
    );
    state.setActionHint("world hint", 9999);
    state.resetWorldSession();
    LHOLO_CHECK(!state.guiVisible());
    LHOLO_CHECK(!state.openingInputBlocked());
    LHOLO_CHECK(state.blockGameInputUntil() == 0);
    LHOLO_CHECK(!state.capturingHotkey());
    LHOLO_CHECK(state.currentHotkeyModifiers() == 0);
    LHOLO_CHECK(!state.materialListReady());
    LHOLO_CHECK(!state.materialHudSnapshot().ready);
    LHOLO_CHECK(state.actionHint().text.empty());
    LHOLO_CHECK(state.actionHint().expiry == 0);
    auto const afterWorldExit = state.consumePendingHotkeyActions();
    LHOLO_CHECK(afterWorldExit.offsetX == 0);
    LHOLO_CHECK(afterWorldExit.offsetY == 0);
    LHOLO_CHECK(afterWorldExit.offsetZ == 0);
    LHOLO_CHECK(afterWorldExit.layerDelta == 0);
    LHOLO_CHECK(!afterWorldExit.loadProjection);
    LHOLO_CHECK(!afterWorldExit.closeProjection);
    // A pending settings write is not world-owned and must still complete.
    LHOLO_CHECK(afterWorldExit.settingsSave);
    LHOLO_CHECK(state.experimentalConsentGiven());
    LHOLO_CHECK(state.materialHudEnabled());
    LHOLO_CHECK(state.materialHudPosition() == 3);

    state.setGuiVisible(false);
    state.resetHotkeys();
    state.resetHotkeyState();
    state.clearMaterials();
    LHOLO_CHECK(!state.materialListReady());
    state.setExperimentalConsentGiven(false);
    state.setMaterialHudEnabled(false);
    state.setMaterialHudPosition(3);
    state.setActionHint({}, 0);
    state.applyHud(HudStateSnapshot{});
}

void testHotkeyFormat() {
    LHOLO_CHECK(lholo::ui::isModifierKey(VK_CONTROL));
    LHOLO_CHECK(lholo::ui::isModifierKey(VK_MENU));
    LHOLO_CHECK(lholo::ui::isModifierKey(VK_LWIN));
    LHOLO_CHECK(!lholo::ui::isModifierKey('A'));
    LHOLO_CHECK(lholo::ui::hotkeyName(0) == "未设置");
    LHOLO_CHECK(lholo::ui::hotkeyName(VK_MBUTTON) == "鼠标中键");
    LHOLO_CHECK(lholo::ui::hotkeyName(VK_XBUTTON1) == "鼠标侧键1");
    LHOLO_CHECK(lholo::ui::hotkeyName(VK_XBUTTON2) == "鼠标侧键2");
    LHOLO_CHECK(lholo::ui::hotkeyChordName(0, 0) == "未设置");
    auto const chord = lholo::ui::hotkeyChordName(lholo::ui::kHotkeyModifierControl, 'M');
    LHOLO_CHECK(chord.rfind("Ctrl + ", 0) == 0);
    LHOLO_CHECK(chord.size() > 7);
}

void testBlockPlacementRules() {
    using lholo::block::placeableBaseName;
    LHOLO_CHECK(placeableBaseName("minecraft:lit_redstone_lamp") == "minecraft:redstone_lamp");
    LHOLO_CHECK(placeableBaseName("minecraft:powered_repeater") == "minecraft:unpowered_repeater");
    LHOLO_CHECK(placeableBaseName("minecraft:stone") == "minecraft:stone");
}

void testJavaTextComponents() {
    using lholo::structure::detail::javaTextComponentToPlainText;
    LHOLO_CHECK(javaTextComponentToPlainText(R"("Launch")") == "Launch");
    LHOLO_CHECK(javaTextComponentToPlainText(R"({"text":"X","extra":[{"text":" count"}]})") == "X count");
    LHOLO_CHECK(javaTextComponentToPlainText(R"(["A",{"text":"B"}])") == "AB");
    LHOLO_CHECK(javaTextComponentToPlainText(R"({"translate":"block.minecraft.oak_sign"})")
                == "block.minecraft.oak_sign");
    LHOLO_CHECK(javaTextComponentToPlainText("not json") == "not json");
}

} // namespace

int main() {
    testLayoutRules();
    testProgress();
    testSettingsStore();
    testStructureSession();
    testPlacementState();
    testStructureUiState();
    testHotkeyFormat();
    testBlockPlacementRules();
    testJavaTextComponents();
    std::printf("LHoloLogicTests: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
