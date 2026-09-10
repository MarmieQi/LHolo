// LHolo - Fluent-style menu pages

#include "ui/MenuPages.h"

#include "structure/LayerDisplayTypes.h"
#include "ui/MenuWidgets.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include <imgui.h>

namespace lholo::ui {

char const* pageName(MenuPage page) {
    // Sized by kMenuPageCount. A page added without a label leaves the "no
    // message" sentinel behind, which the assertion below rejects: a missing
    // initializer alone would only be value-initialized, not diagnosed.
    static constexpr std::array<i18n::TextKey, kMenuPageCount> kPageKeys{
        i18n::TextKey::PageProjection,
        i18n::TextKey::PageCreateStructure,
        i18n::TextKey::PageTransform,
        i18n::TextKey::PageRender,
        i18n::TextKey::PageHud,
        i18n::TextKey::PageHotkeys,
        i18n::TextKey::PageInterface,
        i18n::TextKey::PageExperimental
    };
    constexpr auto allPagesNamed
        = [](std::array<i18n::TextKey, kMenuPageCount> const& keys) constexpr {
              for (auto key : keys) {
                  if (key == i18n::TextKey::None) return false;
              }
              return true;
          };
    static_assert(allPagesNamed(kPageKeys), "every MenuPage needs a navigation label");
    auto const index = static_cast<std::size_t>(page);
    return index < kPageKeys.size() ? i18n::tr(kPageKeys[index]) : "";
}

std::string materialPopupName() {
    return std::string{i18n::tr(i18n::TextKey::MaterialListTitle)} + "###LHoloMaterialList";
}

// Keep the navigation indicator independent from the page content.  This
// makes a page change feel connected even though the right-hand panel is
// rebuilt immediately for the newly selected page.
struct NavigationIndicator {
    float y{};
    float height{};
    bool  initialized{};
};

NavigationIndicator gNavigationIndicator;

using CaptureCoordinateText = std::array<std::array<char, 16>, 3>;

struct CaptureInputState {
    std::uint64_t        revision{std::numeric_limits<std::uint64_t>::max()};
    CaptureCoordinateText first{};
    CaptureCoordinateText second{};
};

CaptureInputState gCaptureInputState;

void setCaptureCoordinateText(CaptureCoordinateText& text, CapturePointModel const& point) {
    for (auto& coordinate : text) coordinate.fill('\0');
    if (!point.set) return;
    std::snprintf(text[0].data(), text[0].size(), "%d", point.x);
    std::snprintf(text[1].data(), text[1].size(), "%d", point.y);
    std::snprintf(text[2].data(), text[2].size(), "%d", point.z);
}

void syncCaptureInputState(MenuModel const& model) {
    if (gCaptureInputState.revision == model.captureRevision) return;
    setCaptureCoordinateText(gCaptureInputState.first, model.capture.first);
    setCaptureCoordinateText(gCaptureInputState.second, model.capture.second);
    gCaptureInputState.revision = model.captureRevision;
}

bool parseCaptureCoordinate(std::array<char, 16> const& text, int& value) {
    std::string_view const input{text.data()};
    if (input.empty()) return false;
    auto const [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
    return error == std::errc{} && end == input.data() + input.size();
}


int maxLayer(MenuModel const& model) {
    if (structure::layerAxisFromInt(model.layerAxis) == structure::LayerAxis::Material) {
        return std::max(0, model.materialCount - 1);
    }
    return structure::layerAxisFromInt(model.layerAxis) == structure::LayerAxis::X
        ? model.maxLayerX : model.maxLayerY;
}

void renderPathRow(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    auto input = [&] {
        if (!model.pathBuffer || model.pathBufferSize == 0) return;
        auto const browseWidth = ImGui::CalcTextSize(i18n::tr(i18n::TextKey::ButtonBrowse)).x
            + ImGui::GetStyle().FramePadding.x * 2.0f;
        auto const width = std::max(
            0.0f,
            (metrics.compact ? ImGui::GetContentRegionAvail().x : fieldWidth(metrics))
                - browseWidth - ImGui::GetStyle().ItemSpacing.x
        );
        ImGui::SetNextItemWidth(width);
        ImGui::InputText(
            "##StructurePath",
            model.pathBuffer,
            model.pathBufferSize,
            model.blockOpeningInput ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None
        );
        ImGui::SameLine();
        if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonBrowse)) && actions.browseStructure) {
            if (auto selected = actions.browseStructure(model.pathBuffer)) {
                std::snprintf(model.pathBuffer, model.pathBufferSize, "%s", selected->c_str());
            }
        }
    };
    if (metrics.compact) {
        ImGui::TextUnformatted(i18n::tr(i18n::TextKey::LabelStructurePath));
        input();
    } else {
        input();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(i18n::tr(i18n::TextKey::LabelStructurePath));
    }
}

