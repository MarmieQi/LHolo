#include "oreui/MenuBindings.h"

#include "i18n/Translator.h"
#include "structure/StructurePaths.h"
#include "structure/ProjectionActions.h"
#include "structure/MaterialTracker.h"
#include "structure/StructureUiState.h"
#include "structure/StructureFileLoader.h"
#include "structure/StructureSession.h"
#include "ui/StructureFilePicker.h"
#include "ui/MaterialFormat.h"
#include "nlohmann/json.hpp"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"

#include <mutex>
#include <exception>
#include <cstdio>
#include <vector>

namespace lholo::oreui {
namespace {
std::mutex bindingsMutex;
ui::StructureFilePicker fileBrowser;
std::string selectedPath;
unsigned long browseError{};
bool fileDirty{}, publishedBusy{};
std::string publishedProjection;
std::optional<std::uint64_t> publishedMaterialsRevision;
std::uint64_t observedGeneration{};
bool worldAvailable() {
    auto client = ll::service::getClientInstance();
    return client && client->$getClientInstanceState() == ClientInstanceState::Playing && client->getLocalPlayer();
}
}

std::string buildMenuLabels() {
    nlohmann::json labels = nlohmann::json::object();
    auto const language = i18n::language();
    for (std::size_t index = 0; index < i18n::kTextKeyCount; ++index) {
        labels[std::string{i18n::kTextKeyIds[index]}] = i18n::tr(static_cast<i18n::TextKey>(index), language);
    }
    return labels.dump();
}

void requestStructureFileBrowse() {
    std::lock_guard lock(bindingsMutex);
    fileDirty = true; // Always acknowledge even when the shared picker gate refuses.
    if (structure::projectionActionPending() || structure::detail::isStructureFileLoading()) return;
    try {
        if (fileBrowser.start(structure::detail::pathFromUtf8(selectedPath))) {
            browseError = 0;
            fileDirty = true;
        }
    } catch (std::exception const&) {
        browseError = 1;
        fileDirty = true;
    }
}

void loadSelectedProjection() {
    std::lock_guard lock(bindingsMutex);
    if (worldAvailable() && !fileBrowser.busy()) (void)structure::requestProjectionLoad(selectedPath);
}

void closeActiveProjection() { structure::requestProjectionClose(); }

void requestMaterialList() { structure::requestMaterialList(); }

void restoreLastProjection() {
    std::lock_guard lock(bindingsMutex);
    if (!worldAvailable() || fileBrowser.busy()) return;
    auto const saved = structure::detail::StructureSession::getInstance().savedProjection();
    if (structure::requestProjectionRestore() && saved.available) {
        selectedPath = saved.structurePath;
        fileDirty = true;
    }
}

std::optional<std::string> takeProjectionUpdate(bool force) {
    std::lock_guard lock(bindingsMutex);
    auto const snapshot = structure::detail::StructureSession::getInstance().snapshot();
    if (snapshot.loaded && snapshot.loaded->generation != observedGeneration) {
        observedGeneration = snapshot.loaded->generation;
        selectedPath = snapshot.lastPath;
        fileDirty = true;
    }
    char origin[512]{};
    if (snapshot.saved.available) std::snprintf(origin, sizeof(origin),
        i18n::tr(i18n::TextKey::LabelSavedOrigin), snapshot.saved.anchorX, snapshot.saved.anchorY, snapshot.saved.anchorZ);
    auto state = nlohmann::json{
        {"status", snapshot.status},
        {"worldAvailable", worldAvailable()},
        {"loading", structure::projectionActionPending() || structure::detail::isStructureFileLoading()},
        {"hasProjection", static_cast<bool>(snapshot.loaded)},
        {"canRestore", snapshot.saved.available && !snapshot.saved.structurePath.empty()},
        {"savedOrigin", origin}
    }.dump();
    if (!force && state == publishedProjection) return std::nullopt;
    publishedProjection = state;
    return state;
}

std::optional<std::string> takeMaterialsUpdate(bool force) {
    std::lock_guard lock(bindingsMutex);
    auto const& ui = structure::detail::StructureUiState::getInstance();
    auto const snapshot = ui.materialListSnapshotIfChanged(force ? std::nullopt : publishedMaterialsRevision);
    if (!snapshot) return std::nullopt;
    auto items = nlohmann::json::array();
    for (auto const& req : snapshot->requirements) {
        auto const keyIndex = req.nameKey ? static_cast<std::size_t>(*req.nameKey) : 0;
        auto const nameKey = (keyIndex > 0 && keyIndex < i18n::kTextKeyCount)
            ? std::string{i18n::kTextKeyIds[keyIndex]}
            : std::string{};
        // Display identity may be a block (liquids), independently of the
        // inventory item used by material availability / placement rules.
        // The native OreUI item icon takes an item name only and cannot show
        // one variant (a banner colour), so variants keep an empty slot rather
        // than a wrongly coloured icon.
        auto const iconId = req.iconAux >= 0 ? std::string{} : ui::materialIconId(req.itemId, req.typeName);
        items.push_back({
            {"displayName", req.displayName},
            {"nameKey", nameKey},
            {"typeName", req.typeName},
            {"iconId", iconId},
            {"count", req.count},
        });
    }
    auto state = nlohmann::json{{"ready", snapshot->ready}, {"items", std::move(items)}}.dump();
    publishedMaterialsRevision = snapshot->revision;
    return state;
}

std::optional<std::string> takeStructureFileUpdate(bool force) {
    std::lock_guard lock(bindingsMutex);
    if (auto result = fileBrowser.takeResult()) {
        if (result->path) selectedPath = structure::detail::pathToUtf8(*result->path);
        browseError = result->errorCode;
        fileDirty = true;
    }
    auto const busy = fileBrowser.busy();
    if (!force && !fileDirty && busy == publishedBusy) return std::nullopt;
    publishedBusy = busy;
    fileDirty = false;
    return nlohmann::json{{"path", selectedPath}, {"browsing", busy}, {"error", browseError}}.dump();
}

void resetMenuBindings() {
    std::lock_guard lock(bindingsMutex);
    fileBrowser.cancel();
    browseError = 0;
    fileDirty = true;
    publishedProjection.clear();
    publishedMaterialsRevision.reset();
}

void shutdownMenuBindings() {
    std::lock_guard lock(bindingsMutex);
    fileBrowser.shutdown();
    browseError = 0;
    fileDirty = true;
}
}
