#include "oreui/OreUiMenuResources.h"
#include "oreui/OreUiActionHint.h"
#include "plugin/LHolo.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "mc/client/gui/oreui/resources/ResourceRequest.h"
#include "mc/client/gui/oreui/resources/ResourceResponse.h"
#include "mc/client/gui/oreui/resources/handlers/HybridResourceHandler.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/resources/ResourcePackManager.h"
#include "mc/external/gameface/cohtml/IAsyncResourceResponse.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

namespace lholo::oreui {
namespace {
bool requestInstalled{};
std::string_view menuResourcePath(std::string_view url) {
    if (url == "ui://./hbui/lholo-menu.html") return "hbui/lholo-menu.html";
    if (url == "ui://./hbui/lholo-menu.js") return "hbui/lholo-menu.js";
    return {};
}
auto& logger() { return LHolo::getInstance().getSelf().getLogger(); }

// 1.26.51 HybridResourceHandler only searches trusted packs for ui:// assets.
// Serve exact LHolo URLs without changing trust
// or handling any vanilla/third-party URL. The broker owns response completion.
LL_TYPE_INSTANCE_HOOK(MenuResourceRequest, ll::memory::HookPriority::Normal,
    OreUI::HybridResourceHandler, &OreUI::HybridResourceHandler::$onResourceRequest,
    Gameface::ResourceHandlerStatus, Gameface::ResourceRequest const& request,
    Gameface::ResourceResponse& response) {
    auto const& url = request.mAbsoluteUrl.get();
    auto const overlayPage = url == "ui://./hbui/lholo-action-hint.html" ? actionHintOverlayPage() : std::string_view{};
    auto const path = overlayPage.empty() ? menuResourcePath(url) : kActionHintOverlayPath;
    std::string menuContents;
    if (overlayPage.empty()) {
        auto const pack = LHolo::getInstance().getSelf().getResourceDir() / "LHoloUI/manifest.json";
        if (path.empty() || !std::filesystem::exists(pack)) return origin(request, response);
        ResourceLocation const location{Core::PathView{path}, ResourceFileSystem::UserPackage};
        if (!mPackManager.load(location, menuContents)
            || menuContents.find("lholo-menu-v7-settings") == std::string::npos) {
            logger().error("[oreui.resource] Missing or incompatible mounted resource: {}", path);
            return Gameface::ResourceHandlerStatus::Failure;
        }
    }
    auto const contents = overlayPage.empty() ? std::string_view{menuContents} : overlayPage;
    auto* target = response.mResponse;
    if (!target || request.mRange.get().has_value()) return Gameface::ResourceHandlerStatus::Failure;
    auto* destination = target->GetSpace(contents.size());
    if (!destination) return Gameface::ResourceHandlerStatus::Failure;
    std::memcpy(destination, contents.data(), contents.size());
    target->SetStatus(200);
    target->SetHeader("Content-Type", path.ends_with(".html") ? "text/html; charset=utf-8"
                                                           : "application/javascript; charset=utf-8");
    target->SetHeader("Cache-Control", "no-store");
    return Gameface::ResourceHandlerStatus::Success;
}

}

bool installMenuResources() {
    requestInstalled = MenuResourceRequest::hook() == 0;
    if (requestInstalled) return true;
    uninstallMenuResources();
    return false;
}
void uninstallMenuResources() {
    if (requestInstalled) MenuResourceRequest::unhook();
    requestInstalled = false;
}
}
