#include "ui/hud/HudContent.h"
#include "input/MaterialHudPageRequest.h"
#include "ui/hud/editor/HudEditor.h"
#include "structure/StructureLoader.h"
#include "structure/IndependentTaskFiles.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"
#include "structure/StructurePaths.h"
#include "projection/Projection.h"
#include "place/PlaceHelper.h"
#include "oreui/OreUiScreen.h"
#include <algorithm>

namespace lholo::ui::hud {
HudContent readHudContent() {
    // Owned exclusively by the HUD render path; input only supplies intent.
    static MaterialHudPaging paging;
    auto const pageDelta = input::consumeMaterialHudPageRequest();
    HudContent result;
    bool const editing = hudLayoutEditorActive();
    if (!editing && (structure::isGuiVisible() || oreui::isMenuOnTop())) return result;
    auto& state = structure::detail::StructureUiState::getInstance();
    auto settings = state.hud();
    if (editing) settings.enabled = true;
    auto const materialEnabled = editing || state.materialHudEnabled();
    if (!materialEnabled) paging = {};
    if (!settings.enabled && !materialEnabled) return result;
    auto const session = structure::detail::StructureSession::getInstance().snapshot();
    if (!session.loaded || projection::isDimensionSuspended()
        || (session.loaded->composed && !structure::hasVisibleTaskFiles(*session.loaded))) {
        paging = {};
        return editing ? makeHudEditorContent(settings.position, state.materialHudPosition()) : result;
    }
    result.context = session.loaded->generation;
    if (settings.enabled) {
        ProjectionHudData data;
        auto editor=structure::detail::StructureSession::getInstance().editorStructure();
        data.fileName = structure::detail::pathToUtf8((session.loaded->composed
            ? structure::detail::pathFromUtf8(session.lastPath) : session.loaded->sourcePath).filename());
        data.axis = session.transform.layerAxis;
        data.mode = session.transform.layerDisplayMode;
        data.layer = session.transform.displayLayer;
        data.maxLayer = data.axis == structure::LayerAxis::Material
            ? std::max(0, editor ? static_cast<int>(editor->materialCount) - 1 : 0)
            : data.axis == structure::LayerAxis::X ? session.maxLayerX : session.maxLayerY;
        if (settings.showOverallProgress || settings.showProgress || settings.showWrongState || settings.showWrongType || settings.showExtraBlocks)
            data.progress = projection::getBuildProgress();
        if (settings.showProjectedBlockName) {
            auto const aimed = place::getAimedProjectedBlock();
            data.aimedBlock = aimed.name;
            data.aimedBlockIconId = aimed.iconId;
            data.aimedBlockIconItem = aimed.iconItem;
        }
        using i18n::TextKey;
        if (place::isManualMode()) data.placementMode = i18n::tr(TextKey::ModeManual);
        else if (place::isEnabled()) data.placementMode = i18n::tr(TextKey::ModeEasy);
        else if (place::isRangeEnabled()) data.placementMode = i18n::tr(TextKey::ModeRange);
        result.projection = makeProjectionHud(settings, data);
    }
    if (materialEnabled) {
        auto const materials = state.materialHudSnapshot();
        auto const changed = paging.setContext({session.loaded->generation,
            structure::detail::StructureSession::getInstance().selectedSource(), state.materialHudMaxItems(),
            static_cast<int>(session.transform.layerAxis), static_cast<int>(session.transform.layerDisplayMode), session.transform.displayLayer});
        result.materials = editing && !materials.ready
            ? makeHudEditorContent(settings.position, state.materialHudPosition()).materials
            : makeMaterialHud(materials, state.materialHudPosition(), state.materialHudStackConversion(), state.materialHudMaxItems(), &paging, editing || changed ? 0 : pageDelta);
    }
    if (editing && result.projection.lines.empty())
        result.projection = makeHudEditorContent(settings.position, state.materialHudPosition()).projection;
    return result;
}
}
