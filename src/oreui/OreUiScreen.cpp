#include "oreui/OreUiScreen.h"
#include "ui/hud/editor/HudEditor.h"
#include "oreui/OreUiMenuResources.h"
#include "input/HotkeyCapture.h"
#include "oreui/MenuContextPolicy.h"

#include "oreui/OreUiBridge.h"
#include "plugin/LHolo.h"
#include "structure/StructureLoader.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
#include "mc/client/gui/oreui/interface/RouteMode.h"
#include "mc/client/gui/oreui/interface/RoutePrerequisite.h"
#include "mc/client/gui/oreui/interface/RouteType.h"
#include "mc/client/gui/oreui/routing/RouteAction.h"
#include "mc/client/gui/oreui/routing/RouteHistoryAction.h"
#include "mc/client/gui/oreui/routing/RouteMatcher.h"
#include "mc/client/gui/oreui/routing/Router.h"
#include "mc/client/gui/screens/AbstractScene.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/deps/input/KeyboardEventProcessor.h"
#include "mc/deps/input/win/HIDControllerGameCoreDesktop.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/TextPacket.h"

#include <atomic>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lholo::oreui {
namespace {
std::atomic_bool active{}, openPending{}, closePending{}, menuOnTop{}, escapeHeld{}, escapeForwarded{};
bool sendInstalled{}, serverInstalled{}, downInstalled{}, upInstalled{}, updateInstalled{};
std::optional<ClientInstanceState> openedState;
std::atomic<MenuEntryPage> entryPage{MenuEntryPage::Tasks};
std::string openedScreenName;
auto& logger() { return LHolo::getInstance().getSelf().getLogger(); }
bool isMenuScene(std::string const& name) { return name.find("lholo-menu.html") != std::string::npos; }

bool isMenuCommand(std::string_view message) {
    constexpr std::string_view command{"lholo"};
    if (message.size() != command.size()) return false;
    for (std::size_t index = 0; index < command.size(); ++index) {
        auto character = message[index];
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        if (character != command[index]) return false;
    }
    return true;
}

bool filterMenuCommand(Packet& packet) {
    if (!active.load() || packet.getId() != MinecraftPacketIds::Text) return false;
    auto const& text = static_cast<TextPacket&>(packet);
    auto const* chat = std::get_if<TextPacketPayload::AuthorAndMessage>(&text.mBody.get());
    if (!chat || chat->mType != TextPacketType::Chat || !isMenuCommand(chat->mMessage.get())) return false;
    auto client = ll::service::getClientInstance();
    if (!client || client->$getClientInstanceState() != ClientInstanceState::Playing) return true;
    ::Bedrock::NonOwnerPointer<ISceneStack> stack = client->$getCurrentSceneStack();
    auto* top = stack ? stack->getTopScene() : nullptr;
    if (top && top->getScreenName() == "chat_screen") stack->schedulePopScreen(1);
    openPending = true;
    return true;
}

bool resourcesAvailable() {
    auto const pack = LHolo::getInstance().getSelf().getResourceDir() / "LHoloUI";
    if (!std::filesystem::exists(pack / "manifest.json")) {
        logger().error("[oreui] Missing LHoloUI resource pack manifest: {}", (pack / "manifest.json").string());
        return false;
    }
    auto const root = pack / "hbui";
    for (auto const* file : {"lholo-menu.html", "lholo-menu.js"}) {
        std::ifstream input(root / file, std::ios::binary);
        std::string const contents{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        if (contents.find("lholo-menu-v7-settings") == std::string::npos) {
            logger().error("[oreui] Missing or incompatible {}. Build and deploy the LHolo OreUI assets first.", (root / file).string());
            return false;
        }
    }
    return true;
}

bool ensureRoute(OreUI::Router& router) {
    auto const* base = router.mRouteMatcher.get();
    if (!base) return false;
    auto& matcher = *const_cast<OreUI::RouteMatcher*>(static_cast<OreUI::RouteMatcher const*>(base));
    for (auto const& route : matcher.mRoutes.get()) {
        if (route.pattern.get() == kMenuRoute) return true;
    }
    matcher.addRoute(kMenuAsset, kMenuRoute, OreUI::RouteType::OreUI,
        OreUI::RouteMode::NoLeaveDelay, OreUI::RoutePrerequisite::None,
        [] { return active.load(); },
        [](std::string const& route, OreUI::RouteHistoryAction action) {
            auto client = ll::service::getClientInstance();
            if (!active.load() || !client || !openedState
                || client->$getClientInstanceState() != *openedState) return;
            auto scene = client->$getSceneFactory().createSceneFromUrl(kMenuAsset, OreUI::RouteMode::NoLeaveDelay);
            ::Bedrock::NonOwnerPointer<ISceneStack> stack = client->$getCurrentSceneStack();
            if (scene && stack) {
                openedScreenName = scene->getScreenName();
                OreUI::RouteAction const routeAction{route, action};
                stack->pushScreenWithRouteAction(scene, routeAction);
            }
        });
    return true;
}

// Client update runs with and without a world; opening must not depend on an
// existing OreUI View (the title screen may use a different UI implementation).
LL_TYPE_INSTANCE_HOOK(MenuClientUpdate, ll::memory::HookPriority::Normal,
    ClientInstance, &ClientInstance::$update, bool, bool isInitFinished, bool allowGameUpdate) {
    auto const result = origin(isInitFinished, allowGameUpdate);
    auto client = ll::service::getClientInstance();
    if (active.load() && client && &client.get() == this) {
        try { processMenuScreen(); }
        catch (std::exception const& error) { logger().error("[oreui] menu navigation failed: {}", error.what()); }
    }
    return result;
}

LL_TYPE_INSTANCE_HOOK(MenuSendToServer, ll::memory::HookPriority::Normal,
    LoopbackPacketSender, &LoopbackPacketSender::$sendToServer, void, Packet& packet) {
    if (!filterMenuCommand(packet)) origin(packet);
}
LL_TYPE_INSTANCE_HOOK(MenuSend, ll::memory::HookPriority::Normal,
    LoopbackPacketSender, &LoopbackPacketSender::$send, void, Packet& packet) {
    if (!filterMenuCommand(packet)) origin(packet);
}
LL_TYPE_INSTANCE_HOOK(MenuEscapeDown, ll::memory::HookPriority::Highest,
    HIDControllerGameCoreDesktop, &HIDControllerGameCoreDesktop::$onKeyDown, void,
    int key, Bedrock::Input::KeyboardEventProcessor::InputOrigin source) {
    if (key == 27 && ui::hud::hudLayoutEditorActive()) {
        // Let the native scene handle cancel. Claim repeats after it is popped,
        // and forward the matching release so the game clears its key state.
        escapeHeld = true;
        escapeForwarded = true;
        origin(key, source);
        return;
    }
    if (key == 27 && active.load() && menuOnTop.load()) {
        if (input::hotkeyCaptureActive()) { escapeHeld = true; input::captureHotkeyKey(27, 0); return; }
        // Claim the whole key pair, including auto-repeat after scheduling pop.
        if (!escapeHeld.exchange(true)) {
            requestMenuClose();
        }
        return;
    }
    if (key == 27 && escapeHeld.load()) return;
    origin(key, source);
}
LL_TYPE_INSTANCE_HOOK(MenuEscapeUp, ll::memory::HookPriority::Highest,
    HIDControllerGameCoreDesktop, &HIDControllerGameCoreDesktop::$onKeyUp, void, int key) {
    if (key == 27 && escapeHeld.exchange(false)) {
        if (escapeForwarded.exchange(false)) origin(key);
        return;
    }
    origin(key);
}
}

bool isMenuOnTop() { return menuOnTop.load(); }
void requestMenuOpen(MenuEntryPage page) { if (active.load()) { entryPage = page; openPending = true; } }
MenuEntryPage menuEntryPage() { return entryPage.load(); }
void requestMenuClose() { closePending = true; }

void processMenuScreen() {
    if (!active.load()) return;
    auto client = ll::service::getClientInstance();
    if (!client) {
        if (openedState) resetMenuBridge();
        openedState.reset();
        openedScreenName.clear();
        openPending = closePending = menuOnTop = false;
        return;
    }
    auto const state = client->$getClientInstanceState();
    // The current stack and current router are a pair, for both title and world UI.
    ::Bedrock::NonOwnerPointer<ISceneStack> stack = client->$getCurrentSceneStack();
    auto* top = stack->getTopScene();
    auto const name = top ? top->getScreenName() : std::string{};
    auto const visible = isMenuScene(name);
    if (openedState && state != *openedState) {
        openedState.reset();
        openPending = closePending = false;
        resetMenuBridge();
        structure::resetHotkeyState();
    }
    auto const close = closePending.exchange(false);
    menuOnTop = visible && openedState.has_value() && !close;
    if (visible && (!openedState || close)) {
        // Use the stack's queued-operation state and expected-name pop API.
        // No local close timer or duplicate scene-stack state machine is needed.
        if (stack->getScheduledPopCount() == 0)
            stack->schedulePopScreenWithExpectedNames(std::vector<std::string>{name});
        openedState.reset();
        openPending = false;
        resetMenuBridge();
        return;
    }
    if (close) {
        // Invalidate an opening route before its scene has been pushed.
        openedState.reset();
        openedScreenName.clear();
        openPending = false;
        resetMenuBridge();
        return;
    }
    if (!openedScreenName.empty() && !stack->isOnSceneStack(openedScreenName)
        && !stack->hasScheduledScreens()) {
        openedState.reset();
        openedScreenName.clear();
        resetMenuBridge();
    }
    // A covered page still belongs to the stack. Keep its state, but only the
    // top page can exchange UI events. The existing request cannot open twice.
    if (openedState) { openPending = false; return; }
    if (!openPending.load()) return;
    if (name == "chat_screen") return;
    openPending = false;
    if (!canOpenMenuContext(state == ClientInstanceState::Playing,
            state == ClientInstanceState::Idle, name)) return;
    if (!resourcesAvailable()) return;
    auto& router = client->$getCurrentUIRouter();
    if (!ensureRoute(router)) return;
    resetMenuBridge();
    openedState = state; // The callback may run inside _pushRoute.
    openedScreenName.clear();
    auto const pushed = router._pushRoute(kMenuRoute, OreUI::Router::RouterPushMode::Normal);
    if (!pushed) {
        openedState.reset();
        logger().error("[oreui] Failed to open the LHolo menu route");
    }
    auto* currentTop = stack->getTopScene();
    menuOnTop = pushed && currentTop && isMenuScene(currentTop->getScreenName());
    structure::resetHotkeyState();
}

bool installMenuScreen() {
    if (active.load()) return true;
    if (!installMenuResources()) return false;
    serverInstalled = MenuSendToServer::hook() == 0;
    sendInstalled = MenuSend::hook() == 0;
    downInstalled = MenuEscapeDown::hook() == 0;
    upInstalled = MenuEscapeUp::hook() == 0;
    if (!(serverInstalled && sendInstalled && downInstalled && upInstalled)) {
        uninstallMenuScreen();
        return false;
    }
    updateInstalled = MenuClientUpdate::hook() == 0;
    if (!updateInstalled) { uninstallMenuScreen(); return false; }
    active = true;
    return true;
}

void uninstallMenuScreen() {
    // Called before bridge teardown; pop only our own top scene.
    if (active.load()) {
        closePending = true;
        processMenuScreen();
    }
    active = false;
    if (updateInstalled) MenuClientUpdate::unhook();
    updateInstalled = false;
    openedState.reset();
    openedScreenName.clear();
    uninstallMenuResources();
    if (upInstalled) MenuEscapeUp::unhook();
    if (downInstalled) MenuEscapeDown::unhook();
    if (sendInstalled) MenuSend::unhook();
    if (serverInstalled) MenuSendToServer::unhook();
    sendInstalled = serverInstalled = downInstalled = upInstalled = false;
    openPending = closePending = menuOnTop = escapeHeld = escapeForwarded = false;
}
}
