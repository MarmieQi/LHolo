#include "ui/hud/HudContent.h"
#include "input/MaterialHudPageRequest.h"
#include "ui/hud/editor/HudEditor.h"
#include "structure/StructureLoader.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"
#include "structure/StructurePaths.h"
#include "projection/Projection.h"
#include "place/PlaceHelper.h"
#include "oreui/OreUiScreen.h"
#include <algorithm>
#include <chrono>
#include "i18n/Translator.h"
#include "app/StructureWriteActions.h"
#include "structure/write/StructureWriteText.h"

namespace lholo::ui::hud {
namespace {
struct MaterialHudCacheKey {
    std::uint64_t revision{};
    std::uint64_t generation{};
    int selectedSource{-1};
    int position{-1};
    bool convertStacks{};
    int maxItems{};
    int axis{};
    int mode{};
    int layer{};
    i18n::Language language{};
    bool editing{};
    bool operator==(MaterialHudCacheKey const&) const = default;
};
} // namespace

HudContent readHudContent() {
    // Owned exclusively by the HUD render path; input only supplies intent.
    static MaterialHudPaging paging;
    static std::optional<MaterialHudCacheKey> cachedMaterialKey;
    static HudPanel cachedMaterialPanel;
    auto const pageDelta = input::consumeMaterialHudPageRequest();
    HudContent result;
    bool const editing = hudLayoutEditorActive();
    if (!editing && oreui::isMenuOnTop()) return result;
    auto& state = structure::detail::StructureUiState::getInstance();
    auto settings = state.hud();
    if (editing) settings.enabled = true;
    auto const materialEnabled = editing || state.materialHudEnabled();
    if (!materialEnabled) { paging = {}; cachedMaterialKey.reset(); }
    if (editing || state.writeHudEnabled()) {
        auto const write = app::structureWriteStatus();
        auto const command = write.mode == structure::write::WriteMode::ServerCommands;
        auto const phase = command ? structure::write::writeCommandText(write.command)
            : write.busy ? i18n::TextKey::WriteWorldBusy : structure::write::writeResultText(write.result);
        auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        result.write = makeWriteHud({write.operation, write.finishedAt, write.command.done, write.command.total,
            write.busy, command, write.targetName, phase, write.command.failedSkipped}, state.writeHudPosition(), now);
        if (editing && result.write.lines.empty()) result.write = makeHudEditorContent(settings.position, state.materialHudPosition()).write;
    }
    if (!settings.enabled && !materialEnabled) return result;
    auto const session = structure::detail::StructureSession::getInstance().snapshot();
    if (!session.hasVisibleSelection() || projection::isDimensionSuspended()) {
        paging = {};
        cachedMaterialKey.reset();
        if (editing) {
            auto example = makeHudEditorContent(settings.position, state.materialHudPosition());
            result.projection = std::move(example.projection); result.materials = std::move(example.materials);
        }
        return result;
    }
    result.context = session.loaded->generation;
    if (settings.enabled) {
        ProjectionHudData data;
        data.fileName = structure::detail::pathToUtf8((session.loaded->composed
            ? structure::detail::pathFromUtf8(session.lastPath) : session.loaded->sourcePath).filename());
        data.axis = session.transform.layerAxis;
        data.mode = session.transform.layerDisplayMode;
        data.layer = session.transform.displayLayer;
        data.maxLayer = data.axis == structure::LayerAxis::Material
            ? std::max(0, static_cast<int>(session.editor->materialCount) - 1)
            : data.axis == structure::LayerAxis::X ? session.maxLayerX : session.maxLayerY;
        if (settings.showOverallProgress || settings.showProgress || settings.showWrongState || settings.showWrongType || settings.showExtraBlocks)
            data.progress = projection::getBuildProgress();
        if (settings.showProjectedBlockName) data.aimedBlock = place::getAimedProjectedBlock();
        using i18n::TextKey;
        if (place::isManualMode() || place::getLocalPlacementMode() == place::LocalPlacementMode::Manual) data.placementMode = i18n::tr(TextKey::ModeManual);
        else if (place::isEnabled() || place::getLocalPlacementMode() == place::LocalPlacementMode::Easy) data.placementMode = i18n::tr(TextKey::ModeEasy);
        else if (place::isRangeEnabled() || place::getLocalPlacementMode() == place::LocalPlacementMode::Range) data.placementMode = i18n::tr(TextKey::ModeRange);
        result.projection = makeProjectionHud(settings, data);
    }
    if (materialEnabled) {
        auto const position = state.materialHudPosition();
        auto const convertStacks = state.materialHudStackConversion();
        auto const maxItems = state.materialHudMaxItems();
        MaterialHudCacheKey const key{
            state.materialHudRevision(),
            session.loaded->generation,
            session.selectedSource,
            position,
            convertStacks,
            maxItems,
            static_cast<int>(session.transform.layerAxis),
            static_cast<int>(session.transform.layerDisplayMode),
            session.transform.displayLayer,
            i18n::language(),
            editing,
        };
        if (pageDelta != 0 || cachedMaterialKey != key) {
            auto const materials = state.materialHudSnapshot();
            auto const changed = paging.setContext({session.loaded->generation,
                session.selectedSource, maxItems,
                key.axis, key.mode, key.layer});
            cachedMaterialPanel = editing && !materials.ready
                ? makeHudEditorContent(settings.position, position).materials
                : makeMaterialHud(materials, position, convertStacks, maxItems, &paging, editing || changed ? 0 : pageDelta);
            cachedMaterialKey = key;
            cachedMaterialKey->revision = materials.revision;
        }
        result.materials = cachedMaterialPanel;
    } else {
        cachedMaterialKey.reset();
    }
    if (editing && result.projection.lines.empty())
        result.projection = makeHudEditorContent(settings.position, state.materialHudPosition()).projection;
    return result;
}
}
