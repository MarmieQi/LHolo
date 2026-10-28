#include "app/StructurePreviewActions.h"
#include "app/TaskActions.h"
#include "structure/StructureFileLoader.h"
#include "structure/StructurePaths.h"
#include "ui/preview/StructurePreviewScene.h"
#include "oreui/OreUiScreen.h"
#include "i18n/Translator.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include "diagnostics/StructurePreviewTrace.h"
#include <mutex>
#include <optional>
#include <utility>

namespace lholo::app {
namespace {
std::mutex mutex;
std::optional<StructurePreviewIntent> pending;
StructurePreviewStatus status;
std::uint64_t readId{};
}
bool submitStructurePreview(StructurePreviewIntent intent) {
    std::lock_guard lock(mutex);
    if (!intent.id || intent.id <= status.ack || pending || readId || ui::preview::structurePreviewActive()) return false;
    pending = std::move(intent); return true;
}
StructurePreviewStatus structurePreviewStatus() { std::lock_guard lock(mutex); return status; }
void resetStructurePreviewActions() {
    std::lock_guard lock(mutex);
    pending.reset();
    structure::detail::cancelStructureRead(readId);
    readId = 0;
}
void processStructurePreviewActions() {
    std::lock_guard lock(mutex);
    // Revoke publication before taking a result which completed on this frame.
    if (auto finished = ui::preview::consumeStructurePreviewCompletion()) {
        structure::detail::cancelStructureRead(readId);
        readId = 0;
        if (*finished && getTaskStatus().worldAvailable) oreui::requestMenuOpen(oreui::MenuEntryPage::Tasks);
    }
    if (auto result = structure::detail::takeStructureReadResult()) {
        if (result->id == readId && result->worldRevision == structure::detail::structureFileWorldRevision())
            ui::preview::completeStructurePreviewRead(readId, result->loaded,
                result->error.empty() && !result->loaded ? i18n::tr(i18n::TextKey::PreviewFailed) : result->error);
        if (result->id == readId) readId = 0;
    }
    if (!pending) return;
    auto request = std::move(*std::exchange(pending, {}));
    status = {request.id, false, i18n::tr(i18n::TextKey::PreviewUnavailable)};
    auto snapshot = getTaskSnapshot();
    if (!snapshot.worldAvailable || snapshot.loading || snapshot.revision != request.revision) return;
    auto* task = tasks::findTask(snapshot.catalog, request.taskId);
    auto* file = task ? tasks::findFile(*task, request.fileId) : nullptr;
    if (!file) return;
    auto const name = structure::detail::pathToUtf8(structure::detail::pathFromUtf8(file->path).filename());
    auto& logger = LHolo::getInstance().getSelf().getLogger();
    logger.info("[structure-preview] request id={} task={} file={} path={}",request.id,request.taskId,request.fileId,file->path);
    diagnostics::traceStructurePreview(fmt::format("request id={} path={}",request.id,file->path));
    logger.flush();
    if (!structure::detail::startStructureRead(request.id, file->path)) return;
    readId = request.id;
    if (!ui::preview::requestStructurePreviewScene(request.id, name, request.frame)) {
        structure::detail::cancelStructureRead(readId);
        readId = 0; return;
    }
    status.accepted = true; status.error.clear();
    structure::resetHotkeyState();
    oreui::requestMenuClose();
}
}
