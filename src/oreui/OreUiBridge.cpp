#include "oreui/OreUiBridge.h"
#include "oreui/PreviewBindings.h"
#include "oreui/StructureWriteBindings.h"
#include "app/SettingsActions.h"

#include "oreui/MenuBindings.h"
#include "oreui/CaptureBindings.h"
#include "oreui/TransformBindings.h"
#include "oreui/RenderSettingsBindings.h"
#include "oreui/SettingsBindings.h"
#include "oreui/TaskBindings.h"
#include "oreui/TaskProtocol.h"
#include "i18n/Translator.h"
#include "mc/client/gui/oreui/binding/FacetRegistry.h"
#include "oreui/IntentFacet.h"
#include "oreui/OreUiScreen.h"
#include "plugin/LHolo.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "mc/client/gui/oreui/binding/Binder.h"
#include "mc/client/gui/oreui/binding/QueryRegistry.h"
#include "mc/client/gui/oreui/binding/ViewContextFactory.h"
#include "mc/client/gui/oreui/binding/interface/IHandlerFactory.h"
#include "mc/client/gui/oreui/views/View.h"
#include "mc/external/gameface/cohtml/IEventHandler.h"

#include <atomic>
#include <exception>
#include <memory>
#include <new>
#include <string>

namespace lholo::oreui {
namespace {
std::atomic_bool active{}, labelsPending{};
std::atomic_bool materialsRequestPending{}, materialsStatePending{};
bool registered{}, viewInstalled{}, intentInstalled{};
auto& logger() { return LHolo::getInstance().getSelf().getLogger(); }
enum class MenuQuery { Ready, Close, RequestMaterials, CancelHotkeyCapture, EditHudLayout };
std::optional<i18n::Language> publishedLanguage;

// Typed facet activation is the supported scalar-payload entry point. No
// layout assumptions about the opaque cohtml::ArgumentsBinder are required.
using FacetPayload = std::unordered_map<std::string, std::variant<double, bool, std::string>>;
bool receiveCaptureIntent(std::string_view request) {
    if (!active.load() || !isMenuOnTop()) return false;
    return queueCaptureIntent(request);
}
bool receiveTransformIntent(std::string_view request) {
    return active.load() && isMenuOnTop() && queueTransformIntent(request);
}
bool receiveRenderSettingsIntent(std::string_view request) {
    return active.load() && isMenuOnTop() && queueRenderSettingsIntent(request);
}
bool receiveSettingsIntent(std::string_view request) {
    return active.load() && isMenuOnTop() && queueSettingsIntent(request);
}
bool receivePreviewIntent(std::string_view request) {
    return active.load() && isMenuOnTop() && queuePreviewIntent(request);
}
bool receiveStructureWriteIntent(std::string_view request) {
    return active.load() && isMenuOnTop() && queueStructureWriteIntent(request);
}
bool receiveTaskIntent(std::string_view request) {
    return active.load() && isMenuOnTop() && queueTaskIntent(request);
}
LL_TYPE_INSTANCE_HOOK(MenuIntentActivation, ll::memory::HookPriority::Normal,
    OreUI::FacetRegistry, &OreUI::FacetRegistry::$activateFacet,
    std::optional<OreUI::IFacetRegistry::Error>, OreUI::FacetBinder& binder,
    std::string const& name, std::string const& id, FacetPayload const& payload) {
    if (name == "lholo.capture.intent" || name == "lholo.transform.intent" || name == "lholo.render.intent" || name == "lholo.settings.intent" || name == "lholo.tasks.intent" || name == "lholo.preview.intent" || name == "lholo.write.intent") {
        if (!active.load() || !isMenuOnTop()) return OreUI::IFacetRegistry::Error::ActivateNotFound;
        bool known{};
        for (auto const& prototype : mFacetPrototypes.get()) if (prototype.name.get() == name) known = true;
        if (!known) {
            if (name == "lholo.write.intent")
                $registerFacet(name, [] { return std::make_unique<IntentFacet>(receiveStructureWriteIntent, "lholo.write.intent"); });
            else if (name == "lholo.preview.intent")
                $registerFacet(name, [] { return std::make_unique<IntentFacet>(receivePreviewIntent, "lholo.preview.intent"); });
            else if (name == "lholo.capture.intent")
                $registerFacet(name, [] { return std::make_unique<IntentFacet>(receiveCaptureIntent); });
            else if (name == "lholo.transform.intent")
                $registerFacet(name, [] { return std::make_unique<IntentFacet>(receiveTransformIntent, "lholo.transform.intent"); });
            else if (name == "lholo.render.intent")
                $registerFacet(name, [] { return std::make_unique<IntentFacet>(receiveRenderSettingsIntent, "lholo.render.intent"); });
            else if (name == "lholo.settings.intent")
                $registerFacet(name, [] { return std::make_unique<IntentFacet>(receiveSettingsIntent, "lholo.settings.intent"); });
            else if (name == "lholo.tasks.intent")
                $registerFacet(name, [] { return std::make_unique<IntentFacet>(receiveTaskIntent, "lholo.tasks.intent", kTaskIntentMaxBytes); });
        }
    }
    return origin(binder, name, id, payload);
}

class MenuHandler final : public cohtml::IEventHandler {
public:
    explicit MenuHandler(MenuQuery query) : mQuery(query) {}
    void Invoke(cohtml::ArgumentsBinder*) override {
        if (!active.load()) return;
        switch (mQuery) {
        case MenuQuery::Close: requestMenuClose(); break;
        case MenuQuery::Ready: labelsPending = materialsStatePending = true; break;
        case MenuQuery::RequestMaterials: materialsRequestPending = materialsStatePending = true; break;
        case MenuQuery::CancelHotkeyCapture: if (isMenuOnTop()) cancelSettingsHotkeyCapture(); break;
        case MenuQuery::EditHudLayout: if (isMenuOnTop()) app::requestHudLayoutEdit(); break;
        }
    }
    void const* GetTarget() override { return nullptr; }
    void SetTarget(void*) override {}
private:
    MenuQuery mQuery;
};
class MenuHandlerFactory final : public OreUI::Detail::IHandlerFactory {
public:
    explicit MenuHandlerFactory(MenuQuery query) : mQuery(query) {}
    cohtml::IEventHandler* makeHandler() const override { return new (std::nothrow) MenuHandler{mQuery}; }
private:
    MenuQuery mQuery;
};

LL_TYPE_INSTANCE_HOOK(MenuViewUpdate, ll::memory::HookPriority::Normal,
    OreUI::View, &OreUI::View::$update, void, double time) {
    origin(time);
    if (!active.load()) return;
    try {
        auto const menuView = mUrl.get().find("lholo-menu.html") != std::string::npos;
        if (!menuView || !isMenuOnTop()) return;
        auto const ready = labelsPending.exchange(false);
        if (ready) $triggerEvent("lholo:menu-entry", menuEntryPage() == MenuEntryPage::Hud ? R"("hud")" : R"("tasks")");
        if (ready || publishedLanguage != i18n::language()) {
            publishedLanguage = i18n::language();
            $triggerEvent("lholo:menu-labels", buildMenuLabels());
        }
        if (materialsRequestPending.exchange(false)) requestMaterialList();
        if (auto update = takeTransformUpdate(ready)) $triggerEvent("lholo:transform", *update);
        if (auto update = takeRenderSettingsUpdate(ready)) $triggerEvent("lholo:render", *update);
        if (auto update = takeSettingsUpdate(ready)) $triggerEvent("lholo:settings", *update);
        if (auto update = takeTaskUpdate(ready)) $triggerEvent("lholo:tasks", *update);
        if (auto update = takePreviewUpdate(ready)) $triggerEvent("lholo:preview", *update);
        if (auto update = takeStructureWriteUpdate(ready)) $triggerEvent("lholo:write", *update);
        if (auto update = takeCaptureUpdate(ready)) {
            $triggerEvent("lholo:capture", *update);
        }
        if (auto update = takeProjectionUpdate(ready)) {
            $triggerEvent("lholo:projection", *update);
        }
        auto const forceMaterials = materialsStatePending.exchange(false) || ready;
        if (auto update = takeMaterialsUpdate(forceMaterials)) {
            $triggerEvent("lholo:materials", *update);
        }
    } catch (std::exception const& error) {
        logger().error("[oreui] menu update failed: {}", error.what());
    }
}


}

bool installMenuBridge() {
    if (active.load()) return true;
    viewInstalled = MenuViewUpdate::hook() == 0;
    if (!viewInstalled) return false;
    intentInstalled = MenuIntentActivation::hook() == 0;
    if (!intentInstalled) { uninstallMenuBridge(); return false; }
    if (!registered) {
        OreUI::Detail::ViewContextFactory::_queryStorage().push_back(
            [](OreUI::Detail::QueryRegistry& registry, OreUI::Detail::ViewContextFactory&) {
                struct QueryDefinition { char const* name; MenuQuery action; };
                for (auto const& definition : {
                    QueryDefinition{"lholo.menu.ready", MenuQuery::Ready},
                    QueryDefinition{"lholo.menu.close", MenuQuery::Close},
                    QueryDefinition{"lholo.materials.request", MenuQuery::RequestMaterials},
                    QueryDefinition{"lholo.hud.edit-layout", MenuQuery::EditHudLayout},
                    QueryDefinition{"lholo.hotkeys.cancel", MenuQuery::CancelHotkeyCapture}
                }) {
                    OreUI::Detail::QueryRegistry::QueryTemplate query;
                    query.mHandlerFactory = std::make_unique<MenuHandlerFactory>(definition.action);
                    query.bind(definition.name, registry.mBinder.get().get());
                    registry.mQueryTemplates.get()[definition.name] = std::move(query);
                }
            });
        registered = true;
    }
    labelsPending = false;
    active = true;
    return true;
}

void resetMenuBridge() {
    labelsPending = false;
    materialsRequestPending = materialsStatePending = false;
    resetMenuBindings();
    resetCaptureBindings();
    resetTransformBindings();
    resetRenderSettingsBindings();
    resetSettingsBindings();
    resetTaskBindings();
    resetPreviewBindings();
    resetStructureWriteBindings();
    publishedLanguage.reset();
}
bool hasMenuRegistrations() { return registered; }
void uninstallMenuBridge() {
    active = false;
    resetMenuBridge();
    if (intentInstalled) MenuIntentActivation::unhook();
    intentInstalled = false;
    if (viewInstalled) MenuViewUpdate::unhook();
    viewInstalled = false;
    shutdownTaskBindings();
    shutdownCaptureBindings();
}
}
