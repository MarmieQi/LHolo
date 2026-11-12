#include "oreui/CaptureBindings.h"
#include "oreui/CaptureProtocol.h"
#include "structure/capture/CaptureBounds.h"
#include "i18n/Translator.h"
#include "ui/StructureFilePicker.h"
#include "nlohmann/json.hpp"
#include <mutex>
#include <utility>

namespace lholo::oreui {
namespace {
std::mutex mutex;
std::optional<std::string> intent;
ui::StructureFilePicker outputPicker{ui::selectMcstructureOutput, ui::StructureFileTypes::Mcstructure};
std::optional<structure::capture::Request> inFlight;
std::string published, error;
i18n::TextKey customStatus{};
std::uint64_t ack{};
bool selecting{};

nlohmann::json pointJson(std::optional<structure::capture::Point> const& point) {
    if (!point) return nullptr;
    return {{"x", point->x}, {"y", point->y}, {"z", point->z}};
}
}
bool queueCaptureIntent(std::string_view payload) {
    std::lock_guard lock(mutex);
    if (payload.size() > 4096 || intent || inFlight || outputPicker.busy()) return false;
    intent = payload;
    return true;
}

std::optional<std::string> takeCaptureUpdate(bool force) {
    using namespace structure::capture;
    std::lock_guard lock(mutex);
    auto snapshot = getSnapshot();
    if (intent) {
        auto request = parseCaptureIntent(*std::exchange(intent, std::nullopt));
        if (request && request->id > ack) {
            error.clear();
            bool const emptyExport = request->action == Action::ExportEmpty;
            if (emptyExport) customStatus = i18n::TextKey::None;
            if (!emptyExport && (!snapshot.worldAvailable || request->revision != snapshot.revision)) {
                ack = request->id;
                error = i18n::tr(i18n::TextKey::OreUiCaptureChanged);

            } else if (request->action == Action::Export || emptyExport) {
                try {
                    if (outputPicker.start({})) { inFlight = *request; selecting = true; }
                    else ack = request->id;
                } catch (std::exception const&) {
                    ack = request->id;
                    error = i18n::tr(i18n::TextKey::OreUiSaveFailed);
                }
            } else if (submitAction(*request)) inFlight = *request;
            else ack = request->id;
        }
    }
    if (auto result = outputPicker.takeResult(); result && inFlight && selecting) {
        selecting = false;
        if (result->path && (inFlight->action == Action::ExportEmpty
            || (snapshot.worldAvailable && snapshot.revision == inFlight->revision))) {
            inFlight->output = std::move(*result->path);
            if (!submitAction(*inFlight)) { ack = inFlight->id; inFlight.reset(); }
        } else {
            if (result->errorCode) error = i18n::tr(i18n::TextKey::OreUiSaveFailed);
            else if (result->path) error = i18n::tr(i18n::TextKey::OreUiCaptureChanged);
            ack = inFlight->id;
            inFlight.reset();
        }
    }
    if (inFlight && !selecting && completedAction() == inFlight->id && !actionPending()) {
        if (inFlight->action == Action::ExportEmpty)
            customStatus = emptyExportSucceeded() ? i18n::TextKey::CaptureStatusExported : i18n::TextKey::CaptureStatusWriteFailed;
        ack = inFlight->id;
        inFlight.reset();
        snapshot = getSnapshot();
    }
    // World/dimension changes dismiss a stale save dialog before it can export.
    if (inFlight && inFlight->action != Action::ExportEmpty
        && (!snapshot.worldAvailable || (selecting && snapshot.revision != inFlight->revision))) {
        outputPicker.cancel();
        ack = inFlight->id;
        inFlight.reset();
        selecting = false;
        error = i18n::tr(i18n::TextKey::OreUiCaptureChanged);
    }
    auto state = nlohmann::json{
        {"draft", {{"first", pointJson(snapshot.draft.first)}, {"second", pointJson(snapshot.draft.second)},
            {"includeEntities", snapshot.draft.includeEntities}}},
        {"worldAvailable", snapshot.worldAvailable}, {"status", snapshot.status}, {"customStatus", i18n::tr(customStatus)},
        {"revision", snapshot.revision}, {"ack", ack}, {"maxEmptyCells", kMaximumEmptyStructureCells},
        {"busy", inFlight.has_value() || outputPicker.busy() || actionPending()}, {"error", error}
    }.dump();
    if (!force && published == state) return std::nullopt;
    published = state;
    return state;
}
void resetCaptureBindings() {
    std::lock_guard lock(mutex);
    intent.reset();
    outputPicker.cancel();
    if (inFlight) ack = inFlight->id;
    inFlight.reset();
    selecting = false;
    error.clear();
    customStatus = i18n::TextKey::None;
    published.clear();
}
void shutdownCaptureBindings() { resetCaptureBindings(); outputPicker.shutdown(); }
}
