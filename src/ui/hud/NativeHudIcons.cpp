#include "ui/hud/NativeHudIcons.h"
#include "render/ScopedShaderColor.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "plugin/LHolo.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/ShaderColor.h"
#include "mc/world/item/ItemStack.h"

#include <string>
#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lholo::ui::hud {
namespace {
using IconItem = std::shared_ptr<ItemStack const>;
std::atomic_bool active{}, failed{}, contextMissingLogged{};
bool tickInstalled{}, flushInstalled{};
std::mutex cacheMutex;
std::uint64_t cacheGeneration{};
std::uint64_t cacheEpoch{};
std::unordered_map<std::string, IconItem> cache;
std::unordered_set<std::string> pending;
struct PreparedIcon { HudIconDraw draw; IconItem item; };
struct IconFrame {
    UIRenderContext& context;
    ClientInstance& client;
    std::vector<PreparedIcon> icons;
    bool consumed{};
};
thread_local IconFrame* currentFrame{};
auto& logger() { return LHolo::getInstance().getSelf().getLogger(); }

void prepareItems() {
    std::unordered_set<std::string> requests;
    std::uint64_t epoch{};
    {
        std::lock_guard lock(cacheMutex);
        epoch = cacheEpoch;
        requests.swap(pending);
    }
    for (auto const& id : requests) {
        {
            std::lock_guard lock(cacheMutex);
            // A render frame can queue an ID while the previous tick batch
            // is still resolving it. Do not reconstruct an already ready item.
            if (cacheEpoch != epoch || !active.load()) break;
            if (cache.contains(id)) continue;
        }
        auto item = std::make_shared<ItemStack>();
        if (!id.empty()) {
            // "namespace:name:aux" selects one variant (a banner colour).
            std::string_view name{id};
            short aux = 0;
            if (auto const colon = name.rfind(':'); colon != std::string_view::npos && colon + 1 < name.size()
                && name.find(':') != colon
                && name.find_first_not_of("0123456789", colon + 1) == std::string_view::npos) {
                aux = static_cast<short>(std::stoi(std::string{name.substr(colon + 1)}));
                name = name.substr(0, colon);
            }
            item->reinit(name, 1, aux);
        }
        IconItem resolved = item->isNull() ? nullptr : std::move(item);
        std::lock_guard lock(cacheMutex);
        if (cacheEpoch != epoch || !active.load()) continue;
        // Null entries cache unsupported IDs too, avoiding repeated lookups.
        cache.insert_or_assign(id, std::move(resolved));
    }
}

// Preserve mutable shared screen state around the native item renderer.
struct ScreenState {
    ScreenContext& screen;
    render::ScopedShaderColor color, darkColor;
    bool drawingUI;
    MatrixStack::MatrixStackRef world, view, projection;
    explicit ScreenState(ScreenContext& value) : screen(value),
        color(value.currentShaderColor), darkColor(value.currentShaderDarkColor, value.currentShaderDarkColor.color.get()),
        drawingUI(value.isDrawingUI), world(value.camera.worldMatrixStack->push(false)),
        view(value.camera.viewMatrixStack->push(false)), projection(value.camera.projectionMatrixStack->push(false)) {
        screen.isDrawingUI = true;
    }
    ~ScreenState() {
        screen.isDrawingUI = drawingUI;
    }
};

LL_TYPE_INSTANCE_HOOK(HudIconItemsTick, ll::memory::HookPriority::Normal,
    LocalPlayer, &LocalPlayer::$tickWorld, void, Tick const& tick) {
    origin(tick);
    if (!active.load() || failed.load()) return;
    auto client = ll::service::getClientInstance();
    if (!client || client->getLocalPlayer() != this
        || client->$getClientInstanceState() != ClientInstanceState::Playing) return;
    try { prepareItems(); }
    catch (std::exception const& error) {
        if (!failed.exchange(true)) logger().warn("[native-hud.icons] item preparation disabled: {}", error.what());
    }
}

LL_TYPE_INSTANCE_HOOK(HudIconTextFlush, ll::memory::HookPriority::Normal,
    MinecraftUIRenderContext, &MinecraftUIRenderContext::$flushText, void,
    float deltaTime, std::optional<float> obfuscateSwitchTime) {
    origin(deltaTime, obfuscateSwitchTime);
    auto* frame = currentFrame;
    if (!frame || frame->consumed || static_cast<UIRenderContext*>(this) != &frame->context) return;
    frame->consumed = true;
    if (!active.load() || failed.load() || frame->icons.empty()) return;
    try {
        auto* renderer = frame->client.$getItemRenderer();
        if (!renderer) return;
        auto& screen = mScreenContext;
        ScreenState restore(screen);
        std::optional<BaseActorRenderContext> actors;
        for (auto const& icon : frame->icons) {
            if (!icon.item) {
                auto const inset = icon.draw.size * 0.25f;
                drawRectangle({icon.draw.x + inset, icon.draw.x + icon.draw.size - inset,
                    icon.draw.y + inset, icon.draw.y + icon.draw.size - inset}, mce::Color{0.5f, 0.5f, 0.5f, 1.0f}, 1, 1);
                continue;
            }
            if (!actors) actors.emplace(screen, frame->client, frame->client.$getMinecraftGame_DEPRECATED());
            // Vanilla inventory icons occupy 16 GUI units at scale 1.
            renderer->renderGuiItemNew(*actors, *icon.item, 0, icon.draw.x, icon.draw.y,
                false, 1.0f, 1.0f, icon.draw.size / 16.0f, 0);
        }
    } catch (std::exception const& error) {
        if (!failed.exchange(true)) logger().warn("[native-hud.icons] icon drawing disabled; HUD text remains available: {}", error.what());
    }
}
}

