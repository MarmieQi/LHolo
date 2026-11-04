#include "ui/hud/editor/HudEditorScene.h"
#include "ui/hud/editor/HudEditor.h"
#include "ui/hud/editor/HudEditorView.h"
#include "ui/hud/NativeHudIcons.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "plugin/LHolo.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/ViewRequest.h"
#include "mc/client/gui/ScreenSizeData.h"
#include "mc/client/gui/screens/ScreenController.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/client/gui/screens/UIScene.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/gui/UIDefRepository.h"
#include "mc/deps/json/Reader.h"
#include "mc/deps/json/Value.h"
#include "mc/deps/input/enums/ButtonState.h"
#include <atomic>
#include <string>
#include <vector>

namespace lholo::ui::hud {
namespace {
std::atomic_bool requested{};
std::atomic_int completion{-1};
bool opened{}, closing{}, accepted{};
std::string screenName;
bool updateInstalled{}, renderInstalled{}, pointerInstalled{}, buttonInstalled{}, focusInstalled{};
bool definitionInstalled{};
auto& logger() { return LHolo::getInstance().getSelf().getLogger(); }

// The editor owns this definition; no JSON UI resource is registered for it.
constexpr char editorScreenName[] = "lholo.hud_editor";
constexpr char editorSceneName[] = "hud_editor"; // SceneFactory strips the namespace.
bool isEditor(std::string const& name) { return name == editorSceneName; }
constexpr char editorDefinitionJson[] = R"json({
  "type": "screen",
  "render_game_behind": true,
  "render_only_when_topmost": false,
  "should_steal_mouse": false,
  "is_showing_menu": true,
  "is_modal": true,
  "absorbs_input": true,
  "low_frequency_rendering": false,
  "force_render_below": false,
  "controls": [{
    "cursor": {"type": "custom", "size": [16, 16], "renderer": "cursor_renderer", "layer": 1000}
  }],
  "button_mappings": [{
    "from_button_id": "button.menu_cancel",
    "to_button_id": "button.lholo_hud_cancel",
    "mapping_type": "global"
  }]
})json";

Json::Value const& editorDefinition() {
    static Json::Value const definition = [] {
        Json::Value result;
        Json::Reader reader;
        if (!reader.parse(editorDefinitionJson, result, false))
            logger().error("[native-hud.editor] Could not parse in-memory scene definition: {}", reader.getFormattedErrorMessages());
        return result;
    }();
    return definition;
}