void renderProjectionPage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    renderSection(
        "##ProjectionFile", i18n::tr(i18n::TextKey::SectionProjectionFile), metrics, [&] {
        ImGui::TextWrapped("%s", model.status.c_str());
        ImGui::Spacing();
        renderPathRow(model, actions, metrics);
        ImGui::Spacing();
        if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonLoad)) && actions.loadStructure) {
            actions.loadStructure(model.pathBuffer ? model.pathBuffer : "");
        }
        if (!metrics.compact) ImGui::SameLine();
        if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonCloseProjection)) && actions.closeProjection) {
            actions.closeProjection();
        }
        if (!metrics.compact) ImGui::SameLine();
        if (ImGui::Button(i18n::tr(i18n::TextKey::MaterialListTitle))) {
            if (actions.requestMaterials) actions.requestMaterials();
            // OpenPopup() is deferred to the page scope, where the modal is
            // rendered, so both calls use the same Dear ImGui ID stack.
            model.materialPopupRequested = true;
        }
        ImGui::Spacing();
        if (model.hasSavedProjection) {
            if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonRestoreLastProjection))
                && actions.restoreProjection) {
                actions.restoreProjection();
            }
            ImGui::SameLine();
            ImGui::TextDisabled(
                i18n::tr(i18n::TextKey::LabelSavedOrigin),
                model.savedAnchorX,
                model.savedAnchorY,
                model.savedAnchorZ
            );
        } else {
            ImGui::TextDisabled("%s", i18n::tr(i18n::TextKey::HintNoSavedProjection));
        }
    });

}

bool renderCapturePoint(
    char const* id,
    i18n::TextKey title,
    CapturePointModel& point,
    CapturePointId pointId,
    CaptureCoordinateText& coordinateText,
    float coordinateWidth,
    MenuActions const& actions,
    UiMetrics const& metrics
) {
    ImGui::PushID(id);
    ImGui::TextUnformatted(i18n::tr(title));
    constexpr std::array<char const*, 3> axisNames{"X", "Y", "Z"};
    for (std::size_t index = 0; index < coordinateText.size(); ++index) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(axisNames[index]);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(coordinateWidth);
        ImGui::PushID(static_cast<int>(index));
        ImGui::InputText(
            "##Coordinate",
            coordinateText[index].data(),
            coordinateText[index].size(),
            ImGuiInputTextFlags_CharsDecimal
        );
        ImGui::PopID();
        if (index + 1 < coordinateText.size()) ImGui::SameLine();
    }
    int x{};
    int y{};
    int z{};
    auto const valid = parseCaptureCoordinate(coordinateText[0], x)
        && parseCaptureCoordinate(coordinateText[1], y)
        && parseCaptureCoordinate(coordinateText[2], z);
    if (valid) {
        point.set = true;
        point.x = x;
        point.y = y;
        point.z = z;
    }
    if (!metrics.compact) ImGui::SameLine();
    if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonUsePlayerPosition))
        && actions.usePlayerCapturePosition) {
        actions.usePlayerCapturePosition(pointId);
    }
    if (!valid) {
        if (!metrics.compact) ImGui::SameLine();
        ImGui::TextDisabled("%s", i18n::tr(i18n::TextKey::LabelNotSet));
    }
    ImGui::PopID();
    return valid;
}

void renderCreateStructurePage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    renderSection(
        "##CaptureSource", i18n::tr(i18n::TextKey::SectionCaptureSource), metrics, [&] {
        model.capture.mode = 0;
        char const* modeNames[]{i18n::tr(i18n::TextKey::ComboCaptureModeClient)};
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(i18n::tr(i18n::TextKey::LabelCaptureMode));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(adaptiveComboWidth(modeNames, 1));
        if (ImGui::BeginCombo("##CaptureMode", modeNames[0])) {
            ImGui::Selectable(modeNames[0], true);
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("%s", i18n::tr(i18n::TextKey::HintCaptureClientOnly));
    });

    renderSection(
        "##CaptureSelection", i18n::tr(i18n::TextKey::SectionCaptureSelection), metrics, [&] {
        ImGui::TextWrapped("%s", model.captureStatus.c_str());
        syncCaptureInputState(model);
        auto coordinateWidth = ImGui::CalcTextSize("0").x;
        auto const measurePoint = [&](CaptureCoordinateText const& point) {
            for (auto const& coordinate : point) {
                coordinateWidth = std::max(
                    coordinateWidth,
                    ImGui::CalcTextSize(coordinate.data()).x
                );
            }
        };
        measurePoint(gCaptureInputState.first);
        measurePoint(gCaptureInputState.second);
        coordinateWidth += ImGui::GetStyle().FramePadding.x * 2.0f;
        auto const axisWidth = ImGui::CalcTextSize("X").x;
        auto const spacing = ImGui::GetStyle().ItemSpacing.x;
        auto const buttonWidth = metrics.compact
            ? 0.0f
            : ImGui::CalcTextSize(i18n::tr(i18n::TextKey::ButtonUsePlayerPosition)).x
                + ImGui::GetStyle().FramePadding.x * 2.0f;
        auto const maximumWidth = std::max(
            0.0f,
            (ImGui::GetContentRegionAvail().x - axisWidth * 3.0f - buttonWidth
                - spacing * (metrics.compact ? 5.0f : 6.0f)) / 3.0f
        );
        coordinateWidth = std::min(coordinateWidth, maximumWidth);
        ImGui::BeginDisabled(!model.captureWorldAvailable);
        auto const firstValid = renderCapturePoint(
            "First", i18n::TextKey::LabelCapturePoint1, model.capture.first, CapturePointId::First,
            gCaptureInputState.first, coordinateWidth, actions, metrics
        );
        ImGui::Spacing();
        auto const secondValid = renderCapturePoint(
            "Second", i18n::TextKey::LabelCapturePoint2, model.capture.second, CapturePointId::Second,
            gCaptureInputState.second, coordinateWidth, actions, metrics
        );
        ImGui::EndDisabled();

        if (firstValid && secondValid) {
            auto const sizeX = static_cast<std::uint64_t>(std::abs(
                static_cast<std::int64_t>(model.capture.second.x) - model.capture.first.x
            )) + 1;
            auto const sizeY = static_cast<std::uint64_t>(std::abs(
                static_cast<std::int64_t>(model.capture.second.y) - model.capture.first.y
            )) + 1;
            auto const sizeZ = static_cast<std::uint64_t>(std::abs(
                static_cast<std::int64_t>(model.capture.second.z) - model.capture.first.z
            )) + 1;
            auto const volume = sizeX * sizeY * sizeZ;
            ImGui::Text(i18n::tr(i18n::TextKey::LabelCaptureSize), sizeX, sizeY, sizeZ);
            ImGui::Text(i18n::tr(i18n::TextKey::LabelCaptureVolume), volume);
        }

        ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.55f));
        ImGui::BeginDisabled(!model.captureWorldAvailable);
        renderCheckboxRow(
            "##IncludeEntities",
            i18n::tr(i18n::TextKey::CheckboxIncludeEntities),
            model.capture.includeEntities,
            metrics
        );
        ImGui::EndDisabled();
        if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonClearSelection)) && actions.clearCapture) {
            actions.clearCapture();
        }
        if (!metrics.compact) ImGui::SameLine();
        ImGui::BeginDisabled(
            !model.captureWorldAvailable || !firstValid || !secondValid
        );
        if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonExportMcstructure)) && actions.exportCapture) {
            actions.exportCapture(model.capture);
        }
        ImGui::EndDisabled();
    });
}

