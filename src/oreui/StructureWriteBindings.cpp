#include "oreui/StructureWriteBindings.h"
#include "oreui/StructureWriteProtocol.h"
#include "nlohmann/json.hpp"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include <mutex>
namespace lholo::oreui {
namespace { std::mutex mutex; std::optional<app::StructureWriteStatus> published; }
bool queueStructureWriteIntent(std::string_view payload) {
    auto value = parseStructureWriteIntent(payload);
    return value && app::submitStructureWrite(std::move(*value));
}
std::optional<std::string> takeStructureWriteUpdate(bool force) {
    std::lock_guard lock(mutex);
    auto status = app::structureWriteStatus();
    if (!force && published == status) return {};
    if ((!published || published->detail != status.detail) && !status.detail.empty())
        LHolo::getInstance().getSelf().getLogger().error("[world-write] request {} failed: {}", status.ack, status.detail);
    published = status;
    using enum structure::write::WriteResult;
    std::string result;
    switch (status.result) {
    case None: break;
    case Complete: result = "writeWorld.complete"; break;
    case NoLocalWorld: result = "writeWorld.noLocalWorld"; break;
    case ContextChanged: result = "writeWorld.contextChanged"; break;
    case NoSelection: result = "writeWorld.noSelection"; break;
    case RegionNotLoaded: result = "writeWorld.regionNotLoaded"; break;
    case HeightLimit: result = "writeWorld.heightLimit"; break;
    case CoordinateLimit: result = "writeWorld.coordinateLimit"; break;
    case TemplateFailed: case Failed: result = "writeWorld.failed"; break;
    }
    return nlohmann::json{{"ack", status.ack}, {"worldRevision", status.worldRevision},
        {"busy", status.busy}, {"localAvailable", status.localAvailable}, {"result", result}}.dump();
}
void resetStructureWriteBindings() { std::lock_guard lock(mutex); published.reset(); }
}