LL_TYPE_INSTANCE_HOOK(HudEditorDefinition, ll::memory::HookPriority::Normal,
    UIDefRepository, &UIDefRepository::$findDef, Json::Value const&,
    std::string const& defNamespace, std::string const& defName) {
    if (defNamespace == "lholo" && defName == "hud_editor") {
        return editorDefinition();
    }
    return origin(defNamespace, defName);
}
void processScene(ClientInstance& client) {
    bool const world = client.$getClientInstanceState() == ClientInstanceState::Playing && client.getLocalPlayer();
    ::Bedrock::NonOwnerPointer<ISceneStack> stack = client.$getCurrentSceneStack();
    if (!world) requested = false;
    if (opened) {
        auto const present = stack && stack->isOnSceneStack(screenName);
        auto const scheduled = stack && stack->hasScheduledScreens();
        if (!closing) {
            accepted = processHudLayoutEditor(false, world, present || scheduled);
            closing = !hudLayoutEditorActive();
            if (!closing) prepareNativeHudIconItems();
        }
        if (closing) {
            auto* top = stack ? stack->getTopScene() : nullptr;
            if (present && top && top->getScreenName() == screenName && stack->getScheduledPopCount() == 0)
                stack->schedulePopScreenWithExpectedNames(std::vector<std::string>{screenName});
            if (!present && !scheduled) {
                opened = closing = false;
                screenName.clear();
                completion = accepted ? 1 : 0;
            }
        }
    }
    if (!world || opened || !requested.load() || !stack || stack->hasScheduledScreens()) return;
    auto* top = stack->getTopScene();
    // Open the editor only after the OreUI menu has actually left.
    if (top && top->isShowingMenu()) return;
    requested = false;
    auto controller = std::make_shared<ScreenController>(false);
    controller->registerButtonPressedHandler(controller->_getNameId("button.lholo_hud_cancel"),
        [](UIPropertyBag*) { cancelHudLayoutEditor(); return ::ui::ViewRequest::ConsumeEvent; });
    auto scene = client.$getSceneFactory().createUIScene(editorScreenName, std::move(controller));
    if (!scene) {
        logger().error("[native-hud.editor] Could not create {}", editorScreenName);
        completion = 0;
        return;
    }
    auto const newScreenName = scene->getScreenName();
    auto screen = client.$getSceneFactory()._createScreen(std::move(scene));
    screenName = newScreenName;
    processHudLayoutEditor(true, true, true);
    closing = accepted = false;
    stack->pushScreen(std::move(screen), false);
    opened = true;
}
LL_TYPE_INSTANCE_HOOK(HudEditorUpdate, ll::memory::HookPriority::Normal,
    ClientInstance, &ClientInstance::$update, bool, bool initialized, bool allowGameUpdate) {
    bool const result = origin(initialized, allowGameUpdate);
    if (!opened && !requested.load()) return result;
    auto client = ll::service::getClientInstance();
    if (client && &client.get() == this) processScene(*this);
    return result;
}
LL_TYPE_INSTANCE_HOOK(HudEditorRender, ll::memory::HookPriority::Normal,
    UIScene, &UIScene::$render, void, ScreenContext& screen, FrameRenderObject const& frame) {
    // Use this editor scene's native render context, independent of ScreenView batching.
    if (hudLayoutEditorActive() && isEditor(getScreenName())) {
        auto client = ll::service::getClientInstance();
        if (client) {
            setupForRendering(screen);
            struct RenderScope {
                UIScene& scene; ScreenContext& screen;
                ~RenderScope() { scene.cleanupForRendering(screen); }
            } scope{*this, screen};
            MinecraftUIRenderContext context{client.get(), screen, *this};
            drawHudEditorView(context, client.get());
        }
    }
    origin(screen, frame);
}
LL_TYPE_INSTANCE_HOOK(HudEditorPointer, ll::memory::HookPriority::Normal,
    ScreenView, &ScreenView::_handlePointerLocation, void, glm::vec2 const& position,
    FocusImpact impact, bool motionless, bool rightStick) {
    if (hudLayoutEditorActive() && isEditor(getScreenName())) {
        auto client = ll::service::getClientInstance();
        if (client) {
            auto const& size = client->$getGuiData()->mScreenSizeData->clientUIScreenSize.get();
            if (size.x > 0 && size.y > 0)
                moveHudEditorPointer(position.x / size.x, position.y / size.y);
        }
    }
    origin(position, impact, motionless, rightStick);
}
LL_TYPE_INSTANCE_HOOK(HudEditorButton, ll::memory::HookPriority::Normal,
    ScreenView, &ScreenView::handleButtonEvent, void, uint button, ButtonState state, FocusImpact impact) {
    if (hudLayoutEditorActive() && isEditor(getScreenName()) && button == mPointerButtonIds.get()[0]) {
        if (state != ButtonState::None) pressHudEditorPointer(state == ButtonState::Down);
        return;
    }
    origin(button, state, impact);
}
LL_TYPE_INSTANCE_HOOK(HudEditorFocusLost, ll::memory::HookPriority::Normal,
    UIScene, &UIScene::$onFocusLost, void) {
    if (hudLayoutEditorActive() && isEditor(getScreenName())) releaseHudEditorPointer();
    origin();
}
}
void requestHudEditorScene() {
    requested = true;
}
std::optional<bool> consumeHudEditorCompletion() {
    auto const result = completion.exchange(-1);
    return result < 0 ? std::nullopt : std::optional<bool>{result == 1};
}
bool installHudEditorScene() {
    if (definitionInstalled) return true;
    definitionInstalled = HudEditorDefinition::hook() == 0;
    if (!definitionInstalled) return false;
    updateInstalled = HudEditorUpdate::hook() == 0;
    renderInstalled = HudEditorRender::hook() == 0;
    pointerInstalled = HudEditorPointer::hook() == 0;
    buttonInstalled = HudEditorButton::hook() == 0;
    focusInstalled = HudEditorFocusLost::hook() == 0;
    if (updateInstalled && renderInstalled && pointerInstalled && buttonInstalled && focusInstalled) return true;
    uninstallHudEditorScene();
    return false;
}
void uninstallHudEditorScene() {
    if (opened) {
        auto client = ll::service::getClientInstance();
        if (client) {
            ::Bedrock::NonOwnerPointer<ISceneStack> stack = client->$getCurrentSceneStack();
            auto* top = stack ? stack->getTopScene() : nullptr;
            if (top && top->getScreenName() == screenName)
                stack->schedulePopScreenWithExpectedNames(std::vector<std::string>{screenName});
        }
    }
    shutdownHudLayoutEditor();
    requested = opened = closing = accepted = false;
    screenName.clear();
    completion = -1;
    if (focusInstalled) HudEditorFocusLost::unhook();
    if (buttonInstalled) HudEditorButton::unhook();
    if (pointerInstalled) HudEditorPointer::unhook();
    if (renderInstalled) HudEditorRender::unhook();
    if (updateInstalled) HudEditorUpdate::unhook();
    if (definitionInstalled) HudEditorDefinition::unhook();
    updateInstalled = renderInstalled = pointerInstalled = buttonInstalled = focusInstalled = false;
    definitionInstalled = false;
}
}