namespace {

bool renderExperimentalInfoButton(char const* label) {
    auto const& style = ImGui::GetStyle();
    auto const  textSize = ImGui::CalcTextSize(label);
    auto const  buttonSize = ImVec2(
        textSize.x + style.FramePadding.x * 2.0f,
        textSize.y + style.FramePadding.y * 2.0f
    );

    // The CJK atlas' visible glyphs sit slightly below the centre of ImGui's
    // logical line box. Draw the label ourselves with a scale-aware optical
    // correction while retaining the normal Button hit box and interaction.
    bool const pressed = ImGui::Button("##ExperimentalInfo", buttonSize);
    auto const minimum = ImGui::GetItemRectMin();
    auto const maximum = ImGui::GetItemRectMax();
    auto const opticalOffset = ImGui::GetFontSize() * 0.07f;
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(
            minimum.x + (maximum.x - minimum.x - textSize.x) * 0.5f,
            minimum.y + (maximum.y - minimum.y - textSize.y) * 0.5f - opticalOffset
        ),
        ImGui::GetColorU32(ImGuiCol_Text),
        label
    );
    return pressed;
}

// The experimental-features consent modal. On "启用" it records consent and
// enables the feature the user was trying to turn on (pendingFeature: 1 manual,
// 2 easy, 3 range; -1 view-only, 0 none).
void renderExperimentalConsentModal(
    MenuModel& model, MenuActions const& actions, int& pendingFeature
) {
    auto const displaySize = ImGui::GetIO().DisplaySize;
    auto const popupWidth = std::min(
        displaySize.x * 0.90f,
        ImGui::GetFontSize() * 26.0f + ImGui::GetStyle().WindowPadding.x * 2.0f
    );
    ImGui::SetNextWindowPos(
        ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f)
    );
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(popupWidth, 0.0f),
        ImVec2(popupWidth, std::numeric_limits<float>::max())
    );
    if (!ImGui::BeginPopupModal(
            "##ExperimentalConsent", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoScrollbar
        )) {
        return;
    }
    ImGui::TextUnformatted(i18n::tr(i18n::TextKey::ModalTitleExperimental));
    ImGui::Separator();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextWrapped("%s", i18n::tr(i18n::TextKey::ExperimentalWarningIntro));
    ImGui::TextWrapped("%s", i18n::tr(i18n::TextKey::ExperimentalWarningAntiCheat));
    ImGui::TextWrapped("%s", i18n::tr(i18n::TextKey::ExperimentalWarningAllowed));
    ImGui::TextWrapped("%s", i18n::tr(i18n::TextKey::ExperimentalWarningConsequence));
    ImGui::TextWrapped("%s", i18n::tr(i18n::TextKey::ExperimentalWarningResponsibility));
    ImGui::TextWrapped("%s", i18n::tr(i18n::TextKey::ExperimentalWarningManualSafe));
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    if (model.experimentalConsent) {
        if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonClose))) {
            pendingFeature = 0;
            ImGui::CloseCurrentPopup();
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.20f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.26f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.16f, 0.18f, 1.0f));
        bool const enable = ImGui::Button(i18n::tr(i18n::TextKey::ButtonConsentEnable));
        ImGui::PopStyleColor(3);
        if (enable) {
            if (actions.giveExperimentalConsent) actions.giveExperimentalConsent();
            model.experimentalConsent = true;
            model.manualPlace = pendingFeature == 1;
            model.easyPlaceEnabled = pendingFeature == 2;
            model.rangeEnabled = pendingFeature == 3;
            pendingFeature = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonCancel))) {
            pendingFeature = 0;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

} // namespace

