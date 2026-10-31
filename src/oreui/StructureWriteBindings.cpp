#include "oreui/StructureWriteBindings.h"
#include "oreui/StructureWriteProtocol.h"
#include "structure/write/StructureWriteText.h"
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
    auto keyId = [](i18n::TextKey key) -> std::string_view {
        return key == i18n::TextKey::Count ? std::string_view{} : i18n::kTextKeyIds[static_cast<std::size_t>(key)];
    };
    auto const result = keyId(structure::write::writeResultText(status.result));
    auto const commandState = keyId(structure::write::writeCommandText(status.command));
    return nlohmann::json{{"ack", status.ack}, {"worldRevision", status.worldRevision},
        {"busy", status.busy}, {"localAvailable", status.localAvailable},
        {"commandAvailable", status.commandAvailable}, {"mode", status.mode == structure::write::WriteMode::LocalWorld ? "local" : "commands"},
        {"command", {{"active", status.command.active}, {"canResume", status.command.canResume},
            {"done", status.command.done}, {"total", status.command.total}, {"state", commandState}}},
        {"targetName", status.targetName}, {"skipAir", status.skipAir}, {"skipPlaced", status.skipPlaced},
        {"dimension", status.dimension ? nlohmann::json(*status.dimension) : nlohmann::json(nullptr)}, {"result", result}}.dump();
}
void resetStructureWriteBindings() { std::lock_guard lock(mutex); published.reset(); }
}
