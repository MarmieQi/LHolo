#include "oreui/OreUiActionHint.h"
#include "oreui/OreUiActionHintPage.h"
#include "oreui/OreUiScreen.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/oreui/Scene.h"
#include "mc/client/gui/oreui/SceneState.h"
#include "mc/client/gui/oreui/input/BedrockInputSource.h"
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
std::shared_ptr<OreUI::Scene> hintScene;

std::mutex messageMutex;
structure::detail::ActionHintSnapshot currentHint;
std::uint64_t hintRevision{};
std::uint64_t publishedRevision{};

// The View participates in native input routing even when its scene is off-stack.
constexpr auto kHintRouteMode = static_cast<OreUI::RouteMode>(
    static_cast<int>(OreUI::RouteMode::Overlay) | static_cast<int>(OreUI::RouteMode::PassInputToGameplay));

void setHint(structure::detail::ActionHintSnapshot hint) {
    std::lock_guard lock(messageMutex);
    if (currentHint.text == hint.text && currentHint.expiry == hint.expiry && currentHint.persistent == hint.persistent)
        return;
    currentHint = std::move(hint);
    ++hintRevision;
}

void clearScene() {
    currentHost = nullptr;
    if (!hintScene) return;
    hintScene->terminate();
    hintScene.reset();
    setHint({});
}

void drawHint(AbstractScene const* host, ScreenContext& context) {
    if (host != currentHost) return;
    std::lock_guard lock(sceneMutex);
    if (host != currentHost) return;
    auto const& view = hintScene->mView.get();
    if (!view) return;
    view->renderOffscreen(context);
    view->render(context);
}

LL_TYPE_INSTANCE_HOOK(ActionHintNativeRender, ll::memory::HookPriority::Normal,
    UIScene, &UIScene::$render, void, ScreenContext& context, FrameRenderObject const& frame) {
    origin(context, frame);
    drawHint(this, context);
}

LL_TYPE_INSTANCE_HOOK(ActionHintOreUiRender, ll::memory::HookPriority::Normal,
    OreUI::Scene, &OreUI::Scene::$render, void, ScreenContext& context, FrameRenderObject const& frame) {
    origin(context, frame);
    drawHint(this, context);
}

LL_TYPE_INSTANCE_HOOK(ActionHintViewUpdate, ll::memory::HookPriority::Normal,
    OreUI::View, &OreUI::View::$update, void, double time) {
    bool const isHintView = mUrl.get() == "ui://./hbui/lholo-action-hint.html";
    // CSS hit testing happens after the native View has received input.
    if (isHintView && mBedrockInputSource.get())
        mBedrockInputSource->mIgnoreBedrockInput = true;
    origin(time);
    if (!isHintView || mState != OreUI::ViewState::ActiveFirstMeaningfulPaintHappened) return;

    structure::detail::ActionHintSnapshot hint;
    {
        std::lock_guard lock(messageMutex);
        if (publishedRevision == hintRevision) return;
        publishedRevision = hintRevision;
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
        hintScene = std::static_pointer_cast<OreUI::Scene>(client->$getSceneFactory().createSceneFromUrl(
            "/" + std::string{kActionHintOverlayPath}, kHintRouteMode));
        if (!hintScene) return;
        hintScene->init(client->$getGuiData()->mScreenSizeData.get());
        hintScene->mState = OreUI::SceneState::InputDisabled;
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