void renderExperimentalPage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    static int pendingConsentFeature = 0;
    renderSection(
        "##AssistedPlacement", i18n::tr(i18n::TextKey::SectionAssistedPlacement), metrics, [&] {
        // The说明 / consent entry sits at the very top and is coloured to stand out.
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.20f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.26f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.16f, 0.18f, 1.0f));
            char const* label = model.experimentalConsent
                ? i18n::tr(i18n::TextKey::ButtonExperimentalInfoView)
                : i18n::tr(i18n::TextKey::ButtonExperimentalInfoOpen);
            if (renderExperimentalInfoButton(label)) {
                pendingConsentFeature = -1;
                ImGui::OpenPopup("##ExperimentalConsent");
            }
            ImGui::PopStyleColor(3);
        }
        renderExperimentalConsentModal(model, actions, pendingConsentFeature);
        ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.25f));

        // A feature toggled on without consent is reverted and the popup opened.
        auto const gate = [&](bool& flag, int feature) {
            if (flag && !model.experimentalConsent) {
                flag = false;
                pendingConsentFeature = feature;
                ImGui::OpenPopup("##ExperimentalConsent");
            }
        };
        renderCheckboxRow(
            "##ManualPlace",
            i18n::tr(i18n::TextKey::CheckboxManualPlace),
            model.manualPlace,
            metrics
        );
        gate(model.manualPlace, 1);
        if (model.manualPlace) { model.easyPlaceEnabled = false; model.rangeEnabled = false; }
        renderCheckboxRow(
            "##EasyPlace",
            i18n::tr(i18n::TextKey::CheckboxEasyPlace),
            model.easyPlaceEnabled,
            metrics
        );
        gate(model.easyPlaceEnabled, 2);
        if (model.easyPlaceEnabled) { model.manualPlace = false; model.rangeEnabled = false; }
        renderCheckboxRow(
            "##RangePlace",
            i18n::tr(i18n::TextKey::CheckboxRangePlace),
            model.rangeEnabled,
            metrics
        );
        gate(model.rangeEnabled, 3);
        if (model.rangeEnabled) { model.easyPlaceEnabled = false; model.manualPlace = false; }
        ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.25f));
        if (model.manualPlace || model.easyPlaceEnabled || model.rangeEnabled) {
            char const* mode = model.manualPlace
                ? i18n::tr(i18n::TextKey::ModeManual)
                : model.easyPlaceEnabled
                    ? i18n::tr(i18n::TextKey::ModeEasy)
                    : i18n::tr(i18n::TextKey::ModeRange);
            ImGui::TextWrapped(i18n::tr(i18n::TextKey::HintModeEnabled), mode);
        } else if (!model.experimentalConsent) {
            ImGui::TextDisabled(
                "%s", i18n::tr(i18n::TextKey::HintAssistedDisabledByConsent)
            );
        } else {
            ImGui::TextDisabled("%s", i18n::tr(i18n::TextKey::HintAssistedDisabled));
        }
        renderSteppedInt(
            "PlacementRadius",
            i18n::tr(i18n::TextKey::LabelPlacementRadius),
            model.placementRadius,
            1,
            4,
            metrics
        );
        renderSteppedInt(
            "AutoPlacementBreakCooldown",
            i18n::tr(i18n::TextKey::LabelAutoPlacementCooldown),
            model.autoPlacementBreakCooldownSeconds,
            0,
            60,
            metrics
        );
    });
}

void renderTransformPage(MenuModel& model, UiMetrics const& metrics) {
    renderSection("##Transform", i18n::tr(i18n::TextKey::SectionTransform), metrics, [&] {
        static char const* rotationNames[]{"0°", "90°", "180°", "270°"};
        char const* mirrorNames[]{
            i18n::tr(i18n::TextKey::ComboMirrorNone), "X", "Z"
        };
        auto const transformComboWidth = std::max(
            adaptiveComboWidth(rotationNames, 4),
            adaptiveComboWidth(mirrorNames, 3)
        );
        renderValueRow(i18n::tr(i18n::TextKey::LabelRotation), metrics, [&] {
            ImGui::SetNextItemWidth(transformComboWidth);
            ImGui::Combo("##Rotation", &model.rotation, rotationNames, 4);
        });
        renderValueRow(i18n::tr(i18n::TextKey::LabelMirror), metrics, [&] {
            ImGui::SetNextItemWidth(transformComboWidth);
            ImGui::Combo("##Mirror", &model.mirror, mirrorNames, 3);
        });
        ImGui::Separator();
        renderSteppedInt("OffsetX", i18n::tr(i18n::TextKey::LabelOffsetX), model.offsetX, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), metrics);
        renderSteppedInt("OffsetY", i18n::tr(i18n::TextKey::LabelOffsetY), model.offsetY, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), metrics);
        renderSteppedInt("OffsetZ", i18n::tr(i18n::TextKey::LabelOffsetZ), model.offsetZ, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), metrics);
    });
}

