// LHolo - Interface text keys
//
// Single source of truth for every user-visible interface string. The list
// below is defined once through an X-macro and expands into both the TextKey
// enum and a parallel table of stable string identifiers ("page.projection")
// used by the language JSON files in src/i18n/lang/.
//
// Stable-key contract: the identifiers are part of the published translation
// format. Never rename an identifier; deprecate the entry and add a new one.
//
// Layering: this header is a leaf. It must not include any other LHolo module,
// nor any Minecraft/LeviLamina header.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// LHolo_TEXT_KEY(identifier, enumName, comment)
#define LHOLO_TEXT_KEY_LIST                                                                              \
    /* Explicit "no message". Keeps platform value 0 out of the real string                              \
       table, so a default-constructed i18n::Message renders as nothing instead                          \
       of picking up whichever entry happens to be declared first. Must stay the                         \
       first entry. */                                                                                   \
    LHOLO_TEXT_KEY("none", None, "")                                                                     \
                                                                                                         \
    /* Shell */                                                                                          \
    LHOLO_TEXT_KEY("menu.close", MenuClose, "")                                                          \
                                                                                                         \
    /* Navigation pages */                                                                               \
    LHOLO_TEXT_KEY("page.projection", PageProjection, "")                                                \
    LHOLO_TEXT_KEY("page.createStructure", PageCreateStructure, "")                                      \
    LHOLO_TEXT_KEY("page.transform", PageTransform, "")                                                  \
    LHOLO_TEXT_KEY("page.render", PageRender, "")                                                        \
    LHOLO_TEXT_KEY("page.hud", PageHud, "")                                                              \
    LHOLO_TEXT_KEY("page.hotkeys", PageHotkeys, "")                                                      \
    LHOLO_TEXT_KEY("page.interface", PageInterface, "")                                                  \
    LHOLO_TEXT_KEY("page.experimental", PageExperimental, "")                                            \
                                                                                                         \
    /* Projection page */                                                                                \
    LHOLO_TEXT_KEY("section.projectionFile", SectionProjectionFile, "")                                  \
    LHOLO_TEXT_KEY("label.structurePath", LabelStructurePath, "")                                        \
    LHOLO_TEXT_KEY("button.browse", ButtonBrowse, "")                                                    \
    LHOLO_TEXT_KEY("button.load", ButtonLoad, "")                                                        \
    LHOLO_TEXT_KEY("button.closeProjection", ButtonCloseProjection, "")                                  \
    LHOLO_TEXT_KEY("material.listTitle", MaterialListTitle, "")                                          \
    LHOLO_TEXT_KEY("button.restoreLastProjection", ButtonRestoreLastProjection, "")                      \
    LHOLO_TEXT_KEY("label.savedOrigin", LabelSavedOrigin, "")                                            \
    LHOLO_TEXT_KEY("hint.noSavedProjection", HintNoSavedProjection, "")                                  \
                                                                                                         \
    /* Create-structure page */                                                                          \
    LHOLO_TEXT_KEY("section.captureSource", SectionCaptureSource, "")                                    \
    LHOLO_TEXT_KEY("label.captureMode", LabelCaptureMode, "")                                            \
    LHOLO_TEXT_KEY("combo.captureModeClient", ComboCaptureModeClient, "")                                \
    LHOLO_TEXT_KEY("hint.captureClientOnly", HintCaptureClientOnly, "")                                  \
    LHOLO_TEXT_KEY("section.captureSelection", SectionCaptureSelection, "")                              \
    LHOLO_TEXT_KEY("label.capturePoint1", LabelCapturePoint1, "")                                        \
    LHOLO_TEXT_KEY("label.capturePoint2", LabelCapturePoint2, "")                                        \
    LHOLO_TEXT_KEY("button.usePlayerPosition", ButtonUsePlayerPosition, "")                              \
    LHOLO_TEXT_KEY("label.notSet", LabelNotSet, "")                                                      \
    LHOLO_TEXT_KEY("label.captureSize", LabelCaptureSize, "")                                            \
    LHOLO_TEXT_KEY("label.captureVolume", LabelCaptureVolume, "")                                        \
    LHOLO_TEXT_KEY("checkbox.includeEntities", CheckboxIncludeEntities, "")                              \
    LHOLO_TEXT_KEY("button.clearSelection", ButtonClearSelection, "")                                    \
    LHOLO_TEXT_KEY("button.exportMcstructure", ButtonExportMcstructure, "")                              \
                                                                                                         \
    /* Transform page */                                                                                 \
    LHOLO_TEXT_KEY("section.transform", SectionTransform, "")                                            \
    LHOLO_TEXT_KEY("label.rotation", LabelRotation, "")                                                  \
    LHOLO_TEXT_KEY("label.mirror", LabelMirror, "")                                                      \
    LHOLO_TEXT_KEY("combo.mirrorNone", ComboMirrorNone, "")                                              \
    LHOLO_TEXT_KEY("label.offsetX", LabelOffsetX, "")                                                    \
    LHOLO_TEXT_KEY("label.offsetY", LabelOffsetY, "")                                                    \
    LHOLO_TEXT_KEY("label.offsetZ", LabelOffsetZ, "")                                                    \
                                                                                                         \
    /* Rendering page */                                                                                 \
    LHOLO_TEXT_KEY("section.projectionStyle", SectionProjectionStyle, "")                                \
    LHOLO_TEXT_KEY("label.opacity", LabelOpacity, "")                                                    \
    LHOLO_TEXT_KEY("checkbox.renderBounds", CheckboxRenderBounds, "")                                    \
    LHOLO_TEXT_KEY("section.layerSettings", SectionLayerSettings, "")                                    \
    LHOLO_TEXT_KEY("label.layerAxis", LabelLayerAxis, "")                                                \
    LHOLO_TEXT_KEY("combo.layerAxisY", ComboLayerAxisY, "")                                              \
    LHOLO_TEXT_KEY("combo.layerAxisX", ComboLayerAxisX, "")                                              \
    LHOLO_TEXT_KEY("combo.layerAxisMaterial", ComboLayerAxisMaterial, "")                                \
    LHOLO_TEXT_KEY("label.displayRange", LabelDisplayRange, "")                                          \
    LHOLO_TEXT_KEY("combo.rangeAll", ComboRangeAll, "")                                                  \
    LHOLO_TEXT_KEY("combo.rangeSingle", ComboRangeSingle, "")                                            \
    LHOLO_TEXT_KEY("combo.rangeUpToCurrent", ComboRangeUpToCurrent, "")                                  \
    LHOLO_TEXT_KEY("combo.rangeFromCurrent", ComboRangeFromCurrent, "")                                  \
    LHOLO_TEXT_KEY("combo.materialAll", ComboMaterialAll, "")                                            \
    LHOLO_TEXT_KEY("combo.materialSingle", ComboMaterialSingle, "")                                      \
    LHOLO_TEXT_KEY("combo.materialUpToCurrent", ComboMaterialUpToCurrent, "")                            \
    LHOLO_TEXT_KEY("combo.materialFromCurrent", ComboMaterialFromCurrent, "")                            \
    LHOLO_TEXT_KEY("label.currentMaterial", LabelCurrentMaterial, "")                                    \
    LHOLO_TEXT_KEY("label.currentLayer", LabelCurrentLayer, "")                                          \
    LHOLO_TEXT_KEY("hint.materialOrder", HintMaterialOrder, "")                                          \
    LHOLO_TEXT_KEY("hint.layerZeroBased", HintLayerZeroBased, "")                                        \
    LHOLO_TEXT_KEY("section.correctionStyle", SectionCorrectionStyle, "")                                \
    LHOLO_TEXT_KEY("label.correctionFill", LabelCorrectionFill, "")                                      \
    LHOLO_TEXT_KEY("label.correctionOutline", LabelCorrectionOutline, "")                                \
    LHOLO_TEXT_KEY("button.resetCorrectionStyle", ButtonResetCorrectionStyle, "")                        \
    LHOLO_TEXT_KEY("section.seeThrough", SectionSeeThrough, "")                                          \
    LHOLO_TEXT_KEY("checkbox.correctionSeeThrough", CheckboxCorrectionSeeThrough, "")                    \
    LHOLO_TEXT_KEY("checkbox.missingSeeThrough", CheckboxMissingSeeThrough, "")                          \
    /* Closing note on the render page, not tied to a single control. */                                 \
    LHOLO_TEXT_KEY("hint.vibrantVisuals", HintVibrantVisuals, "")                                        \
                                                                                                         \
    /* HUD page */                                                                                       \
    LHOLO_TEXT_KEY("section.hud", SectionHud, "")                                                        \
    LHOLO_TEXT_KEY("checkbox.hudEnabled", CheckboxHudEnabled, "")                                        \
    LHOLO_TEXT_KEY("label.hudPosition", LabelHudPosition, "")                                            \
    LHOLO_TEXT_KEY("corner.topLeft", CornerTopLeft, "")                                                  \
    LHOLO_TEXT_KEY("corner.bottomLeft", CornerBottomLeft, "")                                            \
    LHOLO_TEXT_KEY("corner.topRight", CornerTopRight, "")                                                \
    LHOLO_TEXT_KEY("corner.bottomRight", CornerBottomRight, "")                                          \
    LHOLO_TEXT_KEY("checkbox.hudFileName", CheckboxHudFileName, "")                                      \
    LHOLO_TEXT_KEY("checkbox.hudLayer", CheckboxHudLayer, "")                                            \
    LHOLO_TEXT_KEY("checkbox.hudOverallProgress", CheckboxHudOverallProgress, "")                        \
    LHOLO_TEXT_KEY("checkbox.hudProgress", CheckboxHudProgress, "")                                      \
    LHOLO_TEXT_KEY("checkbox.hudProjectedBlockName", CheckboxHudProjectedBlockName, "")                  \
    LHOLO_TEXT_KEY("checkbox.hudWrongState", CheckboxHudWrongState, "")                                  \
    LHOLO_TEXT_KEY("checkbox.hudWrongType", CheckboxHudWrongType, "")                                    \
    LHOLO_TEXT_KEY("checkbox.hudExtraBlocks", CheckboxHudExtraBlocks, "")                                \
    LHOLO_TEXT_KEY("checkbox.materialHudEnabled", CheckboxMaterialHudEnabled, "")                        \
    LHOLO_TEXT_KEY("label.materialHudPosition", LabelMaterialHudPosition, "")                            \
                                                                                                         \
    /* Hotkeys page */                                                                                   \
    LHOLO_TEXT_KEY("section.hotkeys", SectionHotkeys, "")                                                \
    LHOLO_TEXT_KEY("hint.pressKeys", HintPressKeys, "")                                                  \
    LHOLO_TEXT_KEY("button.resetHotkey", ButtonResetHotkey, "")                                          \
    LHOLO_TEXT_KEY("button.clearHotkey", ButtonClearHotkey, "")                                          \
    LHOLO_TEXT_KEY("button.resetAllHotkeys", ButtonResetAllHotkeys, "")                                  \
    LHOLO_TEXT_KEY("hint.chatCommand", HintChatCommand, "")                                              \
    /* Fixed-gesture input on the hotkeys page: the trigger key is fixed to                              \
       Alt, so it is a switch instead of a rebindable slot. */                                           \
    LHOLO_TEXT_KEY("checkbox.altWheelOffset", CheckboxAltWheelOffset, "")                                \
    LHOLO_TEXT_KEY("hotkey.openMenu", HotkeyOpenMenu, "")                                                \
    /* Move rows are ordered like HotkeyId's move slots: left, right, forward,                           \
       backward, up, down. */                                                                            \
    LHOLO_TEXT_KEY("hotkey.moveLeft", HotkeyMoveLeft, "")                                                \
    LHOLO_TEXT_KEY("hotkey.moveRight", HotkeyMoveRight, "")                                              \
    LHOLO_TEXT_KEY("hotkey.moveForward", HotkeyMoveForward, "")                                          \
    LHOLO_TEXT_KEY("hotkey.moveBackward", HotkeyMoveBackward, "")                                        \
    LHOLO_TEXT_KEY("hotkey.moveUp", HotkeyMoveUp, "")                                                    \
    LHOLO_TEXT_KEY("hotkey.moveDown", HotkeyMoveDown, "")                                                \
    LHOLO_TEXT_KEY("hotkey.layerIncrease", HotkeyLayerIncrease, "")                                      \
    LHOLO_TEXT_KEY("hotkey.layerDecrease", HotkeyLayerDecrease, "")                                      \
    LHOLO_TEXT_KEY("hotkey.loadProjection", HotkeyLoadProjection, "")                                    \
    LHOLO_TEXT_KEY("hotkey.closeProjection", HotkeyCloseProjection, "")                                  \
    LHOLO_TEXT_KEY("key.notSet", KeyNotSet, "")                                                          \
    LHOLO_TEXT_KEY("key.mouseMiddle", KeyMouseMiddle, "")                                                \
    LHOLO_TEXT_KEY("key.mouseSide1", KeyMouseSide1, "")                                                  \
    LHOLO_TEXT_KEY("key.mouseSide2", KeyMouseSide2, "")                                                  \
                                                                                                         \
    /* Experimental page */                                                                              \
    LHOLO_TEXT_KEY("section.assistedPlacement", SectionAssistedPlacement, "")                            \
    LHOLO_TEXT_KEY("button.experimentalInfoOpen", ButtonExperimentalInfoOpen, "")                        \
    LHOLO_TEXT_KEY("button.experimentalInfoView", ButtonExperimentalInfoView, "")                        \
    LHOLO_TEXT_KEY("modal.titleExperimental", ModalTitleExperimental, "")                                \
    LHOLO_TEXT_KEY("experimental.warningIntro", ExperimentalWarningIntro, "")                            \
    LHOLO_TEXT_KEY("experimental.warningAntiCheat", ExperimentalWarningAntiCheat, "")                    \
    LHOLO_TEXT_KEY("experimental.warningAllowed", ExperimentalWarningAllowed, "")                        \
    LHOLO_TEXT_KEY("experimental.warningConsequence", ExperimentalWarningConsequence, "")                \
    LHOLO_TEXT_KEY("experimental.warningResponsibility", ExperimentalWarningResponsibility, "")          \
    LHOLO_TEXT_KEY("experimental.warningManualSafe", ExperimentalWarningManualSafe, "")                  \
    LHOLO_TEXT_KEY("button.consentEnable", ButtonConsentEnable, "")                                      \
    LHOLO_TEXT_KEY("button.cancel", ButtonCancel, "")                                                    \
    LHOLO_TEXT_KEY("button.close", ButtonClose, "")                                                      \
    LHOLO_TEXT_KEY("checkbox.manualPlace", CheckboxManualPlace, "")                                      \
    LHOLO_TEXT_KEY("checkbox.easyPlace", CheckboxEasyPlace, "")                                          \
    LHOLO_TEXT_KEY("checkbox.rangePlace", CheckboxRangePlace, "")                                        \
    LHOLO_TEXT_KEY("mode.manual", ModeManual, "")                                                        \
    LHOLO_TEXT_KEY("mode.easy", ModeEasy, "")                                                            \
    LHOLO_TEXT_KEY("mode.range", ModeRange, "")                                                          \
    LHOLO_TEXT_KEY("hint.modeEnabled", HintModeEnabled, "")                                              \
    LHOLO_TEXT_KEY("hint.assistedDisabledByConsent", HintAssistedDisabledByConsent, "")                  \
    LHOLO_TEXT_KEY("hint.assistedDisabled", HintAssistedDisabled, "")                                    \
    LHOLO_TEXT_KEY("label.placementRadius", LabelPlacementRadius, "")                                    \
    LHOLO_TEXT_KEY("label.autoPlacementCooldown", LabelAutoPlacementCooldown, "")                        \
                                                                                                         \
    /* Interface settings page */                                                                        \
    LHOLO_TEXT_KEY("section.interfaceSettings", SectionInterfaceSettings, "")                            \
    LHOLO_TEXT_KEY("label.uiScale", LabelUiScale, "")                                                    \
    LHOLO_TEXT_KEY("label.language", LabelLanguage, "")                                                  \
                                                                                                         \
    /* Material list popup */                                                                            \
    LHOLO_TEXT_KEY("material.water", MaterialWater, "")                                                  \
    LHOLO_TEXT_KEY("material.lava", MaterialLava, "")                                                    \
    LHOLO_TEXT_KEY("status.notLoaded", StatusNotLoaded, "")                                              \
    LHOLO_TEXT_KEY("label.materialSummary", LabelMaterialSummary, "")                                    \
    LHOLO_TEXT_KEY("hint.noPlaceableMaterials", HintNoPlaceableMaterials, "")                            \
    LHOLO_TEXT_KEY("column.item", ColumnItem, "")                                                        \
    LHOLO_TEXT_KEY("column.identifier", ColumnIdentifier, "")                                            \
    LHOLO_TEXT_KEY("column.total", ColumnTotal, "")                                                      \
                                                                                                         \
    /* Projection HUD */                                                                                 \
    LHOLO_TEXT_KEY("hud.fileName", HudFileName, "")                                                      \
    LHOLO_TEXT_KEY("hud.materialFilterAll", HudMaterialFilterAll, "")                                    \
    LHOLO_TEXT_KEY("hud.materialFilterSingle", HudMaterialFilterSingle, "")                              \
    LHOLO_TEXT_KEY("hud.materialFilterUpTo", HudMaterialFilterUpTo, "")                                  \
    LHOLO_TEXT_KEY("hud.materialFilterRange", HudMaterialFilterRange, "")                                \
    LHOLO_TEXT_KEY("hud.rangeAll", HudRangeAll, "")                                                      \
    LHOLO_TEXT_KEY("hud.currentLayer", HudCurrentLayer, "")                                              \
    LHOLO_TEXT_KEY("hud.rangeFromZero", HudRangeFromZero, "")                                            \
    LHOLO_TEXT_KEY("hud.rangeBetween", HudRangeBetween, "")                                              \
    LHOLO_TEXT_KEY("hud.overallProgress", HudOverallProgress, "")                                        \
    LHOLO_TEXT_KEY("hud.buildProgress", HudBuildProgress, "")                                            \
    LHOLO_TEXT_KEY("hud.wrongState", HudWrongState, "")                                                  \
    LHOLO_TEXT_KEY("hud.wrongType", HudWrongType, "")                                                    \
    LHOLO_TEXT_KEY("hud.extraBlocks", HudExtraBlocks, "")                                                \
    LHOLO_TEXT_KEY("hud.projectedBlock", HudProjectedBlock, "")                                          \
    LHOLO_TEXT_KEY("hud.placementMode", HudPlacementMode, "")                                            \
                                                                                                         \
    /* Material HUD */                                                                                   \
    LHOLO_TEXT_KEY("materialHud.title", MaterialHudTitle, "")                                            \
    LHOLO_TEXT_KEY("materialHud.scanning", MaterialHudScanning, "")                                      \
    LHOLO_TEXT_KEY("materialHud.complete", MaterialHudComplete, "")                                      \
    LHOLO_TEXT_KEY("materialHud.more", MaterialHudMore, "")                                              \
                                                                                                         \
    /* File dialogs (Win32 common dialog filters) */                                                     \
    LHOLO_TEXT_KEY("dialog.filterProjection", DialogFilterProjection, "")                                \
    LHOLO_TEXT_KEY("dialog.filterBedrock", DialogFilterBedrock, "")                                      \
    LHOLO_TEXT_KEY("dialog.filterLitematica", DialogFilterLitematica, "")                                \
    LHOLO_TEXT_KEY("dialog.filterAll", DialogFilterAll, "")                                              \
                                                                                                         \
    /* Session status messages (stored as i18n::Message, rendered at the boundary) */                    \
    LHOLO_TEXT_KEY("status.pathEmpty", StatusPathEmpty, "")                                              \
    LHOLO_TEXT_KEY("status.loadFailed", StatusLoadFailed, "")                                            \
    LHOLO_TEXT_KEY("status.restoreFailed", StatusRestoreFailed, "")                                      \
    LHOLO_TEXT_KEY("status.restoredPending", StatusRestoredPending, "")                                  \
    LHOLO_TEXT_KEY("status.loaded", StatusLoaded, "")                                                    \
    LHOLO_TEXT_KEY("status.worldExited", StatusWorldExited, "")                                          \
    LHOLO_TEXT_KEY("status.projectionClosed", StatusProjectionClosed, "")                                \
    LHOLO_TEXT_KEY("captureStatus.needTwoPoints", CaptureStatusNeedTwoPoints, "")                        \
    LHOLO_TEXT_KEY("captureStatus.selectionReady", CaptureStatusSelectionReady, "")                      \
    LHOLO_TEXT_KEY("captureStatus.needOtherPoint", CaptureStatusNeedOtherPoint, "")                      \
    LHOLO_TEXT_KEY("captureStatus.noWorld", CaptureStatusNoWorld, "")                                    \
    LHOLO_TEXT_KEY("captureStatus.singleplayerUnsupported", CaptureStatusSingleplayerUnsupported, "")    \
    LHOLO_TEXT_KEY("captureStatus.needBothPoints", CaptureStatusNeedBothPoints, "")                      \
    LHOLO_TEXT_KEY("captureStatus.regionNotLoaded", CaptureStatusRegionNotLoaded, "")                    \
    LHOLO_TEXT_KEY("captureStatus.templateFailed", CaptureStatusTemplateFailed, "")                      \
    LHOLO_TEXT_KEY("captureStatus.writeFailed", CaptureStatusWriteFailed, "")                            \
    LHOLO_TEXT_KEY("captureStatus.exported", CaptureStatusExported, "")                                  \
    LHOLO_TEXT_KEY("captureStatus.point1Recorded", CaptureStatusPoint1Recorded, "")                      \
    LHOLO_TEXT_KEY("captureStatus.point2Recorded", CaptureStatusPoint2Recorded, "")                      \
                                                                                                         \
    /* Transient action hints */                                                                         \
    LHOLO_TEXT_KEY("actionHint.projectionSuspended", ActionHintProjectionSuspended, "")                  \
    LHOLO_TEXT_KEY("actionHint.projectionRestored", ActionHintProjectionRestored, "")                    \
    LHOLO_TEXT_KEY("actionHint.worldExited", ActionHintWorldExited, "")                                  \
    LHOLO_TEXT_KEY("actionHint.loadProjection", ActionHintLoadProjection, "")                            \
    LHOLO_TEXT_KEY("actionHint.closeProjection", ActionHintCloseProjection, "")                          \
    LHOLO_TEXT_KEY("actionHint.noMatchingItem", ActionHintNoMatchingItem, "")                            \
    LHOLO_TEXT_KEY("actionHint.manualModeBlocked", ActionHintManualModeBlocked, "")

namespace lholo::i18n {

enum class TextKey : std::uint16_t {
#define LHOLO_TEXT_KEY(identifier, enumName, comment) enumName,
    LHOLO_TEXT_KEY_LIST
#undef LHOLO_TEXT_KEY
        Count,
};

inline constexpr std::size_t kTextKeyCount = static_cast<std::size_t>(TextKey::Count);

// Stable string identifiers, parallel to the TextKey enum values. The embedded
// JSON language files address entries through these identifiers.
inline constexpr std::array<std::string_view, kTextKeyCount> kTextKeyIds{{
#define LHOLO_TEXT_KEY(identifier, enumName, comment) identifier,
    LHOLO_TEXT_KEY_LIST
#undef LHOLO_TEXT_KEY
}};

} // namespace lholo::i18n
