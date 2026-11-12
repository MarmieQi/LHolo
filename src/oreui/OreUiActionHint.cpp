#include "oreui/OreUiActionHint.h"
#include "oreui/OreUiActionHintPage.h"
#include "oreui/OreUiScreen.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/oreui/Scene.h"
#include "mc/client/gui/oreui/interface/ViewState.h"
#include "mc/client/gui/oreui/views/View.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/UIScene.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "nlohmann/json.hpp"
#include "structure/StructureUiState.h"
#include "ui/hud/editor/HudEditor.h"
#include "ui/preview/StructurePreviewScene.h"

#include <Windows.h>
#include <atomic>
#include <mutex>

namespace lholo::oreui {
namespace {
std::atomic_bool installed{};
std::atomic<AbstractScene const*> currentHost{};
std::mutex sceneMutex;
std::shared_ptr<AbstractScene> hintScene;

std::mutex messageMutex;
structure::detail::ActionHintSnapshot currentHint;
bool hintPending{};

void setHint(structure::detail::ActionHintSnapshot hint) {
    std::lock_guard lock(messageMutex);
    if (currentHint.text == hint.text && currentHint.expiry == hint.expiry && currentHint.persistent == hint.persistent)
        return;
    currentHint = std::move(hint);
    hintPending = true;
}

void clearScene() {
    currentHost = nullptr;
    if (!hintScene) return;
    hintScene->terminate();
    hintScene.reset();
    std::lock_guard lock(messageMutex);
    currentHint = {};
    hintPending = false;
}

void drawHint(AbstractScene const* host, ScreenContext& context, FrameRenderObject const& frame) {
    if (host != currentHost) return;
    std::lock_guard lock(sceneMutex);
    if (host != currentHost) return;
    hintScene->preRenderUpdate(context);
    hintScene->prepareFrame(context);
    hintScene->render(context, frame);
    hintScene->postRenderUpdate(context);
}

LL_TYPE_INSTANCE_HOOK(ActionHintNativeRender, ll::memory::HookPriority::Normal,
    UIScene, &UIScene::$render, void, ScreenContext& context, FrameRenderObject const& frame) {
    origin(context, frame);
    drawHint(this, context, frame);
}

LL_TYPE_INSTANCE_HOOK(ActionHintOreUiRender, ll::memory::HookPriority::Normal,
    OreUI::Scene, &OreUI::Scene::$render, void, ScreenContext& context, FrameRenderObject const& frame) {
    origin(context, frame);
    drawHint(this, context, frame);
}

LL_TYPE_INSTANCE_HOOK(ActionHintViewUpdate, ll::memory::HookPriority::Normal,
    OreUI::View, &OreUI::View::$update, void, double time) {
    origin(time);
    if (mUrl.get() != "ui://./hbui/lholo-action-hint.html"
        || mState != OreUI::ViewState::ActiveFirstMeaningfulPaintHappened) return;

    structure::detail::ActionHintSnapshot hint;
    {
        std::lock_guard lock(messageMutex);
        if (!hintPending) return;
        hintPending = false;
        hint = currentHint;
    }
    auto const now = GetTickCount64();
    auto const remaining = hint.expiry > now ? hint.expiry - now : 0;
    $triggerEvent("lholo:action-hint", nlohmann::json{
        {"text", hint.text}, {"remainingMs", remaining}, {"persistent", hint.persistent}
    }.dump());
}
}

bool installActionHintOverlay() {
    if (ActionHintNativeRender::hook() != 0) return false;
    if (ActionHintOreUiRender::hook() != 0) {
        ActionHintNativeRender::unhook();
        return false;
    }
    if (ActionHintViewUpdate::hook() != 0) {
        ActionHintOreUiRender::unhook();
        ActionHintNativeRender::unhook();
        return false;
    }
    installed = true;
    return true;
}

void updateActionHintOverlay() {
    if (!installed) return;
    auto const now = GetTickCount64();
    auto& state = structure::detail::StructureUiState::getInstance();
    auto const menuVisible = isMenuOnTop();
    if (!state.actionHintActive(now, menuVisible) && !currentHost) return;
    auto client = ll::service::getClientInstance();
    auto* top = client ? client->$getCurrentSceneStack().get()->getTopScene() : nullptr;
    std::lock_guard lock(sceneMutex);
    if (!top || ui::hud::hudLayoutEditorActive() || ui::preview::structurePreviewActive()) {
        clearScene();
        return;
    }
    auto hint = state.actionHint(now, menuVisible);
    if (hint.text.empty()) {
        clearScene();
        return;
    }
    if (!hintScene) {
        hintScene = client->$getSceneFactory().createSceneFromUrl("/" + std::string{kActionHintOverlayPath},
            OreUI::RouteMode::Overlay);
        if (!hintScene) return;
        hintScene->init(client->$getGuiData()->mScreenSizeData.get());
    }
    setHint(std::move(hint));
    currentHost = top;
}

void uninstallActionHintOverlay() {
    if (!installed.exchange(false)) return;
    ActionHintViewUpdate::unhook();
    ActionHintOreUiRender::unhook();
    ActionHintNativeRender::unhook();
    std::lock_guard lock(sceneMutex);
    clearScene();
}

std::string_view actionHintOverlayPage() {
    if (!installed) return {};
    return kActionHintPage;
}
}
