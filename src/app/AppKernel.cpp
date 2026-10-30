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
#include "ui/hud/NativeHudRenderer.h"
#include "ui/hud/editor/HudEditorScene.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
#include "input/MenuInputGuard.h"
#include "input/MenuOpenRequest.h"
#include "oreui/OreUiBridge.h"
#include "oreui/OreUiScreen.h"
#include "overlay/ImGuiOverlay.h"
#include "place/PlaceHelper.h"
#include "plugin/LHolo.h"
#include "projection/ProjectionController.h"
#include "projection/mesh/ProjectionMeshWorker.h"
#include "structure/capture/StructureCapture.h"
#include "structure/MaterialTracker.h"
#include "structure/StructureLoader.h"
#include "structure/StructureFileLoader.h"

#include "ll/api/mod/NativeMod.h"

namespace lholo::app {

AppKernel& AppKernel::getInstance() {
    static AppKernel instance;
    return instance;
}

void AppKernel::processControlFrame() {
    if (input::consumeMenuOpenRequest()) oreui::requestMenuOpen();
    structure::processPendingActions();
    if (processRenderSettingsEdits()) structure::saveSettings();
    if (processSettingsActions()) structure::saveSettings();
    processTaskActions();
    processStructureWriteActions();
    processStructurePreviewActions();
    auto client = ll::service::getClientInstance();
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

    if (!oreui::installMenuBridge() || !oreui::installMenuScreen()) {
        oreui::uninstallMenuScreen();
        oreui::uninstallMenuBridge();
        projection::detail::projectionController().uninstallHooks();
        structure::detail::shutdownStructureFileLoader();
        logger.error("Failed to install the LHolo OreUI menu");
        return false;
    }

    if (!ui::hud::installNativeHudRenderer()) logger.warn("Failed to install the native HUD renderer");

    if (!ui::hud::installHudEditorScene()) logger.error("Failed to install the native HUD editor scene");
    if (!ui::preview::installStructurePreviewScene()) logger.error("Failed to install the native structure preview scene");
    if (!structure::write::installLocalWorldWriter()) logger.error("Failed to install the local world writer");

    if (!place::installHook()) {
        logger.warn("Failed to install easy-place hooks");
    }

    auto const menuInputGuardStatus = input::installMenuInputGuard();
    if (!menuInputGuardStatus.mouseInputHookInstalled) {
        logger.warn("Failed to install menu mouse-input guard");
    }
    if (!menuInputGuardStatus.keyDownInputHookInstalled) {
        logger.warn("Failed to install menu key-down guard");
    }
    if (!menuInputGuardStatus.keyUpInputHookInstalled) {
        logger.warn("Failed to install menu key-up guard");
    }

    overlay::setControlFrameCallback([] { AppKernel::getInstance().processControlFrame(); });
    if (!overlay::ensureInstalled()) {
        logger.warn("GUI overlay hotkey hooks are not ready; lholo will retry initialization");
    }

    logger.info("LHolo enabled. Chat LHolo and the configured menu hotkey open the OreUI main menu.");
    return true;
}

bool AppKernel::disable() {
    auto& logger = LHolo::getInstance().getSelf().getLogger();
    overlay::setControlFrameCallback(nullptr);
    input::clearMenuOpenRequest();

    resetStructurePreviewActions();
    ui::preview::uninstallStructurePreviewScene();
    ui::hud::uninstallHudEditorScene();
    ui::hud::uninstallNativeHudRenderer();
    oreui::uninstallMenuScreen();
    oreui::uninstallMenuBridge();

    resetStructureWriteActions();
    structure::write::uninstallLocalWorldWriter();

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
    input::uninstallMenuInputGuard();
    place::uninstallHook();
    projection::detail::projectionController().uninstallHooks();
    // Projection hooks contain the automatic overlay-install retry path, so
    // remove them before tearing the overlay down.
    overlay::shutdown();

    logger.info("LHolo disabled");
    return true;
}

} // namespace lholo::app