void renderRenderPage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    renderSection(
        "##ProjectionStyle", i18n::tr(i18n::TextKey::SectionProjectionStyle), metrics, [&] {
        auto opacity = static_cast<int>(std::lround(model.opacity * 100.0f));
        renderNumericValueRow(i18n::tr(i18n::TextKey::LabelOpacity), metrics, [&] {
            if (ImGui::InputInt("##Opacity", &opacity, 0, 0)) {
                model.opacity = static_cast<float>(std::clamp(opacity, 0, 100)) / 100.0f;
            }
        });
        renderCheckboxRow(
            "##RenderBounds",
            i18n::tr(i18n::TextKey::CheckboxRenderBounds),
            model.structureBoundsEnabled,
            metrics
        );
    });

    renderSection(
        "##LayerSettings", i18n::tr(i18n::TextKey::SectionLayerSettings), metrics, [&] {
        char const* axisNames[]{
            i18n::tr(i18n::TextKey::ComboLayerAxisY),
            i18n::tr(i18n::TextKey::ComboLayerAxisX),
            i18n::tr(i18n::TextKey::ComboLayerAxisMaterial)
        };
        char const* layerModeNames[]{
            i18n::tr(i18n::TextKey::ComboRangeAll),
            i18n::tr(i18n::TextKey::ComboRangeSingle),
            i18n::tr(i18n::TextKey::ComboRangeUpToCurrent),
            i18n::tr(i18n::TextKey::ComboRangeFromCurrent)
        };
        char const* materialModeNames[]{
            i18n::tr(i18n::TextKey::ComboMaterialAll),
            i18n::tr(i18n::TextKey::ComboMaterialSingle),
            i18n::tr(i18n::TextKey::ComboMaterialUpToCurrent),
            i18n::tr(i18n::TextKey::ComboMaterialFromCurrent)
        };
        renderValueRow(i18n::tr(i18n::TextKey::LabelLayerAxis), metrics, [&] {
            ImGui::SetNextItemWidth(adaptiveComboWidth(axisNames, 3));
            if (ImGui::Combo("##LayerAxis", &model.layerAxis, axisNames, 3)) {
                model.displayLayer = std::clamp(model.displayLayer, 0, maxLayer(model));
            }
        });
        renderValueRow(i18n::tr(i18n::TextKey::LabelDisplayRange), metrics, [&] {
            auto const modeNames = structure::layerAxisFromInt(model.layerAxis)
                    == structure::LayerAxis::Material
                ? materialModeNames : layerModeNames;
            ImGui::SetNextItemWidth(adaptiveComboWidth(modeNames, 4));
            ImGui::Combo("##LayerMode", &model.layerDisplayMode, modeNames, 4);
        });
        if (structure::layerAxisFromInt(model.layerAxis) == structure::LayerAxis::Material) {
            auto materialNumber = model.displayLayer + 1;
            renderSteppedInt(
                "DisplayMaterial", i18n::tr(i18n::TextKey::LabelCurrentMaterial), materialNumber,
                1, maxLayer(model) + 1, metrics
            );
            model.displayLayer = materialNumber - 1;
        } else {
            renderSteppedInt(
                "DisplayLayer", i18n::tr(i18n::TextKey::LabelCurrentLayer), model.displayLayer,
                0, maxLayer(model), metrics
            );
        }
        if (!metrics.compact) ImGui::SameLine(0.0f, metrics.gap * 0.55f);
        ImGui::PushTextWrapPos(-1.0f);
        if (structure::layerAxisFromInt(model.layerAxis) == structure::LayerAxis::Material) {
            ImGui::TextDisabled(
                i18n::tr(i18n::TextKey::HintMaterialOrder), maxLayer(model) + 1
            );
        } else {
            ImGui::TextDisabled(
                i18n::tr(i18n::TextKey::HintLayerZeroBased),
                maxLayer(model),
                structure::layerAxisFromInt(model.layerAxis) == structure::LayerAxis::X ? "X" : "Y"
            );
        }
        ImGui::PopTextWrapPos();
    });

    renderSection(
        "##CorrectionStyle", i18n::tr(i18n::TextKey::SectionCorrectionStyle), metrics, [&] {
        auto fill = static_cast<int>(std::lround(model.correctionFillOpacity * 100.0f));
        renderNumericValueRow(i18n::tr(i18n::TextKey::LabelCorrectionFill), metrics, [&] {
            if (ImGui::InputInt("##CorrectionFill", &fill, 0, 0)) {
                model.correctionFillOpacity = static_cast<float>(std::clamp(fill, 0, 100)) / 100.0f;
            }
        });
        auto outline = static_cast<int>(std::lround(model.correctionOutlineOpacity * 100.0f));
        renderNumericValueRow(i18n::tr(i18n::TextKey::LabelCorrectionOutline), metrics, [&] {
            if (ImGui::InputInt("##CorrectionOutline", &outline, 0, 0)) {
                model.correctionOutlineOpacity = static_cast<float>(std::clamp(outline, 0, 100)) / 100.0f;
            }
        });
        if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonResetCorrectionStyle))) {
            if (actions.resetCorrectionStyle) actions.resetCorrectionStyle();
            model.correctionFillOpacity = 0.15f;
            model.correctionOutlineOpacity = 1.0f;
        }
    });

    renderSection("##SeeThrough", i18n::tr(i18n::TextKey::SectionSeeThrough), metrics, [&] {
        renderCheckboxRow(
            "##CorrectionSeeThrough",
            i18n::tr(i18n::TextKey::CheckboxCorrectionSeeThrough),
            model.correctionSeeThrough,
            metrics
        );
        renderCheckboxRow(
            "##MissingSeeThrough",
            i18n::tr(i18n::TextKey::CheckboxMissingSeeThrough),
            model.missingSeeThrough,
            metrics
        );
    });
}

