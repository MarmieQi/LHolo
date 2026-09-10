// LHolo - Interface text keys
//
// Single source of truth for every user-visible interface string. The
// language tables in TextTable.h are indexed by this enum, and both tables are
// checked at compile time to cover every key exactly once, so adding a key
// without translating it fails the build instead of showing a raw key.
//
// Layering: this header is a leaf. It must not include any other LHolo module,
// nor any Minecraft/LeviLamina header.

#pragma once

#include <cstddef>
#include <cstdint>

namespace lholo::i18n {

enum class TextKey : std::uint16_t {
    // Explicit "no message". Keeps platform value 0 out of the real string
    // table, so a default-constructed i18n::Message renders as nothing instead
    // of picking up whichever entry happens to be declared first. Must stay the
    // first enumerator.
    None,

    // Shell
    MenuClose,

    // Navigation pages
    PageProjection,
    PageCreateStructure,
    PageTransform,
    PageRender,
    PageHud,
    PageHotkeys,
    PageInterface,
    PageExperimental,

    // Projection page
    SectionProjectionFile,
    LabelStructurePath,
    ButtonBrowse,
    ButtonLoad,
    ButtonCloseProjection,
    MaterialListTitle,
    ButtonRestoreLastProjection,
    LabelSavedOrigin,
    HintNoSavedProjection,

    // Create-structure page
    SectionCaptureSource,
    LabelCaptureMode,
    ComboCaptureModeClient,
    HintCaptureClientOnly,
    SectionCaptureSelection,
    LabelCapturePoint1,
    LabelCapturePoint2,
    ButtonUsePlayerPosition,
    LabelNotSet,
    LabelCaptureSize,
    LabelCaptureVolume,
    CheckboxIncludeEntities,
    ButtonClearSelection,
    ButtonExportMcstructure,

    // Transform page
    SectionTransform,
    LabelRotation,
    LabelMirror,
    ComboMirrorNone,
    LabelOffsetX,
    LabelOffsetY,
    LabelOffsetZ,

    // Rendering page
    SectionProjectionStyle,
    LabelOpacity,
    CheckboxRenderBounds,
    SectionLayerSettings,
    LabelLayerAxis,
    ComboLayerAxisY,
    ComboLayerAxisX,
    ComboLayerAxisMaterial,
    LabelDisplayRange,
    ComboRangeAll,
    ComboRangeSingle,
    ComboRangeUpToCurrent,
    ComboRangeFromCurrent,
    ComboMaterialAll,
    ComboMaterialSingle,
    ComboMaterialUpToCurrent,
    ComboMaterialFromCurrent,
    LabelCurrentMaterial,
    LabelCurrentLayer,
    HintMaterialOrder,
    HintLayerZeroBased,
    SectionCorrectionStyle,
    LabelCorrectionFill,
    LabelCorrectionOutline,
    ButtonResetCorrectionStyle,
    SectionSeeThrough,
    CheckboxCorrectionSeeThrough,
    CheckboxMissingSeeThrough,

    // HUD page
    SectionHud,
    CheckboxHudEnabled,
    LabelHudPosition,
    CornerTopLeft,
    CornerBottomLeft,
    CornerTopRight,
    CornerBottomRight,
    CheckboxHudFileName,
    CheckboxHudLayer,
    CheckboxHudOverallProgress,
    CheckboxHudProgress,
    CheckboxHudProjectedBlockName,
    CheckboxHudWrongState,
    CheckboxHudWrongType,
    CheckboxHudExtraBlocks,
    CheckboxMaterialHudEnabled,
    LabelMaterialHudPosition,

    // Hotkeys page
    SectionHotkeys,
    HintPressKeys,
    ButtonResetHotkey,
    ButtonClearHotkey,
    ButtonResetAllHotkeys,
    HintChatCommand,
    HotkeyOpenMenu,
    HotkeyMoveXMinus,
    HotkeyMoveXPlus,
    HotkeyMoveZMinus,
    HotkeyMoveZPlus,
    HotkeyMoveYPlus,
    HotkeyMoveYMinus,
    HotkeyLayerIncrease,
    HotkeyLayerDecrease,
    HotkeyLoadProjection,
    HotkeyCloseProjection,
    KeyNotSet,
    KeyMouseMiddle,
    KeyMouseSide1,
    KeyMouseSide2,

    // Experimental page
    SectionAssistedPlacement,
    ButtonExperimentalInfoOpen,
    ButtonExperimentalInfoView,
    ModalTitleExperimental,
    ExperimentalWarningIntro,
    ExperimentalWarningAntiCheat,
    ExperimentalWarningAllowed,
    ExperimentalWarningConsequence,
    ExperimentalWarningResponsibility,
    ExperimentalWarningManualSafe,
    ButtonConsentEnable,
    ButtonCancel,
    ButtonClose,
    CheckboxManualPlace,
    CheckboxEasyPlace,
    CheckboxRangePlace,
    ModeManual,
    ModeEasy,
    ModeRange,
    HintModeEnabled,
    HintAssistedDisabledByConsent,
    HintAssistedDisabled,
    LabelPlacementRadius,
    LabelAutoPlacementCooldown,

    // Interface settings page
    SectionInterfaceSettings,
    LabelUiScale,
    LabelLanguage,

    // Material list popup
    MaterialWater,
    MaterialLava,
    StatusNotLoaded,
    LabelMaterialSummary,
    HintNoPlaceableMaterials,
    ColumnItem,
    ColumnIdentifier,
    ColumnTotal,

    // Projection HUD
    HudFileName,
    HudMaterialFilterAll,
    HudMaterialFilterSingle,
    HudMaterialFilterUpTo,
    HudMaterialFilterRange,
    HudRangeAll,
    HudCurrentLayer,
    HudRangeFromZero,
    HudRangeBetween,
    HudOverallProgress,
    HudBuildProgress,
    HudWrongState,
    HudWrongType,
    HudExtraBlocks,
    HudProjectedBlock,
    HudPlacementMode,

    // Material HUD
    MaterialHudTitle,
    MaterialHudScanning,
    MaterialHudComplete,
    MaterialHudMore,

    // File dialogs (Win32 common dialog filters)
    DialogFilterProjection,
    DialogFilterBedrock,
    DialogFilterLitematica,
    DialogFilterAll,

    // Session status messages (stored as i18n::Message, rendered at the boundary)
    StatusPathEmpty,
    StatusLoadFailed,
    StatusRestoreFailed,
    StatusRestoredPending,
    StatusLoaded,
    StatusWorldExited,
    StatusProjectionClosed,
    CaptureStatusNeedTwoPoints,
    CaptureStatusSelectionReady,
    CaptureStatusNeedOtherPoint,
    CaptureStatusNoWorld,
    CaptureStatusSingleplayerUnsupported,
    CaptureStatusNeedBothPoints,
    CaptureStatusRegionNotLoaded,
    CaptureStatusTemplateFailed,
    CaptureStatusWriteFailed,
    CaptureStatusExported,
    CaptureStatusPoint1Recorded,
    CaptureStatusPoint2Recorded,

    // Transient action hints
    ActionHintProjectionSuspended,
    ActionHintProjectionRestored,
    ActionHintWorldExited,
    ActionHintLoadProjection,
    ActionHintCloseProjection,
    ActionHintNoMatchingItem,
    ActionHintManualModeBlocked,

    Count
};

inline constexpr std::size_t kTextKeyCount = static_cast<std::size_t>(TextKey::Count);

} // namespace lholo::i18n
