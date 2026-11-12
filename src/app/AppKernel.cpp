// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "app/AppKernel.h"
#include "app/RenderSettingsActions.h"
#include "app/SettingsActions.h"
#include "app/TaskActions.h"
#include "app/StructurePreviewActions.h"
#include "app/StructureWriteActions.h"
#include "structure/write/local/LocalWorldWriter.h"
#include "ui/preview/StructurePreviewScene.h"

#include "i18n/LanguageStore.h"
#include "i18n/Translator.h"
#include "ui/hud/NativeHudRenderer.h"
#include "ui/hud/editor/HudEditorScene.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
#include "mc/client/options/IOptionRegistry.h"
#include "input/ProjectionInputGuard.h"
#include "input/MenuOpenRequest.h"
#include "oreui/OreUiBridge.h"
#include "oreui/OreUiScreen.h"
#include "oreui/OreUiActionHint.h"
#include "input/WindowHotkeys.h"
#include "place/PlaceHelper.h"
#include "plugin/LHolo.h"
#include "projection/ProjectionController.h"
#include "projection/mesh/ProjectionMeshWorker.h"
#include "structure/capture/StructureCapture.h"
#include "structure/MaterialTracker.h"
#include "structure/StructureLoader.h"
#include "structure/StructureFileLoader.h"
#include "structure/write/command/CommandWorldWriter.h"

#include "ll/api/mod/NativeMod.h"
#include "ll/api/memory/Hook.h"

#include <atomic>

namespace lholo::app {
namespace {
std::atomic_bool controlFramesEnabled{};

// World-facing actions belong to the client logic thread. Present may run
// concurrently with this update and must never drain application queues.
LL_TYPE_INSTANCE_HOOK(AppClientUpdate, ll::memory::HookPriority::Normal,
    ClientInstance, &ClientInstance::$update, bool, bool isInitFinished, bool allowGameUpdate) {
    auto const result = origin(isInitFinished, allowGameUpdate);
    if (controlFramesEnabled.load(std::memory_order_acquire)) {
        auto client = ll::service::getClientInstance();
        if (client && &client.get() == this) AppKernel::getInstance().processControlFrame();
    }
    return result;
}
} // namespace

AppKernel& AppKernel::getInstance() {
    static AppKernel instance;
    return instance;
}

void AppKernel::processControlFrame() {
    input::updateWindowHotkeys();
    if (input::consumeMenuOpenRequest()) oreui::requestMenuOpen();
    structure::processPendingActions();
    if (processRenderSettingsEdits()) structure::saveSettings();
    if (processSettingsActions()) structure::saveSettings();
    auto client = ll::service::getClientInstance();
    if (client && i18n::followsGameLanguage())
        i18n::updateGameLanguage(client->getOptions().getLanguage());
    oreui::updateActionHintOverlay();
    processTaskActions();
    processStructureWriteActions();
    processStructurePreviewActions();
    bool const worldAvailable = client && client->$getClientInstanceState() == ClientInstanceState::Playing && client->getLocalPlayer();
    if (consumeHudLayoutEditRequest() && worldAvailable) {
        oreui::requestMenuClose();
        ui::hud::requestHudEditorScene();
    }
    if (auto completed = ui::hud::consumeHudEditorCompletion(); completed) {
        if (*completed) structure::saveSettings();
        if (worldAvailable) oreui::requestMenuOpen(oreui::MenuEntryPage::Hud);
    }
}

bool AppKernel::load() {
    i18n::initLanguageStore();
    structure::loadSettings();
    loadTasks();
    return true;
}

bool AppKernel::enable() {
    input::clearMenuOpenRequest();
    auto& logger = LHolo::getInstance().getSelf().getLogger();

    projection::detail::resetMeshWorkerForSession();
    structure::detail::enableStructureFileLoader();

    if (!projection::detail::projectionController().installHooks()) {
        logger.error("Failed to install projection hooks");
        return false;
    }

    if (!oreui::installMenuBridge() || !oreui::installMenuScreen() || AppClientUpdate::hook() != 0) {
        oreui::uninstallMenuScreen();
        oreui::uninstallMenuBridge();
        projection::detail::projectionController().uninstallHooks();
        structure::detail::shutdownStructureFileLoader();
        logger.error("Failed to install the LHolo menu or client control update");
        return false;
    }

    if (!ui::hud::installNativeHudRenderer()) logger.warn("Failed to install the native HUD renderer");
    if (!oreui::installActionHintOverlay()) logger.warn("Failed to install the Ore UI action hint overlay");

    if (!ui::hud::installHudEditorScene()) logger.error("Failed to install the native HUD editor scene");
    if (!ui::preview::installStructurePreviewScene()) logger.error("Failed to install the native structure preview scene");
    if (!structure::write::installLocalWorldWriter()) logger.error("Failed to install the local world writer");
    if (!structure::write::command::installCommandWorldWriter()) logger.error("Failed to install the command world writer");

    if (!place::installHook()) {
        logger.warn("Failed to install easy-place hooks");
    }

    if (!input::installProjectionInputGuard()) {
        logger.warn("Failed to install projection mouse-wheel guard");
    }

    controlFramesEnabled.store(true, std::memory_order_release);
    logger.info("LHolo enabled. Chat LHolo and the configured menu hotkey open the OreUI main menu.");
    return true;
}

bool AppKernel::disable() {
    auto& logger = LHolo::getInstance().getSelf().getLogger();
    if (controlFramesEnabled.exchange(false, std::memory_order_acq_rel)) AppClientUpdate::unhook();
    input::uninstallWindowHotkeys();
    input::clearMenuOpenRequest();

    resetStructurePreviewActions();
    ui::preview::uninstallStructurePreviewScene();
    ui::hud::uninstallHudEditorScene();
    ui::hud::uninstallNativeHudRenderer();
    oreui::uninstallActionHintOverlay();
    oreui::uninstallMenuScreen();
    oreui::uninstallMenuBridge();

    resetStructureWriteActions();
    structure::write::uninstallLocalWorldWriter();
    structure::write::command::uninstallCommandWorldWriter();

    structure::detail::shutdownStructureFileLoader();
    structure::saveSettings();
    shutdownTasks();
    structure::detail::shutdownMaterialTracker();
    // Drop all world-owned state before removing hooks. In particular, this
    // resets held placement/input state and joins the projection mesh worker
    // while its Level/Dimension pointers are still valid.
    place::resetWorldSession();
    structure::resetWorldSession();
    structure::capture::clear();
    input::uninstallProjectionInputGuard();
    place::uninstallHook();
    projection::detail::projectionController().uninstallHooks();
    // Projection hooks contain the automatic overlay-install retry path, so
    // remove them before tearing the overlay down.

    logger.info("LHolo disabled");
    return true;
}

} // namespace lholo::app