void renderHotkeysPage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    renderSection("##Hotkeys", i18n::tr(i18n::TextKey::SectionHotkeys), metrics, [&] {
        auto maxLabelWidth = 0.0f;
        auto maxBindingWidth = ImGui::CalcTextSize(i18n::tr(i18n::TextKey::HintPressKeys)).x;
        for (auto const& hotkey : model.hotkeys) {
            maxLabelWidth = std::max(maxLabelWidth, ImGui::CalcTextSize(hotkey.label.c_str()).x);
            maxBindingWidth = std::max(maxBindingWidth, ImGui::CalcTextSize(hotkey.display.c_str()).x);
        }
        auto const bindingPadding = ImGui::GetStyle().FramePadding.x * 2.0f;
        auto const preferredBindingWidth = maxBindingWidth + bindingPadding;
        auto const rowSpacing = metrics.gap * 0.70f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, rowSpacing));
        for (auto const& hotkey : model.hotkeys) {
            ImGui::PushID(static_cast<int>(hotkey.id));
            auto const rowStart = ImGui::GetCursorPosX();
            if (metrics.compact) {
                ImGui::TextUnformatted(hotkey.label.c_str());
            } else {
                // Put the action first: scanning the left edge now explains
                // what a shortcut does before showing its current binding.
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(hotkey.label.c_str());
                ImGui::SameLine();
                // Reserve one label column for every row.  Binding fields no
                // longer shift horizontally with the length of their action.
                auto const controlX = rowStart + maxLabelWidth + metrics.gap * 1.4f;
                ImGui::SetCursorPosX(controlX);
            }
            auto const clearWidth = ImGui::CalcTextSize(i18n::tr(i18n::TextKey::ButtonClearHotkey)).x
                + ImGui::GetStyle().FramePadding.x * 2.0f;
            auto const available = ImGui::GetContentRegionAvail().x;
            auto const controlSpacing = ImGui::GetStyle().ItemSpacing.x;
            auto const requiredWidth = preferredBindingWidth + clearWidth + controlSpacing;
            auto const totalWidth = std::min(available, requiredWidth);
            auto const bindWidth = std::max(0.0f, totalWidth - clearWidth - controlSpacing);
            auto const label = hotkey.capturing
                ? i18n::tr(i18n::TextKey::HintPressKeys)
                : hotkey.display.c_str();
            if (ImGui::Button(label, ImVec2(bindWidth, 0.0f)) && !hotkey.capturing && actions.beginHotkeyCapture) {
                actions.beginHotkeyCapture(hotkey.id);
            }
            ImGui::SameLine();
            if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonResetHotkey)) && actions.resetHotkey) {
                actions.resetHotkey(hotkey.id);
            }
            ImGui::SameLine();
            if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonClearHotkey)) && actions.clearHotkey) {
                actions.clearHotkey(hotkey.id);
            }
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonResetAllHotkeys)) && actions.resetHotkeys) {
            actions.resetHotkeys();
        }
        ImGui::TextDisabled("%s", i18n::tr(i18n::TextKey::HintChatCommand));
    });
}

void renderHudPage(MenuModel& model, UiMetrics const& metrics) {
    renderSection("##Hud", i18n::tr(i18n::TextKey::SectionHud), metrics, [&] {
        renderCheckboxRow(
            "##HudEnabled", i18n::tr(i18n::TextKey::CheckboxHudEnabled), model.hudEnabled, metrics
        );
        ImGui::BeginDisabled(!model.hudEnabled);
        char const* positions[]{
            i18n::tr(i18n::TextKey::CornerTopLeft),
            i18n::tr(i18n::TextKey::CornerBottomLeft),
            i18n::tr(i18n::TextKey::CornerTopRight),
            i18n::tr(i18n::TextKey::CornerBottomRight)
        };
        renderValueRow(i18n::tr(i18n::TextKey::LabelHudPosition), metrics, [&] {
            ImGui::SetNextItemWidth(adaptiveComboWidth(positions, 4));
            ImGui::Combo("##HudPosition", &model.hudPosition, positions, 4);
        });
        renderCheckboxRow(
            "##HudFileName",
            i18n::tr(i18n::TextKey::CheckboxHudFileName),
            model.hudShowFileName,
            metrics
        );
        renderCheckboxRow(
            "##HudLayer",
            i18n::tr(i18n::TextKey::CheckboxHudLayer),
            model.hudShowLayer,
            metrics
        );
        renderCheckboxRow(
            "##HudOverallProgress",
            i18n::tr(i18n::TextKey::CheckboxHudOverallProgress),
            model.hudShowOverallProgress,
            metrics
        );
        renderCheckboxRow(
            "##HudProgress",
            i18n::tr(i18n::TextKey::CheckboxHudProgress),
            model.hudShowProgress,
            metrics
        );
        renderCheckboxRow(
            "##HudProjectedBlockName",
            i18n::tr(i18n::TextKey::CheckboxHudProjectedBlockName),
            model.hudShowProjectedBlockName,
            metrics
        );
        renderCheckboxRow(
            "##HudWrongState",
            i18n::tr(i18n::TextKey::CheckboxHudWrongState),
            model.hudShowWrongState,
            metrics
        );
        renderCheckboxRow(
            "##HudWrongType",
            i18n::tr(i18n::TextKey::CheckboxHudWrongType),
            model.hudShowWrongType,
            metrics
        );
        renderCheckboxRow(
            "##HudExtraBlocks",
            i18n::tr(i18n::TextKey::CheckboxHudExtraBlocks),
            model.hudShowExtraBlocks,
            metrics
        );
        ImGui::EndDisabled();
        ImGui::Separator();
        renderCheckboxRow(
            "##MaterialHudEnabled",
            i18n::tr(i18n::TextKey::CheckboxMaterialHudEnabled),
            model.materialHudEnabled,
            metrics
        );
        ImGui::BeginDisabled(!model.materialHudEnabled);
        renderValueRow(i18n::tr(i18n::TextKey::LabelMaterialHudPosition), metrics, [&] {
            ImGui::SetNextItemWidth(adaptiveComboWidth(positions, 4));
            ImGui::Combo("##MaterialHudPosition", &model.materialHudPosition, positions, 4);
        });
        ImGui::EndDisabled();
    });
}

