// LHolo - Interface text tables
//
// Compile-time language tables. Every entry names its own key, so a missing or
// duplicated key is a build error rather than a runtime fallback. Patterns may
// contain printf placeholders; keys used through i18n::Message only ever use
// %s because message arguments are pre-formatted strings.
//
// Layering: leaf header, same rules as TextKeys.h.

#pragma once

#include "i18n/TextKeys.h"

#include <array>

namespace lholo::i18n {

struct TextEntry {
    TextKey     key{};
    char const* text{};
};

using TextTable = std::array<char const*, kTextKeyCount>;

// Fills the lookup table by key. Slots never assigned stay null and are
// rejected by isComplete() below.
constexpr TextTable buildTable(std::array<TextEntry, kTextKeyCount> const& entries) {
    TextTable table{};
    for (auto const& entry : entries) {
        auto const index = static_cast<std::size_t>(entry.key);
        if (index < kTextKeyCount) table[index] = entry.text;
    }
    return table;
}

constexpr bool isComplete(std::array<TextEntry, kTextKeyCount> const& entries) {
    std::array<bool, kTextKeyCount> seen{};
    for (auto const& entry : entries) {
        auto const index = static_cast<std::size_t>(entry.key);
        if (index >= kTextKeyCount || seen[index] || entry.text == nullptr) return false;
        seen[index] = true;
    }
    for (bool present : seen) {
        if (!present) return false;
    }
    return true;
}

inline constexpr std::array<TextEntry, kTextKeyCount> kSimplifiedChineseEntries{{
    {TextKey::None, ""},

    {TextKey::MenuClose, "关闭菜单"},

    {TextKey::PageProjection, "投影"},
    {TextKey::PageCreateStructure, "创建结构"},
    {TextKey::PageTransform, "结构变换"},
    {TextKey::PageRender, "渲染设置"},
    {TextKey::PageHud, "HUD 信息显示"},
    {TextKey::PageHotkeys, "快捷键"},
    {TextKey::PageInterface, "界面设置"},
    {TextKey::PageExperimental, "实验性功能"},

    {TextKey::SectionProjectionFile, "投影文件"},
    {TextKey::LabelStructurePath, "结构文件路径（.mcstructure / .litematic）"},
    {TextKey::ButtonBrowse, "浏览"},
    {TextKey::ButtonLoad, "加载"},
    {TextKey::ButtonCloseProjection, "关闭投影"},
    {TextKey::MaterialListTitle, "材料清单"},
    {TextKey::ButtonRestoreLastProjection, "恢复上次投影"},
    {TextKey::LabelSavedOrigin, "原点 X %d  Y %d  Z %d"},
    {TextKey::HintNoSavedProjection, "没有可恢复的上次投影记录"},

    {TextKey::SectionCaptureSource, "创建结构"},
    {TextKey::LabelCaptureMode, "模式"},
    {TextKey::ComboCaptureModeClient, "客户端模式"},
    {TextKey::HintCaptureClientOnly, "客户端模式只能保存当前已加载的世界范围，且容器内容和实体数据可能不完整"},
    {TextKey::SectionCaptureSelection, "结构选区"},
    {TextKey::LabelCapturePoint1, "点 1"},
    {TextKey::LabelCapturePoint2, "点 2"},
    {TextKey::ButtonUsePlayerPosition, "使用玩家当前位置"},
    {TextKey::LabelNotSet, "未设置"},
    {TextKey::LabelCaptureSize, "尺寸：%llu × %llu × %llu"},
    {TextKey::LabelCaptureVolume, "方块总数：%llu"},
    {TextKey::CheckboxIncludeEntities, "包含实体"},
    {TextKey::ButtonClearSelection, "清除选区"},
    {TextKey::ButtonExportMcstructure, "导出 .mcstructure"},

    {TextKey::SectionTransform, "结构变换"},
    {TextKey::LabelRotation, "结构旋转"},
    {TextKey::LabelMirror, "结构镜像"},
    {TextKey::ComboMirrorNone, "无"},
    {TextKey::LabelOffsetX, "X 轴偏移"},
    {TextKey::LabelOffsetY, "Y 轴偏移"},
    {TextKey::LabelOffsetZ, "Z 轴偏移"},

    {TextKey::SectionProjectionStyle, "投影显示设置"},
    {TextKey::LabelOpacity, "投影透明度（范围 0～100）"},
    {TextKey::CheckboxRenderBounds, "显示整体结构边框"},
    {TextKey::SectionLayerSettings, "分层显示设置"},
    {TextKey::LabelLayerAxis, "分层轴"},
    {TextKey::ComboLayerAxisY, "Y 轴（水平分层）"},
    {TextKey::ComboLayerAxisX, "X 轴（纵向切片）"},
    {TextKey::ComboLayerAxisMaterial, "按材料"},
    {TextKey::LabelDisplayRange, "显示范围"},
    {TextKey::ComboRangeAll, "完整结构"},
    {TextKey::ComboRangeSingle, "单层"},
    {TextKey::ComboRangeUpToCurrent, "当前层及以下"},
    {TextKey::ComboRangeFromCurrent, "当前层及以上"},
    {TextKey::ComboMaterialAll, "全部材料"},
    {TextKey::ComboMaterialSingle, "当前序号"},
    {TextKey::ComboMaterialUpToCurrent, "第 1 项至当前项"},
    {TextKey::ComboMaterialFromCurrent, "当前项至最后一项"},
    {TextKey::LabelCurrentMaterial, "当前材料序号"},
    {TextKey::LabelCurrentLayer, "当前层"},
    {TextKey::HintMaterialOrder, "1 - %d（按材料清单顺序）"},
    {TextKey::HintLayerZeroBased, "0 - %d（结构 %s 轴起点为 0）"},
    {TextKey::SectionCorrectionStyle, "纠错提示样式"},
    {TextKey::LabelCorrectionFill, "纠错填充透明度（范围 0～100）"},
    {TextKey::LabelCorrectionOutline, "纠错描边透明度（范围 0～100）"},
    {TextKey::ButtonResetCorrectionStyle, "恢复默认纠错样式"},
    {TextKey::SectionSeeThrough, "穿透显示"},
    {TextKey::CheckboxCorrectionSeeThrough, "错误标记穿透显示（X 光）"},
    {TextKey::CheckboxMissingSeeThrough, "未放置标记穿透显示（X 光）"},

    {TextKey::SectionHud, "HUD 信息显示"},
    {TextKey::CheckboxHudEnabled, "启用 HUD"},
    {TextKey::LabelHudPosition, "HUD 位置"},
    {TextKey::CornerTopLeft, "左上"},
    {TextKey::CornerBottomLeft, "左下"},
    {TextKey::CornerTopRight, "右上"},
    {TextKey::CornerBottomRight, "右下"},
    {TextKey::CheckboxHudFileName, "显示投影文件名"},
    {TextKey::CheckboxHudLayer, "显示渲染层信息"},
    {TextKey::CheckboxHudOverallProgress, "显示总体进度"},
    {TextKey::CheckboxHudProgress, "显示建造进度"},
    {TextKey::CheckboxHudProjectedBlockName, "显示投影方块名称"},
    {TextKey::CheckboxHudWrongState, "显示朝向错误"},
    {TextKey::CheckboxHudWrongType, "显示放置错误"},
    {TextKey::CheckboxHudExtraBlocks, "显示多余方块"},
    {TextKey::CheckboxMaterialHudEnabled, "显示缺失材料"},
    {TextKey::LabelMaterialHudPosition, "缺失材料位置"},

    {TextKey::SectionHotkeys, "快捷键"},
    {TextKey::HintPressKeys, "请按组合键"},
    {TextKey::ButtonResetHotkey, "恢复默认"},
    {TextKey::ButtonClearHotkey, "清除"},
    {TextKey::ButtonResetAllHotkeys, "恢复默认快捷键"},
    {TextKey::HintChatCommand, "可在聊天栏输入 LHolo 打开投影菜单"},
    {TextKey::HotkeyOpenMenu, "打开投影菜单"},
    {TextKey::HotkeyMoveXMinus, "结构偏移 X -1"},
    {TextKey::HotkeyMoveXPlus, "结构偏移 X +1"},
    {TextKey::HotkeyMoveZMinus, "结构偏移 Z -1"},
    {TextKey::HotkeyMoveZPlus, "结构偏移 Z +1"},
    {TextKey::HotkeyMoveYPlus, "结构偏移 Y +1"},
    {TextKey::HotkeyMoveYMinus, "结构偏移 Y -1"},
    {TextKey::HotkeyLayerIncrease, "上一层"},
    {TextKey::HotkeyLayerDecrease, "下一层"},
    {TextKey::HotkeyLoadProjection, "加载投影"},
    {TextKey::HotkeyCloseProjection, "关闭投影"},
    {TextKey::KeyNotSet, "未设置"},
    {TextKey::KeyMouseMiddle, "鼠标中键"},
    {TextKey::KeyMouseSide1, "鼠标侧键1"},
    {TextKey::KeyMouseSide2, "鼠标侧键2"},

    {TextKey::SectionAssistedPlacement, "辅助放置"},
    {TextKey::ButtonExperimentalInfoOpen, "⚠ 实验性功能说明（点此阅读并启用）"},
    {TextKey::ButtonExperimentalInfoView, "查看实验性功能说明"},
    {TextKey::ModalTitleExperimental, "⚠ 实验性功能 · 使用前请阅读"},
    {TextKey::ExperimentalWarningIntro, "轻松放置 / 手动放置 / 范围放置 属于实验性辅助功能，会自动或半自动地帮你放置投影中的方块。"},
    {TextKey::ExperimentalWarningAntiCheat, "由于这些功能会程序化地模拟方块放置，部分服务器的反作弊系统可能将其判定为作弊 / 外挂行为。"},
    {TextKey::ExperimentalWarningAllowed, "· 请仅在单人世界，或已获得服主明确许可的服务器上使用。"},
    {TextKey::ExperimentalWarningConsequence, "· 在未经许可的服务器上使用，可能导致被踢出、封禁账号等后果。"},
    {TextKey::ExperimentalWarningResponsibility, "· 一切风险与后果由使用者自行承担，作者与本模组概不负责。"},
    {TextKey::ExperimentalWarningManualSafe, "手动逐格搭建始终是安全的做法；辅助放置仅为提升效率的实验性工具。"},
    {TextKey::ButtonConsentEnable, "我已了解风险，启用"},
    {TextKey::ButtonCancel, "取消"},
    {TextKey::ButtonClose, "关闭"},
    {TextKey::CheckboxManualPlace, "手动放置（右键放置·按住连放）"},
    {TextKey::CheckboxEasyPlace, "轻松放置（准心对准投影方块自动放置）"},
    {TextKey::CheckboxRangePlace, "范围放置（自动放置周围投影缺块）"},
    {TextKey::ModeManual, "手动放置"},
    {TextKey::ModeEasy, "轻松放置"},
    {TextKey::ModeRange, "范围放置"},
    {TextKey::HintModeEnabled, "%s已临时开启"},
    {TextKey::HintAssistedDisabledByConsent, "辅助放置未开启（需先同意实验性功能声明）"},
    {TextKey::HintAssistedDisabled, "辅助放置未开启"},
    {TextKey::LabelPlacementRadius, "放置半径（范围 1～4）"},
    {TextKey::LabelAutoPlacementCooldown, "投影方块被破坏自动放置冷却时长（范围 0～60 秒）"},

    {TextKey::SectionInterfaceSettings, "界面设置"},
    {TextKey::LabelUiScale, "界面缩放（范围 1～5）"},
    {TextKey::LabelLanguage, "语言"},

    {TextKey::MaterialWater, "水"},
    {TextKey::MaterialLava, "熔岩"},
    {TextKey::StatusNotLoaded, "尚未加载结构文件"},
    {TextKey::LabelMaterialSummary, "共 %llu 个方块，%zu 种材料"},
    {TextKey::HintNoPlaceableMaterials, "没有可直接放置的实体方块"},
    {TextKey::ColumnItem, "物品"},
    {TextKey::ColumnIdentifier, "标识符"},
    {TextKey::ColumnTotal, "总计数量"},

    {TextKey::HudFileName, "投影：%s"},
    {TextKey::HudMaterialFilterAll, "材料筛选：全部"},
    {TextKey::HudMaterialFilterSingle, "材料筛选：第 %d 种（共 %d 种）"},
    {TextKey::HudMaterialFilterUpTo, "材料筛选：前 %d 种"},
    {TextKey::HudMaterialFilterRange, "材料筛选：第 %d～%d 种"},
    {TextKey::HudRangeAll, "显示范围：完整结构"},
    {TextKey::HudCurrentLayer, "当前层：%d / %d（%s 轴）"},
    {TextKey::HudRangeFromZero, "显示范围：第 0～%d 层（%s 轴）"},
    {TextKey::HudRangeBetween, "显示范围：第 %d～%d 层（%s 轴）"},
    {TextKey::HudOverallProgress, "总体进度：%llu / %llu"},
    {TextKey::HudBuildProgress, "建造进度：%llu / %llu"},
    {TextKey::HudWrongState, "朝向错误：%llu"},
    {TextKey::HudWrongType, "放置错误：%llu"},
    {TextKey::HudExtraBlocks, "多余方块：%llu"},
    {TextKey::HudProjectedBlock, "投影方块：%s"},
    {TextKey::HudPlacementMode, "辅助放置：%s"},
    {TextKey::MaterialHudTitle, "缺失材料"},
    {TextKey::MaterialHudScanning, "正在统计当前显示范围…"},
    {TextKey::MaterialHudComplete, "当前显示范围材料已备齐"},
    {TextKey::MaterialHudMore, "…还有 %zu 种材料"},
    {TextKey::DialogFilterProjection, "投影结构 (*.mcstructure;*.litematic)"},
    {TextKey::DialogFilterBedrock, "Bedrock 结构 (*.mcstructure)"},
    {TextKey::DialogFilterLitematica, "Litematica 结构 (*.litematic)"},
    {TextKey::DialogFilterAll, "所有文件 (*.*)"},

    {TextKey::StatusPathEmpty, "请选择或输入 .mcstructure / .litematic 文件路径"},
    {TextKey::StatusLoadFailed, "加载失败：%s"},
    {TextKey::StatusRestoreFailed, "恢复失败：%s"},
    {TextKey::StatusRestoredPending, "已恢复上次投影记录，等待进入渲染"},
    {TextKey::StatusLoaded, "已加载：%s\n尺寸 %s  |  方块 %s"},
    {TextKey::StatusWorldExited, "已退出世界"},
    {TextKey::StatusProjectionClosed, "已关闭投影"},
    {TextKey::CaptureStatusNeedTwoPoints, "请设置选区点 1 和点 2"},
    {TextKey::CaptureStatusSelectionReady, "选区已设置"},
    {TextKey::CaptureStatusNeedOtherPoint, "请继续设置另一个选区点"},
    {TextKey::CaptureStatusNoWorld, "尚未进入世界"},
    {TextKey::CaptureStatusSingleplayerUnsupported, "单人存档模式尚未实现"},
    {TextKey::CaptureStatusNeedBothPoints, "请先设置选区点 1 和点 2"},
    {TextKey::CaptureStatusRegionNotLoaded, "选区包含客户端尚未完整加载的区域"},
    {TextKey::CaptureStatusTemplateFailed, "原版 StructureTemplate 无法捕获该选区"},
    {TextKey::CaptureStatusWriteFailed, "写入 .mcstructure 文件失败"},
    {TextKey::CaptureStatusExported, "结构已成功导出"},
    {TextKey::CaptureStatusPoint1Recorded, "已记录选区点 1"},
    {TextKey::CaptureStatusPoint2Recorded, "已记录选区点 2"},

    {TextKey::ActionHintProjectionSuspended, "维度发生变化，投影已暂停"},
    {TextKey::ActionHintProjectionRestored, "已返回投影所在维度，投影已恢复"},
    {TextKey::ActionHintWorldExited, "已退出世界，投影已关闭"},
    {TextKey::ActionHintLoadProjection, "加载投影"},
    {TextKey::ActionHintCloseProjection, "关闭投影"},
    {TextKey::ActionHintNoMatchingItem, "背包中没有对应的投影方块"},
    {TextKey::ActionHintManualModeBlocked, "已被手动放置模式阻止（对准投影方块才能放置）"}
}};

inline constexpr std::array<TextEntry, kTextKeyCount> kEnglishEntries{{
    {TextKey::None, ""},

    {TextKey::MenuClose, "Close menu"},

    {TextKey::PageProjection, "Projection"},
    {TextKey::PageCreateStructure, "Create structure"},
    {TextKey::PageTransform, "Transform"},
    {TextKey::PageRender, "Rendering"},
    {TextKey::PageHud, "HUD"},
    {TextKey::PageHotkeys, "Hotkeys"},
    {TextKey::PageInterface, "Interface"},
    {TextKey::PageExperimental, "Experimental"},

    {TextKey::SectionProjectionFile, "Projection file"},
    {TextKey::LabelStructurePath, "Structure path (.mcstructure / .litematic)"},
    {TextKey::ButtonBrowse, "Browse"},
    {TextKey::ButtonLoad, "Load"},
    {TextKey::ButtonCloseProjection, "Close projection"},
    {TextKey::MaterialListTitle, "Materials"},
    {TextKey::ButtonRestoreLastProjection, "Restore last projection"},
    {TextKey::LabelSavedOrigin, "Origin X %d  Y %d  Z %d"},
    {TextKey::HintNoSavedProjection, "No saved projection to restore"},

    {TextKey::SectionCaptureSource, "Create structure"},
    {TextKey::LabelCaptureMode, "Mode"},
    {TextKey::ComboCaptureModeClient, "Client mode"},
    {TextKey::HintCaptureClientOnly, "Client mode only saves the world area the client has loaded; container contents and entity data may be incomplete."},
    {TextKey::SectionCaptureSelection, "Selection"},
    {TextKey::LabelCapturePoint1, "Point 1"},
    {TextKey::LabelCapturePoint2, "Point 2"},
    {TextKey::ButtonUsePlayerPosition, "Use player position"},
    {TextKey::LabelNotSet, "Not set"},
    {TextKey::LabelCaptureSize, "Size: %llu × %llu × %llu"},
    {TextKey::LabelCaptureVolume, "Total blocks: %llu"},
    {TextKey::CheckboxIncludeEntities, "Include entities"},
    {TextKey::ButtonClearSelection, "Clear selection"},
    {TextKey::ButtonExportMcstructure, "Export .mcstructure"},

    {TextKey::SectionTransform, "Transform"},
    {TextKey::LabelRotation, "Rotation"},
    {TextKey::LabelMirror, "Mirror"},
    {TextKey::ComboMirrorNone, "None"},
    {TextKey::LabelOffsetX, "X offset"},
    {TextKey::LabelOffsetY, "Y offset"},
    {TextKey::LabelOffsetZ, "Z offset"},

    {TextKey::SectionProjectionStyle, "Projection display"},
    {TextKey::LabelOpacity, "Projection opacity (0-100)"},
    {TextKey::CheckboxRenderBounds, "Show structure bounds"},
    {TextKey::SectionLayerSettings, "Layer display"},
    {TextKey::LabelLayerAxis, "Layer axis"},
    {TextKey::ComboLayerAxisY, "Y (horizontal layers)"},
    {TextKey::ComboLayerAxisX, "X (vertical slices)"},
    {TextKey::ComboLayerAxisMaterial, "By material"},
    {TextKey::LabelDisplayRange, "Range"},
    {TextKey::ComboRangeAll, "Whole structure"},
    {TextKey::ComboRangeSingle, "Single layer"},
    {TextKey::ComboRangeUpToCurrent, "Up to current layer"},
    {TextKey::ComboRangeFromCurrent, "From current layer"},
    {TextKey::ComboMaterialAll, "All materials"},
    {TextKey::ComboMaterialSingle, "Current only"},
    {TextKey::ComboMaterialUpToCurrent, "First to current"},
    {TextKey::ComboMaterialFromCurrent, "Current to last"},
    {TextKey::LabelCurrentMaterial, "Material number"},
    {TextKey::LabelCurrentLayer, "Current layer"},
    {TextKey::HintMaterialOrder, "1 - %d (material list order)"},
    {TextKey::HintLayerZeroBased, "0 - %d (structure %s axis starts at 0)"},
    {TextKey::SectionCorrectionStyle, "Correction style"},
    {TextKey::LabelCorrectionFill, "Correction fill opacity (0-100)"},
    {TextKey::LabelCorrectionOutline, "Correction outline opacity (0-100)"},
    {TextKey::ButtonResetCorrectionStyle, "Reset correction style"},
    {TextKey::SectionSeeThrough, "See-through"},
    {TextKey::CheckboxCorrectionSeeThrough, "See-through wrong-block markers (X-ray)"},
    {TextKey::CheckboxMissingSeeThrough, "See-through missing markers (X-ray)"},

    {TextKey::SectionHud, "HUD"},
    {TextKey::CheckboxHudEnabled, "Enable HUD"},
    {TextKey::LabelHudPosition, "HUD position"},
    {TextKey::CornerTopLeft, "Top left"},
    {TextKey::CornerBottomLeft, "Bottom left"},
    {TextKey::CornerTopRight, "Top right"},
    {TextKey::CornerBottomRight, "Bottom right"},
    {TextKey::CheckboxHudFileName, "Show file name"},
    {TextKey::CheckboxHudLayer, "Show layer info"},
    {TextKey::CheckboxHudOverallProgress, "Show overall progress"},
    {TextKey::CheckboxHudProgress, "Show build progress"},
    {TextKey::CheckboxHudProjectedBlockName, "Show projected block name"},
    {TextKey::CheckboxHudWrongState, "Show wrong orientation"},
    {TextKey::CheckboxHudWrongType, "Show wrong block type"},
    {TextKey::CheckboxHudExtraBlocks, "Show extra blocks"},
    {TextKey::CheckboxMaterialHudEnabled, "Show missing materials"},
    {TextKey::LabelMaterialHudPosition, "Missing materials position"},

    {TextKey::SectionHotkeys, "Hotkeys"},
    {TextKey::HintPressKeys, "Press keys"},
    {TextKey::ButtonResetHotkey, "Reset"},
    {TextKey::ButtonClearHotkey, "Clear"},
    {TextKey::ButtonResetAllHotkeys, "Reset all hotkeys"},
    {TextKey::HintChatCommand, "Type LHolo in chat to open the menu"},
    {TextKey::HotkeyOpenMenu, "Open menu"},
    {TextKey::HotkeyMoveXMinus, "Offset X -1"},
    {TextKey::HotkeyMoveXPlus, "Offset X +1"},
    {TextKey::HotkeyMoveZMinus, "Offset Z -1"},
    {TextKey::HotkeyMoveZPlus, "Offset Z +1"},
    {TextKey::HotkeyMoveYPlus, "Offset Y +1"},
    {TextKey::HotkeyMoveYMinus, "Offset Y -1"},
    {TextKey::HotkeyLayerIncrease, "Layer up"},
    {TextKey::HotkeyLayerDecrease, "Layer down"},
    {TextKey::HotkeyLoadProjection, "Load projection"},
    {TextKey::HotkeyCloseProjection, "Close projection"},
    {TextKey::KeyNotSet, "Not set"},
    {TextKey::KeyMouseMiddle, "Middle mouse"},
    {TextKey::KeyMouseSide1, "Mouse button 4"},
    {TextKey::KeyMouseSide2, "Mouse button 5"},

    {TextKey::SectionAssistedPlacement, "Assisted placement"},
    {TextKey::ButtonExperimentalInfoOpen, "⚠ Experimental features (read and enable)"},
    {TextKey::ButtonExperimentalInfoView, "View experimental features notice"},
    {TextKey::ModalTitleExperimental, "⚠ Experimental features · Please read before use"},
    {TextKey::ExperimentalWarningIntro, "Easy place / manual place / range place are experimental helpers that place projected blocks for you, automatically or semi-automatically."},
    {TextKey::ExperimentalWarningAntiCheat, "Because they simulate block placement programmatically, some server anti-cheat systems may treat them as cheating."},
    {TextKey::ExperimentalWarningAllowed, "· Use them only in single-player worlds, or on servers whose owner has explicitly allowed it."},
    {TextKey::ExperimentalWarningConsequence, "· Using them on a server without permission may get you kicked or banned."},
    {TextKey::ExperimentalWarningResponsibility, "· You accept all risks and consequences; the author and this mod take no responsibility."},
    {TextKey::ExperimentalWarningManualSafe, "Placing blocks by hand is always safe; assisted placement is only an experimental convenience."},
    {TextKey::ButtonConsentEnable, "I understand the risk, enable"},
    {TextKey::ButtonCancel, "Cancel"},
    {TextKey::ButtonClose, "Close"},
    {TextKey::CheckboxManualPlace, "Manual place (right-click; hold to keep placing)"},
    {TextKey::CheckboxEasyPlace, "Easy place (place when the crosshair is on a projected block)"},
    {TextKey::CheckboxRangePlace, "Range place (auto place nearby missing blocks)"},
    {TextKey::ModeManual, "Manual place"},
    {TextKey::ModeEasy, "Easy place"},
    {TextKey::ModeRange, "Range place"},
    {TextKey::HintModeEnabled, "%s is enabled for this session"},
    {TextKey::HintAssistedDisabledByConsent, "Assisted placement is off (accept the experimental notice first)"},
    {TextKey::HintAssistedDisabled, "Assisted placement is off"},
    {TextKey::LabelPlacementRadius, "Placement radius (1-4)"},
    {TextKey::LabelAutoPlacementCooldown, "Auto-place cooldown after a projected block is broken (0-60 s)"},

    {TextKey::SectionInterfaceSettings, "Interface settings"},
    {TextKey::LabelUiScale, "UI scale (1-5)"},
    {TextKey::LabelLanguage, "Language"},

    {TextKey::MaterialWater, "Water"},
    {TextKey::MaterialLava, "Lava"},
    {TextKey::StatusNotLoaded, "No structure loaded"},
    {TextKey::LabelMaterialSummary, "%llu blocks, %zu materials"},
    {TextKey::HintNoPlaceableMaterials, "No directly placeable blocks"},
    {TextKey::ColumnItem, "Item"},
    {TextKey::ColumnIdentifier, "Identifier"},
    {TextKey::ColumnTotal, "Total"},

    {TextKey::HudFileName, "Projection: %s"},
    {TextKey::HudMaterialFilterAll, "Material filter: all"},
    {TextKey::HudMaterialFilterSingle, "Material filter: #%d of %d"},
    {TextKey::HudMaterialFilterUpTo, "Material filter: first %d"},
    {TextKey::HudMaterialFilterRange, "Material filter: #%d-%d"},
    {TextKey::HudRangeAll, "Range: whole structure"},
    {TextKey::HudCurrentLayer, "Current layer: %d / %d (%s axis)"},
    {TextKey::HudRangeFromZero, "Range: layers 0-%d (%s axis)"},
    {TextKey::HudRangeBetween, "Range: layers %d-%d (%s axis)"},
    {TextKey::HudOverallProgress, "Overall: %llu / %llu"},
    {TextKey::HudBuildProgress, "Progress: %llu / %llu"},
    {TextKey::HudWrongState, "Wrong orientation: %llu"},
    {TextKey::HudWrongType, "Wrong type: %llu"},
    {TextKey::HudExtraBlocks, "Extra blocks: %llu"},
    {TextKey::HudProjectedBlock, "Projected block: %s"},
    {TextKey::HudPlacementMode, "Assist: %s"},
    {TextKey::MaterialHudTitle, "Missing materials"},
    {TextKey::MaterialHudScanning, "Counting the visible range…"},
    {TextKey::MaterialHudComplete, "All materials for the visible range are ready"},
    {TextKey::MaterialHudMore, "…and %zu more materials"},
    {TextKey::DialogFilterProjection, "Projection structures (*.mcstructure;*.litematic)"},
    {TextKey::DialogFilterBedrock, "Bedrock structures (*.mcstructure)"},
    {TextKey::DialogFilterLitematica, "Litematica structures (*.litematic)"},
    {TextKey::DialogFilterAll, "All files (*.*)"},

    {TextKey::StatusPathEmpty, "Choose or type a .mcstructure / .litematic path"},
    {TextKey::StatusLoadFailed, "Load failed: %s"},
    {TextKey::StatusRestoreFailed, "Restore failed: %s"},
    {TextKey::StatusRestoredPending, "Restored the saved projection; waiting to render"},
    {TextKey::StatusLoaded, "Loaded: %s\nSize %s  |  Blocks %s"},
    {TextKey::StatusWorldExited, "Left the world"},
    {TextKey::StatusProjectionClosed, "Projection closed"},
    {TextKey::CaptureStatusNeedTwoPoints, "Set selection points 1 and 2"},
    {TextKey::CaptureStatusSelectionReady, "Selection is ready"},
    {TextKey::CaptureStatusNeedOtherPoint, "Set the other selection point"},
    {TextKey::CaptureStatusNoWorld, "Not in a world"},
    {TextKey::CaptureStatusSingleplayerUnsupported, "Single-player mode is not implemented yet"},
    {TextKey::CaptureStatusNeedBothPoints, "Set selection points 1 and 2 first"},
    {TextKey::CaptureStatusRegionNotLoaded, "The selection includes areas the client has not fully loaded"},
    {TextKey::CaptureStatusTemplateFailed, "The vanilla StructureTemplate could not capture this selection"},
    {TextKey::CaptureStatusWriteFailed, "Failed to write the .mcstructure file"},
    {TextKey::CaptureStatusExported, "Structure exported"},
    {TextKey::CaptureStatusPoint1Recorded, "Selection point 1 recorded"},
    {TextKey::CaptureStatusPoint2Recorded, "Selection point 2 recorded"},

    {TextKey::ActionHintProjectionSuspended, "Dimension changed; projection paused"},
    {TextKey::ActionHintProjectionRestored, "Back in the projection's dimension; projection restored"},
    {TextKey::ActionHintWorldExited, "Left the world; projection closed"},
    {TextKey::ActionHintLoadProjection, "Loading projection"},
    {TextKey::ActionHintCloseProjection, "Closing projection"},
    {TextKey::ActionHintNoMatchingItem, "No matching block in your inventory"},
    {TextKey::ActionHintManualModeBlocked, "Blocked by manual place mode (aim at a projected block)"}
}};

static_assert(
    isComplete(kSimplifiedChineseEntries),
    "Simplified Chinese table must cover every TextKey exactly once"
);
static_assert(
    isComplete(kEnglishEntries),
    "English table must cover every TextKey exactly once"
);

inline constexpr TextTable kSimplifiedChineseTable = buildTable(kSimplifiedChineseEntries);
inline constexpr TextTable kEnglishTable = buildTable(kEnglishEntries);

} // namespace lholo::i18n
