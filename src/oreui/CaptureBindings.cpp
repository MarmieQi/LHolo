#include "oreui/CaptureBindings.h"
#include "oreui/CaptureProtocol.h"
#include "structure/capture/CaptureBounds.h"
#include "structure/capture/CustomBlockCatalog.h"
#include "i18n/Translator.h"
#include "ui/StructureFilePicker.h"
#include "nlohmann/json.hpp"
#include <mutex>
#include <utility>

namespace lholo::oreui {
namespace {
struct PublishedCapture {
    structure::capture::Snapshot snapshot;
    i18n::TextKey customStatus{};
    i18n::Language language{};
    std::uint64_t ack{};
    bool busy{};
    std::string error;
    bool operator==(PublishedCapture const&) const = default;
};
std::mutex mutex;
std::optional<structure::capture::Request> intent;
ui::StructureFilePicker outputPicker{ui::selectMcstructureOutput, ui::StructureFileTypes::Mcstructure};
std::optional<structure::capture::Request> inFlight;
std::optional<PublishedCapture> published;
std::string error;
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
    if (intent || inFlight || outputPicker.busy()) return false;
    auto request = parseCaptureIntent(payload);
    if (!request || request->id <= ack) return false;
    intent = std::move(request);
    return true;
}

std::string buildCustomBlockCatalog() {
    auto entries = nlohmann::json::array();
    for (auto const& block : structure::capture::customBlockCatalog(i18n::languageCode(i18n::language())))
        entries.push_back({{"id", block.id}, {"name", block.name}});
    return entries.dump();
}

std::optional<std::string> takeCaptureUpdate(bool force) {
    using namespace structure::capture;
    std::lock_guard lock(mutex);
    auto snapshot = getSnapshot();
    if (auto request = std::exchange(intent, std::nullopt)) {
        error.clear();
        bool const customExport = request->action == Action::ExportCustom;
        if (customExport) customStatus = i18n::TextKey::None;
        if (!customExport && (!snapshot.worldAvailable || request->revision != snapshot.revision)) {
            ack = request->id;
            error = i18n::tr(i18n::TextKey::OreUiCaptureChanged);
        } else if (request->action == Action::Export || customExport) {
            try {
                if (outputPicker.start({})) { inFlight = std::move(request); selecting = true; }
                else ack = request->id;
            } catch (std::exception const&) {
                ack = request->id;
                error = i18n::tr(i18n::TextKey::OreUiSaveFailed);
            }
        } else if (submitAction(*request)) inFlight = std::move(request);
        else ack = request->id;
    }
    if (auto result = outputPicker.takeResult(); result && inFlight && selecting) {
        selecting = false;
        if (result->path && (inFlight->action == Action::ExportCustom
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
        if (inFlight->action == Action::ExportCustom)
            customStatus = customExportSucceeded() ? i18n::TextKey::CaptureStatusExported : i18n::TextKey::CaptureStatusWriteFailed;
        ack = inFlight->id;
        inFlight.reset();
        snapshot = getSnapshot();
    }
    // World/dimension changes dismiss a stale save dialog before it can export.
    if (inFlight && inFlight->action != Action::ExportCustom
        && (!snapshot.worldAvailable || (selecting && snapshot.revision != inFlight->revision))) {
        outputPicker.cancel();
        ack = inFlight->id;
        inFlight.reset();
        selecting = false;
        error = i18n::tr(i18n::TextKey::OreUiCaptureChanged);
    }
    bool const busy = inFlight.has_value() || outputPicker.busy() || actionPending();
    PublishedCapture current{snapshot, customStatus, i18n::language(), ack, busy, error};
    if (!force && published == current) return std::nullopt;
    published = std::move(current);
    return nlohmann::json{
        {"draft", {{"first", pointJson(snapshot.draft.first)}, {"second", pointJson(snapshot.draft.second)},
            {"includeEntities", snapshot.draft.includeEntities}}},
        {"worldAvailable", snapshot.worldAvailable}, {"status", snapshot.status}, {"customStatus", i18n::tr(customStatus)},
        {"revision", snapshot.revision}, {"ack", ack}, {"maxCustomCells", kMaximumCustomStructureCells},
        {"maxRequestBytes", kCaptureIntentMaxBytes},
        {"busy", busy}, {"error", error}
    }.dump();
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
    published.reset();
}
void shutdownCaptureBindings() { resetCaptureBindings(); outputPicker.shutdown(); }
}