void renderInterfacePage(MenuModel& model, UiMetrics const& metrics) {
    renderSection(
        "##InterfaceSettings", i18n::tr(i18n::TextKey::SectionInterfaceSettings), metrics, [&] {
        renderSteppedFloat(
            "UiScaleValue", i18n::tr(i18n::TextKey::LabelUiScale), model.uiScale,
            1.0f, 5.0f, 0.1f, metrics
        );
        char const* languageNames[]{
            i18n::languageName(i18n::Language::SimplifiedChinese),
            i18n::languageName(i18n::Language::English)
        };
        // Label first, like the stepped row above: this page reads left to
        // right, so the value follows the name instead of preceding it.
        if (metrics.compact) {
            ImGui::TextUnformatted(i18n::tr(i18n::TextKey::LabelLanguage));
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(i18n::tr(i18n::TextKey::LabelLanguage));
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 0.55f);
        }
        ImGui::SetNextItemWidth(adaptiveComboWidth(languageNames, i18n::kLanguageCount));
        // The selection is persisted by applyStructureMenuModel; the next frame
        // then rebuilds every string from the new language.
        ImGui::Combo("##Language", &model.language, languageNames, i18n::kLanguageCount);
    });
}

void renderMaterialPopup(MenuModel const& model, UiMetrics const& metrics) {
    // The material list has a little more breathing room than the regular
    // menu: its table is intentionally 1.5x the original logical footprint.
    constexpr float popupDensity = 1.20f;
    constexpr float itemColumnWeight = 0.38f;
    constexpr float typeColumnWeight = 0.42f;
    constexpr float totalColumnWeight = 0.20f;
    // Measure the actual table content.  A fixed width multiplier makes a
    // seven-row list look like a wide banner and leaves unused space below.
    auto nameWidth = ImGui::CalcTextSize(i18n::tr(i18n::TextKey::ColumnItem)).x;
    auto typeWidth = ImGui::CalcTextSize(i18n::tr(i18n::TextKey::ColumnIdentifier)).x;
    auto countWidth = ImGui::CalcTextSize(i18n::tr(i18n::TextKey::ColumnTotal)).x;
    auto const materialName = [](MaterialRow const& row) {
        return materialDisplayName(row.displayName, row.nameKey);
    };
    for (auto const& item : model.materials) {
        nameWidth = std::max(nameWidth, ImGui::CalcTextSize(materialName(item)).x);
        typeWidth = std::max(typeWidth, ImGui::CalcTextSize(item.typeName.c_str()).x);
        auto const countText = std::to_string(item.count);
        countWidth = std::max(countWidth, ImGui::CalcTextSize(countText.c_str()).x);
    }
    auto const cellPadding = ImGui::GetStyle().CellPadding.x * 2.0f;
    auto const tableWidth = std::max(
        (nameWidth + cellPadding) / itemColumnWeight,
        std::max(
            (typeWidth + cellPadding) / typeColumnWeight,
            (countWidth + cellPadding) / totalColumnWeight
        )
    );
    auto const closeLabel = i18n::tr(i18n::TextKey::ButtonClose);
    auto const closeWidth = ImGui::CalcTextSize(closeLabel).x
        + ImGui::GetStyle().FramePadding.x * 2.0f;
    auto const titleWidth = ImGui::CalcTextSize(i18n::tr(i18n::TextKey::MaterialListTitle)).x
        + closeWidth + metrics.gap;
    auto const popupWidth = std::min(
        std::max(tableWidth + metrics.outerPadding * 2.35f, titleWidth + metrics.outerPadding * 2.35f)
            * popupDensity,
        metrics.viewport.x * 0.86f
    );

    auto const line = ImGui::GetTextLineHeightWithSpacing();
    auto const rowHeight = (ImGui::GetTextLineHeight() + metrics.gap * 0.55f) * popupDensity;
    auto const listContentHeight = model.materials.empty()
        ? rowHeight
        : line + rowHeight * static_cast<float>(model.materials.size());
    auto const listHeight = model.hasLoadedStructure
        ? std::min(listContentHeight, metrics.viewport.y * 0.60f)
        : 0.0f;
    auto const popupHeight = std::min(
        listHeight + ImGui::GetTextLineHeight() * 2.0f * popupDensity
            + metrics.outerPadding * 2.35f * popupDensity + metrics.gap * 3.0f,
        metrics.viewport.y * 0.84f
    );
    auto const popupSize = ImVec2(
        popupWidth,
        popupHeight
    );
    ImGui::SetNextWindowSize(popupSize, ImGuiCond_Always);
    auto const popupName = materialPopupName();
    if (!ImGui::BeginPopupModal(
            popupName.c_str(),
            nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
        )) return;

    ImGui::TextUnformatted(i18n::tr(i18n::TextKey::MaterialListTitle));
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(),
        ImGui::GetWindowWidth() - closeWidth - metrics.sectionPadding
    ));
    if (ImGui::Button(closeLabel)) ImGui::CloseCurrentPopup();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.35f));

    if (!model.hasLoadedStructure) {
        ImGui::TextDisabled("%s", i18n::tr(i18n::TextKey::StatusNotLoaded));
    } else {
        std::uint64_t total{};
        for (auto const& item : model.materials) {
            if (std::numeric_limits<std::uint64_t>::max() - total < item.count) {
                total = std::numeric_limits<std::uint64_t>::max();
                break;
            }
            total += item.count;
        }
        ImGui::Text(
            i18n::tr(i18n::TextKey::LabelMaterialSummary),
            static_cast<unsigned long long>(total),
            model.materials.size()
        );
        ImGui::Separator();
        // The popup header and Close button are fixed. Only this child consumes
        // the wheel; its visual scrollbar stays hidden to match the rest of the
        // menu, while the non-scrollable parent prevents wheel propagation.
        auto const materialAreaHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
        if (ImGui::BeginChild(
                "##MaterialList",
                ImVec2(0.0f, materialAreaHeight),
                false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings
            )) {
            if (model.materials.empty()) {
                ImGui::TextDisabled(
                    "%s", i18n::tr(i18n::TextKey::HintNoPlaceableMaterials)
                );
            } else if (ImGui::BeginTable(
                           "##MaterialTable", 3,
                           ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg
                               | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings
                       )) {
                ImGui::TableSetupColumn(
                    i18n::tr(i18n::TextKey::ColumnItem),
                    ImGuiTableColumnFlags_WidthStretch,
                    itemColumnWeight
                );
                ImGui::TableSetupColumn(
                    i18n::tr(i18n::TextKey::ColumnIdentifier),
                    ImGuiTableColumnFlags_WidthStretch,
                    typeColumnWeight
                );
                ImGui::TableSetupColumn(
                    i18n::tr(i18n::TextKey::ColumnTotal),
                    ImGuiTableColumnFlags_WidthStretch,
                    totalColumnWeight
                );
                ImGui::TableHeadersRow();
                auto renderCenteredCell = [&](char const* text) {
                    auto const cellWidth = ImGui::GetContentRegionAvail().x;
                    auto const textSize = ImGui::CalcTextSize(text, nullptr, false, cellWidth);
                    auto const verticalOffset = std::max(
                        0.0f,
                        (rowHeight - textSize.y) * 0.5f
                    );
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + verticalOffset);
                    if (textSize.y > ImGui::GetTextLineHeight() + 0.5f) {
                        ImGui::TextWrapped("%s", text);
                    } else {
                        ImGui::TextUnformatted(text);
                    }
                };
                for (auto const& item : model.materials) {
                    ImGui::TableNextRow(
                        ImGuiTableRowFlags_None,
                        (ImGui::GetTextLineHeight() + metrics.gap * 0.55f) * popupDensity
                    );
                    ImGui::TableSetColumnIndex(0);
                    renderCenteredCell(materialName(item));
                    ImGui::TableSetColumnIndex(1);
                    renderCenteredCell(item.typeName.c_str());
                    ImGui::TableSetColumnIndex(2);
                    auto const countText = std::to_string(item.count);
                    renderCenteredCell(countText.c_str());
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndPopup();
}

void renderNavigation(MenuModel& model, UiMetrics const& metrics) {
    ImVec2 indicatorMin{};
    ImVec2 indicatorMax{};
    for (std::size_t index = 0; index < kMenuPageCount; ++index) {
        auto const page = static_cast<MenuPage>(index);
        auto const* name = pageName(page);
        auto const selected = model.page == page;
        ImGui::PushID(static_cast<int>(index));
        auto const min = ImGui::GetCursorScreenPos();
        auto const height = std::max(
            ImGui::GetFrameHeight() * 1.08f,
            ImGui::GetTextLineHeight() + metrics.sectionPadding * 0.72f
        );
        auto const size = ImVec2(ImGui::GetContentRegionAvail().x, height);
        if (ImGui::InvisibleButton("##NavigationItem", size)) model.page = page;
        auto const max = ImGui::GetItemRectMax();
        auto const hovered = ImGui::IsItemHovered();
        auto* drawList = ImGui::GetWindowDrawList();
        if (selected || hovered) {
            drawList->AddRectFilled(
                min,
                max,
                selected ? IM_COL32(51, 51, 54, 255) : IM_COL32(43, 43, 46, 255),
                metrics.rounding * 0.8f
            );
        }
        if (selected) {
            indicatorMin = min;
            indicatorMax = max;
        }
        auto const textSize = ImGui::CalcTextSize(name);
        auto const textInset = metrics.sectionPadding * 1.25f;
        drawList->AddText(
            ImGui::GetFont(),
            ImGui::GetFontSize(),
            ImVec2(min.x + textInset, min.y + (height - textSize.y) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text),
            name
        );
        ImGui::PopID();
    }

    // Animate only the blue selection strip.  The selected page itself is
    // updated immediately, so input and configuration changes never wait for
    // the visual transition.  Exponential interpolation is frame-rate
    // independent and also behaves well when UI scale changes at runtime.
    if (indicatorMax.y > indicatorMin.y) {
        auto const targetY = indicatorMin.y;
        auto const targetHeight = indicatorMax.y - indicatorMin.y;
        if (!gNavigationIndicator.initialized) {
            gNavigationIndicator = {targetY, targetHeight, true};
        } else {
            auto const progress = 1.0f - std::exp(-18.0f * ImGui::GetIO().DeltaTime);
            gNavigationIndicator.y += (targetY - gNavigationIndicator.y) * progress;
            gNavigationIndicator.height += (targetHeight - gNavigationIndicator.height) * progress;
        }

        auto const stripInset = std::max(2.0f, metrics.sectionPadding * 0.22f);
        auto const stripWidth = std::max(4.0f, metrics.scale * 4.0f);
        auto const stripTop = gNavigationIndicator.y + stripInset;
        auto const stripBottom = std::max(stripTop, gNavigationIndicator.y + gNavigationIndicator.height - stripInset);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(indicatorMin.x + stripInset, stripTop),
            ImVec2(indicatorMin.x + stripInset + stripWidth, stripBottom),
            IM_COL32(0, 120, 212, 255),
            stripWidth * 0.5f
        );
    }
}

} // namespace lholo::ui
