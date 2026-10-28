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
void resetPreviewBindings() { std::lock_guard lock(mutex); published.reset(); }
}
