#include "oreui/TransformBindings.h"
#include "oreui/TransformProtocol.h"
#include "structure/TransformActions.h"
#include "nlohmann/json.hpp"
#include <mutex>
namespace lholo::oreui {
namespace { std::mutex mutex; std::optional<structure::TransformState> published; }
bool queueTransformIntent(std::string_view payload) {
    auto edit = parseTransformIntent(payload);
    return edit && structure::submitTransformEdit(*edit);
}
std::optional<std::string> takeTransformUpdate(bool force) {
    std::lock_guard lock(mutex);
    auto s = structure::getTransformState();
    if (!force && published == s) return std::nullopt;
    published = s;
    return nlohmann::json{{"values", {{"rotation", s.values.rotation}, {"mirror", s.values.mirror},
        {"x", s.values.x}, {"y", s.values.y}, {"z", s.values.z}}},
        {"context", s.context}, {"ack", s.ack}, {"accepted", s.accepted}}.dump();
}
void resetTransformBindings() {
    std::lock_guard lock(mutex);
    published.reset();
    structure::resetTransformEdits();
}
}
