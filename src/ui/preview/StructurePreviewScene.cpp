#include "ui/preview/StructurePreviewScene.h"
#include "ui/preview/StructurePreviewRenderer.h"
#include "ui/preview/StructurePreviewView.h"
#include "ui/preview/StructurePreviewDefinition.h"
#include "structure/StructureLoader.h"
#include "plugin/LHolo.h"
#include "input/MenuInputGuard.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/deps/application/AppPlatform.h"
#include "mc/gamecore/pc/AppPlatform_GameCorePC.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/ScreenSizeData.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/ViewRequest.h"
#include "mc/client/gui/UIDefRepository.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/StructureVolumeRenderer.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/screens/ScreenController.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/gui/screens/UIScene.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/json/Reader.h"
#include "mc/deps/json/Value.h"
#include "mc/deps/input/enums/ButtonState.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/deps/input/MouseDevice.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelListener.h"
#include <atomic>
#include <mutex>
#include <vector>

namespace lholo::ui::preview {
namespace {
std::recursive_mutex mutex;
std::atomic_bool active{};
std::atomic_int completion{-1};
std::atomic_bool onTop{};
bool opened{}, closing{}, returnToMenu{true};
thread_local bool drawingPreviewCanvas{};
std::uint64_t requestId{};
int dimension{};
std::string screenName;
PreviewViewState view;
std::unique_ptr<StructurePreviewRenderer> renderer;
bool definitionInstalled{}, updateInstalled{}, renderInstalled{}, canvasInstalled{}, pointerInstalled{}, buttonInstalled{}, wheelInstalled{}, focusInstalled{}, cursorInstalled{};
void refreshPointer() {
    if (onTop && !closing) {
        if (auto platform = ll::service::getAppPlatform()) platform->setMousePointerType(view.pointerType());
    }
}
bool isPreview(std::string const& name) { return name == "structure_preview"; }
struct WorldGuard final : LevelListener {
    Level* level{};
    void onLevelDestruction(std::string const&) override {
        std::lock_guard lock(mutex);
        // The renderer's chunks, BlockSource and palette pointers must die
        // before the Level's registries, even when the scene is covered.
        renderer.reset(); level = nullptr; closing = true; requestId = 0; returnToMenu = false;
    }
    void detach() { if (level) level->removeListener(*this); level = nullptr; }
} worldGuard;

Json::Value const& definition() {
    static auto value = [] {
        Json::Value result; Json::Reader reader;
        if (!reader.parse(kStructurePreviewDefinition, result, false))
            throw std::runtime_error("Invalid LHolo preview scene definition");
        return result;
    }();
    auto& size = value["controls"][1]["preview_frame"]["size"];
    size[0] = std::to_string(view.frame.width * 100.0f) + "%";
    size[1] = std::to_string(view.frame.height * 100.0f) + "%";
    return value;
}

void finish() {
    renderer.reset(); worldGuard.detach();
    opened = closing = false; requestId = 0; screenName.clear(); view = {};
    active = onTop = false; completion = returnToMenu ? 1 : 0;
}
void processScene(ClientInstance& client) {
    std::lock_guard lock(mutex);
    if (!active) return;
    bool const world = client.$getClientInstanceState() == ClientInstanceState::Playing && client.getLocalPlayer();
    if (!world || !worldGuard.level || (world && static_cast<int>(client.getLocalPlayer()->getDimensionId()) != dimension)) {
        renderer.reset(); closing = true; returnToMenu = false;
    }
    ::Bedrock::NonOwnerPointer<ISceneStack> stack = client.$getCurrentSceneStack();
    bool const present = stack && !screenName.empty() && stack->isOnSceneStack(screenName);
    bool const scheduled = stack && stack->hasScheduledScreens();
    auto* currentTop = stack ? stack->getTopScene() : nullptr;
    onTop = present && currentTop && currentTop->getScreenName() == screenName && !closing;
    if (opened && !present && !scheduled) { finish(); return; }
    if (closing) {
        auto* top = stack ? stack->getTopScene() : nullptr;
        if (present && top && top->getScreenName() == screenName && !stack->getScheduledPopCount())
            stack->schedulePopScreenWithExpectedNames(std::vector<std::string>{screenName});
        if (!opened) finish();
        return;
    }
    if (opened || !stack || scheduled) return;
    auto* top = stack->getTopScene();
    if (top && top->isShowingMenu()) return; // OreUI must have actually left.
    auto controller = std::make_shared<ScreenController>(false);
    controller->registerButtonPressedHandler(controller->_getNameId("button.lholo_preview_close"),
        [](UIPropertyBag*) { std::lock_guard lock(mutex); closing = true; return ::ui::ViewRequest::ConsumeEvent; });
    auto scene = client.$getSceneFactory().createUIScene("lholo.structure_preview", std::move(controller));
    if (!scene) { finish(); return; }
    screenName = scene->getScreenName();
    auto screen = client.$getSceneFactory()._createScreen(std::move(scene));
    stack->pushScreen(std::move(screen), false);
    opened = true;
}
LL_TYPE_INSTANCE_HOOK(PreviewDefinition, ll::memory::HookPriority::Normal,
    UIDefRepository, &UIDefRepository::$findDef, Json::Value const&,
    std::string const& nameSpace, std::string const& name) {
    if (nameSpace == "lholo" && name == "structure_preview") return definition();
    return origin(nameSpace, name);
}
LL_TYPE_INSTANCE_HOOK(PreviewUpdate, ll::memory::HookPriority::Normal,
    ClientInstance, &ClientInstance::$update, bool, bool initialized) {
    bool const result = origin(initialized);
    if (active) { auto client = ll::service::getClientInstance(); if (client && &client.get() == this) {
        processScene(*this);
        std::lock_guard lock(mutex); refreshPointer();
    } }
    return result;
}
// A covered OreUI view can still request Disabled. Resolve cursor appearance
// only while our native preview is the top scene; preserve platform ownership.
LL_TYPE_INSTANCE_HOOK(PreviewCursor, ll::memory::HookPriority::Normal,
    AppPlatform_GameCorePC, &AppPlatform_GameCorePC::$setMousePointerType, void,
    Bedrock::Input::PointerType type) {
    if (onTop) {
        std::lock_guard lock(mutex);
        if (onTop && !closing) type = view.pointerType();
    }
    origin(type);
}
LL_TYPE_INSTANCE_HOOK(PreviewRender, ll::memory::HookPriority::Normal,
    UIScene, &UIScene::$render, void, ScreenContext& screen, FrameRenderObject const& frame) {
    origin(screen, frame);
    if (active && isPreview(getScreenName())) {
        std::lock_guard lock(mutex);
        auto client = ll::service::getClientInstance();
        if (client && worldGuard.level && !closing) {
            setupForRendering(screen);
            struct Scope { UIScene& scene; ScreenContext& screen; ~Scope() { scene.cleanupForRendering(screen); } } scope{*this,screen};
            MinecraftUIRenderContext context{client.get(),screen,*this};
            if (renderer) { view.error = renderer->error(); view.loading = view.error.empty() && !renderer->ready(); }
            drawStructurePreviewView(context,client.get(),view);
        }
    }
}
// Keep the original custom-control batching, clipping, background and cursor
// ordering. Only our named canvas substitutes the independent file renderer.
LL_TYPE_INSTANCE_HOOK(PreviewCanvasRender, ll::memory::HookPriority::Normal,
    StructureVolumeRenderer, &StructureVolumeRenderer::$render, void,
    MinecraftUIRenderContext& context, IClientInstance& client, UIControl& owner, int pass) {
    if (owner.mName.get() != "lholo_preview_canvas" || drawingPreviewCanvas) {
        origin(context, client, owner, pass); return;
    }
    std::lock_guard lock(mutex);
    auto mainClient = ll::service::getClientInstance();
    if (!active || !opened || closing || !worldGuard.level || !mainClient) return;
    drawStructurePreviewBackground(context,mainClient.get(),view);
    if (!renderer) return;
    struct Scope { Scope() { drawingPreviewCanvas = true; } ~Scope() { drawingPreviewCanvas = false; } } scope;
    renderer->render(context,mainClient.get(),owner,view.yaw,view.pitch,view.zoom);
}
LL_TYPE_INSTANCE_HOOK(PreviewPointer, ll::memory::HookPriority::Normal,
    ScreenView, &ScreenView::_handlePointerLocation, void, glm::vec2 const& position,
    FocusImpact impact, bool motionless, bool rightStick) {
    if (active && isPreview(getScreenName())) {
        std::lock_guard lock(mutex);
        auto client = ll::service::getClientInstance();
        if (client) { auto const size = client->$getGuiData()->mScreenSizeData->clientUIScreenSize.get();
            if (size.x > 0 && size.y > 0) view.moveOnScreen(position.x / size.x, position.y / size.y); }
    }
    origin(position, impact, motionless, rightStick);
}
LL_TYPE_INSTANCE_HOOK(PreviewButton, ll::memory::HookPriority::Normal,
    ScreenView, &ScreenView::handleButtonEvent, void, uint button, ButtonState state, FocusImpact impact) {
    if (active && isPreview(getScreenName()) && button == mPointerButtonIds.get()[0]) {
        std::lock_guard lock(mutex);
        if (state != ButtonState::None && view.press(state == ButtonState::Down)) closing = true;
        refreshPointer();
        return;
    }
    origin(button, state, impact);
}
LL_TYPE_INSTANCE_HOOK(PreviewWheel, ll::memory::HookPriority::Highest,
    MouseDevice, &MouseDevice::feed, void, char button, schar data, short x, short y, short dx, short dy, bool motionless) {
    if (active && button == MouseAction::ActionWheel) {
        std::lock_guard lock(mutex);
        // Bedrock's wheel payload is a signed direction (not Win32's 120).
        if (onTop && !closing) {
            if (view.overCanvas() && data) view.wheel(data > 0 ? 1 : -1);
            return;
        }
    }
    origin(button, data, x, y, dx, dy, motionless);
}
LL_TYPE_INSTANCE_HOOK(PreviewFocusLost, ll::memory::HookPriority::Normal,
    UIScene, &UIScene::$onFocusLost, void) {
    if (active && isPreview(getScreenName())) {
        std::lock_guard lock(mutex);
        onTop = false;
        view.dragging = view.closePressed = false;
    }
    origin();
}
}
bool structurePreviewActive() { return active.load(); }
std::optional<bool> consumeStructurePreviewCompletion() {
    auto const value = completion.exchange(-1);
    return value < 0 ? std::nullopt : std::optional<bool>{value == 1};
}
bool requestStructurePreviewScene(std::uint64_t id, std::string name, PreviewFrame frame) {
    std::lock_guard lock(mutex);
    if (active || !definitionInstalled) return false;
    auto client = ll::service::getClientInstance();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player) return false;
    worldGuard.level = &player->getLevel(); worldGuard.level->addListener(worldGuard);
    dimension = static_cast<int>(player->getDimensionId());
    requestId = id; view = {}; view.name = std::move(name); view.frame = frame;
    opened = closing = false; returnToMenu = true; completion = -1; active = true;
    return true;
}
void completeStructurePreviewRead(std::uint64_t id, std::shared_ptr<structure::LoadedStructure const> file, std::string error) {
    std::lock_guard lock(mutex);
    if (!active || closing || id != requestId || !worldGuard.level) return;
    view.error = std::move(error);
    if (file) {
        view.dimensions = {file->sizeX,file->sizeY,file->sizeZ};
        view.blockCount = file->renderBlocks.size();
        view.hasMetadata = true;
        renderer = std::make_unique<StructurePreviewRenderer>(); renderer->setStructure(std::move(file));
    }
    else view.loading = false;
}
bool installStructurePreviewScene() {
    if (definitionInstalled) return true;
    if (!installStructurePreviewRenderer()) return false;
    definitionInstalled = PreviewDefinition::hook() == 0;
    updateInstalled = PreviewUpdate::hook() == 0;
    renderInstalled = PreviewRender::hook() == 0;
    canvasInstalled = PreviewCanvasRender::hook() == 0;
    pointerInstalled = PreviewPointer::hook() == 0;
    buttonInstalled = PreviewButton::hook() == 0;
    wheelInstalled = PreviewWheel::hook() == 0;
    focusInstalled = PreviewFocusLost::hook() == 0;
    cursorInstalled = PreviewCursor::hook() == 0;
    if (definitionInstalled && updateInstalled && renderInstalled && canvasInstalled && pointerInstalled && buttonInstalled && wheelInstalled && focusInstalled && cursorInstalled) return true;
    uninstallStructurePreviewScene(); return false;
}
void uninstallStructurePreviewScene() {
    std::lock_guard lock(mutex);
    if (opened) {
        auto client = ll::service::getClientInstance();
        ::Bedrock::NonOwnerPointer<ISceneStack> stack;
        if (client) stack = client->$getCurrentSceneStack();
        auto* top = stack ? stack->getTopScene() : nullptr;
        if (top && top->getScreenName() == screenName) stack->schedulePopScreenWithExpectedNames(std::vector<std::string>{screenName});
    }
    finish(); completion = -1;
    if (cursorInstalled) PreviewCursor::unhook();
    if (focusInstalled) PreviewFocusLost::unhook();
    if (wheelInstalled) PreviewWheel::unhook();
    if (buttonInstalled) PreviewButton::unhook();
    if (pointerInstalled) PreviewPointer::unhook();
    if (canvasInstalled) PreviewCanvasRender::unhook();
    if (renderInstalled) PreviewRender::unhook();
    if (updateInstalled) PreviewUpdate::unhook();
    if (definitionInstalled) PreviewDefinition::unhook();
    uninstallStructurePreviewRenderer();
    definitionInstalled = updateInstalled = renderInstalled = canvasInstalled = pointerInstalled = buttonInstalled = wheelInstalled = focusInstalled = cursorInstalled = false;
}
}
