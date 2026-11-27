#include "oreui/RenderSettingsBindings.h"
#include "oreui/RenderSettingsProtocol.h"
#include "app/RenderSettingsActions.h"
#include "nlohmann/json.hpp"
#include <mutex>
namespace lholo::oreui {
namespace { std::mutex mutex; std::optional<app::RenderSettingsState> published; }
bool queueRenderSettingsIntent(std::string_view payload) {
    auto edit = parseRenderSettingsIntent(payload);
    return edit && app::submitRenderSettingsEdit(*edit);
}
std::optional<std::string> takeRenderSettingsUpdate(bool force) {
    std::lock_guard lock(mutex);
    auto s = app::getRenderSettingsState();
    if (!force && published == s) return std::nullopt;
    published = s;
    auto const& v = s.values;
    return nlohmann::json{{"values", {{"bounds", v.bounds}, {"axis", structure::toInt(v.layers.axis)},
        {"mode", structure::toInt(v.layers.mode)}, {"layer", v.layers.layer}, {"fill", v.fill}, {"outline", v.outline},
        {"correctionSeeThrough", v.correctionSeeThrough}, {"missingSeeThrough", v.missingSeeThrough}}},
        {"maximum", s.maximum}, {"context", s.context}, {"ack", s.ack}, {"accepted", s.accepted}}.dump();
}
void resetRenderSettingsBindings() {
    std::lock_guard lock(mutex);
    published.reset(); app::resetRenderSettingsEdits();
}
}
