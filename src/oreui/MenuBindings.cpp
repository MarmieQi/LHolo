#include "oreui/MenuBindings.h"

#include "i18n/Translator.h"
#include "structure/ProjectionActions.h"
#include "structure/StructureLoader.h"
#include "structure/StructureUiState.h"
#include "structure/StructureFileLoader.h"
#include "structure/StructureSession.h"
#include "ui/MaterialFormat.h"
#include "nlohmann/json.hpp"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
#include <mutex>

namespace lholo::oreui {
namespace {
std::mutex bindingsMutex;
std::string publishedProjection;
std::optional<std::uint64_t> publishedMaterialsRevision;
bool worldAvailable() {
    auto client = ll::service::getClientInstance();
    return client && client->$getClientInstanceState() == ClientInstanceState::Playing && client->getLocalPlayer();
}
}

std::string buildMenuLabels() {
    nlohmann::json labels = nlohmann::json::object();
    auto const language = i18n::language();
    for (std::size_t index = 0; index < i18n::kTextKeyCount; ++index)
        labels[std::string{i18n::kTextKeyIds[index]}] = i18n::tr(static_cast<i18n::TextKey>(index), language);
    return labels.dump();
}

void requestMaterialList() { structure::requestMaterialList(); }

std::optional<std::string> takeProjectionUpdate(bool force) {
    std::lock_guard lock(bindingsMutex);
    auto state = nlohmann::json{
        {"worldAvailable", worldAvailable()},
        {"loading", structure::projectionActionPending() || structure::detail::isStructureFileLoading()},
        {"hasProjection", structure::detail::StructureSession::getInstance().hasLoaded()}
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
        auto const iconId = !req.variantKey.empty() ? std::string{} : std::string{ui::materialIconId(req.itemId, req.typeName)};
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


void resetMenuBindings() {
    std::lock_guard lock(bindingsMutex);
    publishedProjection.clear();
    publishedMaterialsRevision.reset();
}
}
