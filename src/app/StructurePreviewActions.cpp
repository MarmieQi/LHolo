#include "app/StructurePreviewActions.h"
#include "app/TaskActions.h"
#include "structure/StructureFileLoader.h"
#include "structure/StructurePaths.h"
#include "structure/capture/CapturePreviewBuilder.h"
#include "ui/preview/StructurePreviewScene.h"
#include "oreui/OreUiScreen.h"
#include "i18n/Translator.h"
#include <mutex>
#include <optional>
#include <utility>

namespace lholo::app {
namespace {
std::mutex mutex;
std::optional<StructurePreviewIntent> pending;
StructurePreviewStatus status;
std::uint64_t readId{};
oreui::MenuEntryPage returnPage{oreui::MenuEntryPage::Tasks};
std::optional<structure::capture::CustomStructure> savedCustomDraft;
}
bool submitStructurePreview(StructurePreviewIntent intent) {
    std::lock_guard lock(mutex);
    if (!intent.id || intent.id <= status.ack || pending || readId || ui::preview::structurePreviewActive()) return false;
    pending = std::move(intent); return true;
}
StructurePreviewStatus structurePreviewStatus() { std::lock_guard lock(mutex); return status; }
std::optional<structure::capture::CustomStructure> consumePreviewCustomDraft() {
    std::lock_guard lock(mutex);
    return std::exchange(savedCustomDraft, std::nullopt);
}
void resetStructurePreviewActions() {
    std::lock_guard lock(mutex);
    pending.reset();
    savedCustomDraft.reset();
    returnPage = oreui::MenuEntryPage::Tasks;
    structure::detail::cancelStructureRead(readId);
    readId = 0;
}
void processStructurePreviewActions() {
    std::lock_guard lock(mutex);
    // Revoke publication before taking a result which completed on this frame.
    if (auto finished = ui::preview::consumeStructurePreviewCompletion()) {
        structure::detail::cancelStructureRead(readId);
        readId = 0;
        if (*finished && getTaskStatus().worldAvailable) oreui::requestMenuOpen(returnPage);
        else savedCustomDraft.reset();
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

    if (request.source == PreviewSourceKind::CaptureWorld || request.source == PreviewSourceKind::CaptureCustom) {
        if (request.source == PreviewSourceKind::CaptureWorld) {
            auto const captureSnap = structure::capture::getSnapshot();
            if (!captureSnap.worldAvailable || captureSnap.revision != request.revision) {
                status.error = i18n::tr(captureSnap.worldAvailable ? i18n::TextKey::OreUiCaptureChanged : i18n::TextKey::CaptureStatusNoWorld);
                return;
            }
        }
        auto built = request.source == PreviewSourceKind::CaptureWorld
            ? structure::capture::buildWorldSelectionPreview(request.captureDraft)
            : structure::capture::buildCustomStructurePreview(request.customStructure);
        if (!built.loaded) {
            if (!built.error.empty()) status.error = std::move(built.error);
            return;
        }
        auto const titleKey = request.source == PreviewSourceKind::CaptureWorld
            ? i18n::TextKey::CaptureTabWorld
            : i18n::TextKey::CaptureTabCustom;
        if (!ui::preview::requestStructurePreviewScene(request.id, i18n::tr(titleKey), request.frame)) return;
        ui::preview::completeStructurePreviewRead(request.id, std::move(built.loaded), {});
        if (request.source == PreviewSourceKind::CaptureCustom) {
            returnPage = oreui::MenuEntryPage::CreateCustom;
            savedCustomDraft = std::move(request.customStructure);
        } else {
            returnPage = oreui::MenuEntryPage::CreateWorld;
            savedCustomDraft.reset();
        }
        status.accepted = true; status.error.clear();
        structure::resetHotkeyState();
        oreui::requestMenuClose();
        return;
    }

    auto snapshot = getTaskSnapshot();
    if (!snapshot.worldAvailable || snapshot.loading || snapshot.revision != request.revision) return;
    auto* task = tasks::findTask(snapshot.catalog, request.taskId);
    auto* file = task ? tasks::findFile(*task, request.fileId) : nullptr;
    if (!file) return;
    auto const name = structure::detail::pathToUtf8(structure::detail::pathFromUtf8(file->path).filename());
    if (!structure::detail::startStructureRead(request.id, file->path)) return;
    readId = request.id;
    if (!ui::preview::requestStructurePreviewScene(request.id, name, request.frame)) {
        structure::detail::cancelStructureRead(readId);
        readId = 0; return;
    }
    returnPage = oreui::MenuEntryPage::Tasks;
    savedCustomDraft.reset();
    status.accepted = true; status.error.clear();
    structure::resetHotkeyState();
    oreui::requestMenuClose();
}
}
