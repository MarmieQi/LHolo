#include "structure/capture/CaptureActions.h"
#include "structure/capture/McstructureExporter.h"

#include <mutex>
#include <utility>
#include <gsl/util>

namespace lholo::structure::capture {
namespace {
std::mutex mutex;
std::optional<Request> pending;
bool executing{};
std::uint64_t completed{};
bool emptyExported{};
}
bool submitAction(Request request) {
    std::lock_guard lock(mutex);
    if (pending || executing) return false;
    pending = std::move(request);
    return true;
}
bool actionPending() { std::lock_guard lock(mutex); return pending.has_value() || executing; }
std::uint64_t completedAction() { std::lock_guard lock(mutex); return completed; }
bool emptyExportSucceeded() { std::lock_guard lock(mutex); return emptyExported; }
void resetActions() { std::lock_guard lock(mutex); pending.reset(); }
void resetWorldActions() {
    std::lock_guard lock(mutex);
    if (pending && pending->action != Action::ExportEmpty) pending.reset();
}
void processActions() {
    std::optional<Request> request;
    {
        std::lock_guard lock(mutex);
        if (!pending || executing) return;
        request = std::exchange(pending, std::nullopt);
        executing = true;
    }
    bool exported{};
    auto finish = gsl::finally([&] {
        std::lock_guard lock(mutex);
        emptyExported = exported;
        completed = request->id;
        executing = false;
    });
    if (request->action == Action::ExportEmpty) {
        exported = exportEmptyMcstructure(request->size, request->output);
        return;
    }
    auto const snapshot = getSnapshot();
    // Context changes and edits made in another UI invalidate an old intent,
    // including a save dialog which completed after changing dimensions.
    if (!snapshot.worldAvailable || snapshot.revision != request->revision) return;
    if (request->action == Action::Clear) { clear(); return; }
    updateDraft(request->draft);
    switch (request->action) {
    case Action::Apply: break;
    case Action::FirstFromPlayer: setPointFromPlayer(PointSlot::First); break;
    case Action::SecondFromPlayer: setPointFromPlayer(PointSlot::Second); break;
    case Action::Export: exportStructure(request->draft, request->output); break;
    case Action::Clear: break;
    case Action::ExportEmpty: break;
    }
}
}