void prepareNativeHudIconItems() {
    if (!active.load() || failed.load()) return;
    prepareItems();
}

void clearNativeHudIcons() {
    std::lock_guard lock(cacheMutex);
    cacheGeneration = 0;
    ++cacheEpoch;
    cache.clear();
    pending.clear();
}

void flushHudTextAndIcons(UIRenderContext& context, ClientInstance& client,
    std::uint64_t generation, std::span<HudIconDraw const> icons) {
    if (icons.empty()) {
        clearNativeHudIcons();
        context.flushText(0.0f, std::nullopt);
        return;
    }
    IconFrame frame{context, client, {}};
    if (active.load() && !failed.load()) {
        std::lock_guard lock(cacheMutex);
        if (cacheGeneration != generation || cache.size() > 64) {
            cache.clear(); pending.clear(); cacheGeneration = generation;
            ++cacheEpoch;
        }
        frame.icons.reserve(icons.size());
        for (auto const& icon : icons) {
            auto found = cache.find(icon.id);
            frame.icons.push_back({icon, found == cache.end() ? nullptr : found->second});
            if (found == cache.end()) pending.insert(icon.id);
        }
    }
    // A borrowed frame exists only for this synchronous virtual call. All
    // vanilla text flushes outside it pass through without icon processing.
    auto* previous = currentFrame;
    currentFrame = &frame;
    struct Reset { IconFrame* previous; ~Reset() { currentFrame = previous; } } reset{previous};
    context.flushText(0.0f, std::nullopt);
    if (active.load() && !frame.consumed && !contextMissingLogged.exchange(true))
        logger().warn("[native-hud.icons] expected native UI context callback not observed; icons skipped");
}

bool installNativeHudIcons() {
    if (active.load()) return true;
    failed = contextMissingLogged = false;
    clearNativeHudIcons();
    flushInstalled = HudIconTextFlush::hook() == 0;
    tickInstalled = flushInstalled && HudIconItemsTick::hook() == 0;
    active = flushInstalled && tickInstalled;
    if (!active.load()) {
        if (flushInstalled) HudIconTextFlush::unhook();
        flushInstalled = tickInstalled = false;
    }
    return active.load();
}
void uninstallNativeHudIcons() {
    active = false;
    if (tickInstalled) HudIconItemsTick::unhook();
    if (flushInstalled) HudIconTextFlush::unhook();
    tickInstalled = flushInstalled = false;
    clearNativeHudIcons();
}
}
