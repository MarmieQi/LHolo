#include "oreui/PreviewBindings.h"
#include "oreui/PreviewProtocol.h"
#include "app/StructurePreviewActions.h"
#include "nlohmann/json.hpp"
#include <mutex>
namespace lholo::oreui {
namespace { std::mutex mutex; std::optional<app::StructurePreviewStatus> published; }
bool queuePreviewIntent(std::string_view payload) {
    auto value = parsePreviewIntent(payload);
    return value && app::submitStructurePreview(std::move(*value));
}
std::optional<std::string> takePreviewUpdate(bool force) {
    std::lock_guard lock(mutex);
    auto status = app::structurePreviewStatus();
    if (!force && published == status) return {};
    published = status;
    return nlohmann::json{{"ack",status.ack},{"accepted",status.accepted},{"error",status.error}}.dump();
}
std::optional<std::string> takePreviewCustomDraft() {
    auto draft = app::consumePreviewCustomDraft();
    if (!draft) return {};
    auto layers = nlohmann::json::array();
    for (auto const& layer : draft->layers) {
        layers.push_back({{"y", layer.y}, {"endY", layer.endY}, {"block", layer.block}});
    }
    return nlohmann::json{
        {"size", {{"x", draft->size[0]}, {"y", draft->size[1]}, {"z", draft->size[2]}}},
        {"layers", std::move(layers)},
    }.dump();
}
void resetPreviewBindings() { std::lock_guard lock(mutex); published.reset(); }
}
