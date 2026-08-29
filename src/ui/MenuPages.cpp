// LHolo - Fluent-style menu pages

#include "ui/MenuPages.h"

#include "ui/MenuWidgets.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>

#include <imgui.h>

namespace lholo::ui {
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
    return model.layerAxis == 1 ? model.maxLayerX : model.maxLayerY;
}

void renderPathRow(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    auto input = [&] {
        if (!model.pathBuffer || model.pathBufferSize == 0) return;
        auto const browseWidth = ImGui::CalcTextSize("浏览").x + ImGui::GetStyle().FramePadding.x * 2.0f;
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
        if (ImGui::Button("浏览") && actions.browseStructure) {
            if (auto selected = actions.browseStructure(model.pathBuffer)) {
                std::snprintf(model.pathBuffer, model.pathBufferSize, "%s", selected->c_str());
            }
        }
    };
    if (metrics.compact) {
        ImGui::TextUnformatted("结构文件路径（.mcstructure / .litematic）");
        input();
    } else {
        input();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("结构文件路径（.mcstructure / .litematic）");
    }
}

void renderProjectionPage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    renderSection("##ProjectionFile", "投影文件", metrics, [&] {
        ImGui::TextWrapped("%s", model.status.c_str());
        ImGui::Spacing();
        renderPathRow(model, actions, metrics);
        ImGui::Spacing();
        if (ImGui::Button("加载") && actions.loadStructure) actions.loadStructure(model.pathBuffer ? model.pathBuffer : "");
        if (!metrics.compact) ImGui::SameLine();
        if (ImGui::Button("关闭投影") && actions.closeProjection) actions.closeProjection();
        if (!metrics.compact) ImGui::SameLine();
        if (ImGui::Button("材料清单")) {
            if (actions.requestMaterials) actions.requestMaterials();
            // OpenPopup() is deferred to the page scope, where the modal is
            // rendered, so both calls use the same Dear ImGui ID stack.
            model.materialPopupRequested = true;
        }
        ImGui::Spacing();
        if (model.hasSavedProjection) {
            if (ImGui::Button("恢复上次投影") && actions.restoreProjection) actions.restoreProjection();
            ImGui::SameLine();
            ImGui::TextDisabled("原点 X %d  Y %d  Z %d", model.savedAnchorX, model.savedAnchorY, model.savedAnchorZ);
        } else {
            ImGui::TextDisabled("没有可恢复的上次投影记录");
        }
    });

}

bool renderCapturePoint(
    char const* id,
    char const* title,
    CapturePointModel& point,
    CapturePointId pointId,
    CaptureCoordinateText& coordinateText,
    float coordinateWidth,
    MenuActions const& actions,
    UiMetrics const& metrics
) {
    ImGui::PushID(id);
    ImGui::TextUnformatted(title);
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
    if (ImGui::Button("使用玩家当前位置") && actions.usePlayerCapturePosition) {
        actions.usePlayerCapturePosition(pointId);
    }
    if (!valid) {
        if (!metrics.compact) ImGui::SameLine();
        ImGui::TextDisabled("未设置");
    }
    ImGui::PopID();
    return valid;
}

void renderCreateStructurePage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    renderSection("##CaptureSource", "创建结构", metrics, [&] {
        model.capture.mode = 0;
        static char const* modeNames[]{"客户端模式"};
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("模式");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(adaptiveComboWidth(modeNames, 1));
        if (ImGui::BeginCombo("##CaptureMode", modeNames[0])) {
            ImGui::Selectable(modeNames[0], true);
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("客户端模式只能保存当前已加载的世界范围，且容器内容和实体数据可能不完整");
    });

    renderSection("##CaptureSelection", "结构选区", metrics, [&] {
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
            : ImGui::CalcTextSize("使用玩家当前位置").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        auto const maximumWidth = std::max(
            0.0f,
            (ImGui::GetContentRegionAvail().x - axisWidth * 3.0f - buttonWidth
                - spacing * (metrics.compact ? 5.0f : 6.0f)) / 3.0f
        );
        coordinateWidth = std::min(coordinateWidth, maximumWidth);
        ImGui::BeginDisabled(!model.captureWorldAvailable);
        auto const firstValid = renderCapturePoint(
            "First", "点 1", model.capture.first, CapturePointId::First,
            gCaptureInputState.first, coordinateWidth, actions, metrics
        );
        ImGui::Spacing();
        auto const secondValid = renderCapturePoint(
            "Second", "点 2", model.capture.second, CapturePointId::Second,
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
            ImGui::Text("尺寸：%llu × %llu × %llu", sizeX, sizeY, sizeZ);
            ImGui::Text("方块总数：%llu", volume);
        }

        ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.55f));
        ImGui::BeginDisabled(!model.captureWorldAvailable);
        renderCheckboxRow("##IncludeEntities", "包含实体", model.capture.includeEntities, metrics);
        ImGui::EndDisabled();
        if (ImGui::Button("清除选区") && actions.clearCapture) actions.clearCapture();
        if (!metrics.compact) ImGui::SameLine();
        ImGui::BeginDisabled(
            !model.captureWorldAvailable || !firstValid || !secondValid
        );
        if (ImGui::Button("导出 .mcstructure") && actions.exportCapture) {
            actions.exportCapture(model.capture);
        }
        ImGui::EndDisabled();
    });
}

namespace {

// The experimental-features consent modal. On "启用" it records consent and
// enables the feature the user was trying to turn on (pendingFeature: 1 manual,
// 2 easy, 3 range; -1 view-only, 0 none).
void renderExperimentalConsentModal(
    MenuModel& model, MenuActions const& actions, int& pendingFeature
) {
    auto const displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(
        ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f)
    );
    if (!ImGui::BeginPopupModal(
            "##ExperimentalConsent", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
        )) {
        return;
    }
    ImGui::TextUnformatted("⚠ 实验性功能 · 使用前请阅读");
    ImGui::Separator();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
    ImGui::TextWrapped(
        "轻松放置 / 手动放置 / 范围放置 属于实验性辅助功能，会自动或半自动地帮你"
        "放置投影中的方块。\n\n"
        "由于这些功能会程序化地模拟方块放置，部分服务器的反作弊系统可能将其判定为"
        "作弊 / 外挂行为。\n\n"
        "· 请仅在单人世界，或已获得服主明确许可的服务器上使用。\n"
        "· 在未经许可的服务器上使用，可能导致被踢出、封禁账号等后果。\n"
        "· 一切风险与后果由使用者自行承担，作者与本模组概不负责。\n\n"
        "手动逐格搭建始终是安全的做法；辅助放置仅为提升效率的实验性工具。"
    );
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    if (model.experimentalConsent) {
        if (ImGui::Button("关闭")) {
            pendingFeature = 0;
            ImGui::CloseCurrentPopup();
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.20f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.26f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.16f, 0.18f, 1.0f));
        bool const enable = ImGui::Button("我已了解风险，启用");
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
        if (ImGui::Button("取消")) {
            pendingFeature = 0;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

} // namespace

void renderExperimentalPage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    static int pendingConsentFeature = 0;
    renderSection("##AssistedPlacement", "辅助放置", metrics, [&] {
        // A placement hotkey pressed before consent routed us here and asked to
        // open the popup for the feature it was trying to enable.
        if (model.consentPopupRequest != 0) {
            pendingConsentFeature = model.consentPopupRequest;
            model.consentPopupRequest = 0;
            ImGui::OpenPopup("##ExperimentalConsent");
        }
        // The说明 / consent entry sits at the very top and is coloured to stand out.
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.20f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.26f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.16f, 0.18f, 1.0f));
            char const* label = model.experimentalConsent
                ? "查看实验性功能说明"
                : "⚠ 实验性功能说明（点此阅读并启用）";
            if (ImGui::Button(label)) {
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
        renderCheckboxRow("##ManualPlace", "手动放置（右键放置·按住连放）", model.manualPlace, metrics);
        gate(model.manualPlace, 1);
        if (model.manualPlace) { model.easyPlaceEnabled = false; model.rangeEnabled = false; }
        renderCheckboxRow("##EasyPlace", "轻松放置（准心对准投影方块自动放置）", model.easyPlaceEnabled, metrics);
        gate(model.easyPlaceEnabled, 2);
        if (model.easyPlaceEnabled) { model.manualPlace = false; model.rangeEnabled = false; }
        renderCheckboxRow("##RangePlace", "范围放置（自动放置周围投影缺块）", model.rangeEnabled, metrics);
        gate(model.rangeEnabled, 3);
        if (model.rangeEnabled) { model.easyPlaceEnabled = false; model.manualPlace = false; }
        ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.25f));
        if (model.manualPlace || model.easyPlaceEnabled || model.rangeEnabled) {
            char const* mode = model.manualPlace ? "手动放置"
                : model.easyPlaceEnabled ? "轻松放置" : "范围放置";
            ImGui::TextWrapped("%s已临时开启", mode);
        } else if (!model.experimentalConsent) {
            ImGui::TextDisabled("辅助放置未开启（需先同意实验性功能声明）");
        } else {
            ImGui::TextDisabled("辅助放置未开启");
        }
        renderSteppedInt("PlacementRadius", "放置半径（范围 1～4）", model.placementRadius, 1, 4, metrics);
        renderSteppedInt(
            "AutoPlacementBreakCooldown",
            "投影方块被破坏自动放置冷却时长（范围 0～60 秒）",
            model.autoPlacementBreakCooldownSeconds,
            0,
            60,
            metrics
        );
    });
}

void renderTransformPage(MenuModel& model, UiMetrics const& metrics) {
    renderSection("##Transform", "结构变换", metrics, [&] {
        static char const* rotationNames[]{"0°", "90°", "180°", "270°"};
        static char const* mirrorNames[]{"无", "X", "Z"};
        auto const transformComboWidth = std::max(
            adaptiveComboWidth(rotationNames, 4),
            adaptiveComboWidth(mirrorNames, 3)
        );
        renderValueRow("结构旋转", metrics, [&] {
            ImGui::SetNextItemWidth(transformComboWidth);
            ImGui::Combo("##Rotation", &model.rotation, rotationNames, 4);
        });
        renderValueRow("结构镜像", metrics, [&] {
            ImGui::SetNextItemWidth(transformComboWidth);
            ImGui::Combo("##Mirror", &model.mirror, mirrorNames, 3);
        });
        ImGui::Separator();
        renderSteppedInt("OffsetX", "X 轴偏移", model.offsetX, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), metrics);
        renderSteppedInt("OffsetY", "Y 轴偏移", model.offsetY, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), metrics);
        renderSteppedInt("OffsetZ", "Z 轴偏移", model.offsetZ, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), metrics);
    });
}

void renderRenderPage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    renderSection("##ProjectionStyle", "投影显示设置", metrics, [&] {
        auto opacity = static_cast<int>(std::lround(model.opacity * 100.0f));
        renderNumericValueRow("投影透明度（范围 0～100）", metrics, [&] {
            if (ImGui::InputInt("##Opacity", &opacity, 0, 0)) {
                model.opacity = static_cast<float>(std::clamp(opacity, 0, 100)) / 100.0f;
            }
        });
        renderCheckboxRow("##RenderBounds", "显示整体结构边框", model.structureBoundsEnabled, metrics);
        renderCheckboxRow(
            "##ProjectionSeeThrough", "投影方块穿透显示（X 光）", model.projectionSeeThrough, metrics
        );
    });

    renderSection("##LayerSettings", "分层显示设置", metrics, [&] {
        static char const* axisNames[]{"Y 轴（水平分层）", "X 轴（纵向切片）"};
        static char const* modeNames[]{"完整结构", "单层", "当前层及以下", "当前层及以上"};
        renderValueRow("分层轴", metrics, [&] {
            ImGui::SetNextItemWidth(adaptiveComboWidth(axisNames, 2));
            if (ImGui::Combo("##LayerAxis", &model.layerAxis, axisNames, 2)) {
                model.displayLayer = std::clamp(model.displayLayer, 0, maxLayer(model));
            }
        });
        renderValueRow("显示范围", metrics, [&] {
            ImGui::SetNextItemWidth(adaptiveComboWidth(modeNames, 4));
            ImGui::Combo("##LayerMode", &model.layerDisplayMode, modeNames, 4);
        });
        renderSteppedInt("DisplayLayer", "当前层", model.displayLayer, 0, maxLayer(model), metrics);
        if (!metrics.compact) ImGui::SameLine(0.0f, metrics.gap * 0.55f);
        ImGui::PushTextWrapPos(-1.0f);
        ImGui::TextDisabled("0 - %d（结构 %s 轴起点为 0）", maxLayer(model), model.layerAxis == 1 ? "X" : "Y");
        ImGui::PopTextWrapPos();
    });

    renderSection("##CorrectionStyle", "纠错提示样式", metrics, [&] {
        auto fill = static_cast<int>(std::lround(model.correctionFillOpacity * 100.0f));
        renderNumericValueRow("纠错填充透明度（范围 0～100）", metrics, [&] {
            if (ImGui::InputInt("##CorrectionFill", &fill, 0, 0)) {
                model.correctionFillOpacity = static_cast<float>(std::clamp(fill, 0, 100)) / 100.0f;
            }
        });
        auto outline = static_cast<int>(std::lround(model.correctionOutlineOpacity * 100.0f));
        renderNumericValueRow("纠错描边透明度（范围 0～100）", metrics, [&] {
            if (ImGui::InputInt("##CorrectionOutline", &outline, 0, 0)) {
                model.correctionOutlineOpacity = static_cast<float>(std::clamp(outline, 0, 100)) / 100.0f;
            }
        });
        renderCheckboxRow(
            "##CorrectionSeeThrough", "错误标记穿透显示（X 光）", model.correctionSeeThrough, metrics
        );
        ImGui::TextDisabled("放置错误与状态错误共用此开关");
        renderCheckboxRow(
            "##MissingSeeThrough", "未放置标记穿透显示（X 光）", model.missingSeeThrough, metrics
        );
        if (ImGui::Button("恢复默认纠错样式")) {
            if (actions.resetCorrectionStyle) actions.resetCorrectionStyle();
            model.correctionFillOpacity = 0.15f;
            model.correctionOutlineOpacity = 1.0f;
        }
    });
}

void renderHotkeysPage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    renderSection("##Hotkeys", "快捷键", metrics, [&] {
        auto maxLabelWidth = 0.0f;
        auto maxBindingWidth = ImGui::CalcTextSize("请按组合键").x;
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
            auto const clearWidth = ImGui::CalcTextSize("清除").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            auto const available = ImGui::GetContentRegionAvail().x;
            auto const controlSpacing = ImGui::GetStyle().ItemSpacing.x;
            auto const requiredWidth = preferredBindingWidth + clearWidth + controlSpacing;
            auto const totalWidth = std::min(available, requiredWidth);
            auto const bindWidth = std::max(0.0f, totalWidth - clearWidth - controlSpacing);
            auto const label = hotkey.capturing ? "请按组合键" : hotkey.display.c_str();
            if (ImGui::Button(label, ImVec2(bindWidth, 0.0f)) && !hotkey.capturing && actions.beginHotkeyCapture) {
                actions.beginHotkeyCapture(hotkey.id);
            }
            ImGui::SameLine();
            if (ImGui::Button("清除") && actions.clearHotkey) actions.clearHotkey(hotkey.id);
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        if (ImGui::Button("恢复默认快捷键") && actions.resetHotkeys) actions.resetHotkeys();
        ImGui::TextDisabled("可在聊天栏输入 LHolo 打开投影菜单");
    });
}

void renderHudPage(MenuModel& model, UiMetrics const& metrics) {
    renderSection("##Hud", "HUD 信息显示", metrics, [&] {
        renderCheckboxRow("##HudEnabled", "启用 HUD", model.hudEnabled, metrics);
        ImGui::BeginDisabled(!model.hudEnabled);
        static char const* positions[]{"左上", "左下", "右上", "右下"};
        renderValueRow("HUD 位置", metrics, [&] {
            ImGui::SetNextItemWidth(adaptiveComboWidth(positions, 4));
            ImGui::Combo("##HudPosition", &model.hudPosition, positions, 4);
        });
        renderCheckboxRow("##HudFileName", "显示投影文件名", model.hudShowFileName, metrics);
        renderCheckboxRow("##HudLayer", "显示渲染层信息", model.hudShowLayer, metrics);
        renderCheckboxRow(
            "##HudOverallProgress", "显示总体进度", model.hudShowOverallProgress, metrics
        );
        renderCheckboxRow("##HudProgress", "显示建造进度", model.hudShowProgress, metrics);
        renderCheckboxRow(
            "##HudProjectedBlockName",
            "显示投影方块名称",
            model.hudShowProjectedBlockName,
            metrics
        );
        renderCheckboxRow("##HudWrongState", "显示朝向错误", model.hudShowWrongState, metrics);
        renderCheckboxRow("##HudWrongType", "显示放置错误", model.hudShowWrongType, metrics);
        ImGui::EndDisabled();
        // The material HUD is independent of the main HUD, so its position stays
        // enabled here; whether it shows is toggled in the material-list popup.
        ImGui::Separator();
        renderValueRow("材料HUD 位置", metrics, [&] {
            ImGui::SetNextItemWidth(adaptiveComboWidth(positions, 4));
            ImGui::Combo("##MaterialHudPosition", &model.materialHudPosition, positions, 4);
        });
        ImGui::TextDisabled("材料HUD 在「材料清单」里开启显示");
        ImGui::TextDisabled("HUD 仅在关闭投影菜单后显示");
    });
}

void renderUiScalePage(MenuModel& model, UiMetrics const& metrics) {
    renderSection("##UiScale", "界面缩放", metrics, [&] {
        renderSteppedFloat(
            "UiScaleValue", "界面缩放（范围 1～5）", model.uiScale,
            1.0f, 5.0f, 0.1f, metrics
        );
    });
}

void renderMaterialPopup(MenuModel const& model, MenuActions const& actions, UiMetrics const& metrics) {
    // The material list has a little more breathing room than the regular
    // menu: its table is intentionally 1.5x the original logical footprint.
    constexpr float popupDensity = 1.20f;
    // Measure the actual table content.  A fixed width multiplier makes a
    // seven-row list look like a wide banner and leaves unused space below.
    auto nameWidth = ImGui::CalcTextSize("名称").x;
    auto typeWidth = ImGui::CalcTextSize("标识符").x;
    auto countWidth = ImGui::CalcTextSize("数量").x;
    for (auto const& item : model.materials) {
        nameWidth = std::max(nameWidth, ImGui::CalcTextSize(item.displayName.c_str()).x);
        typeWidth = std::max(typeWidth, ImGui::CalcTextSize(item.typeName.c_str()).x);
        auto const countText = std::to_string(item.count);
        countWidth = std::max(countWidth, ImGui::CalcTextSize(countText.c_str()).x);
    }
    auto const cellPadding = ImGui::GetStyle().CellPadding.x * 2.0f;
    auto const tableWidth = std::max(
        nameWidth + cellPadding,
        std::max((typeWidth + cellPadding) / 0.45f, (countWidth + cellPadding) / 0.15f)
    );
    auto const closeWidth = ImGui::CalcTextSize("关闭").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    auto const titleWidth = ImGui::CalcTextSize("材料清单").x + closeWidth + metrics.gap;
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
    if (!ImGui::BeginPopupModal(
            kMaterialPopupName,
            nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_NoSavedSettings
        )) return;

    ImGui::TextUnformatted("材料清单");
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(),
        ImGui::GetWindowWidth() - closeWidth - metrics.sectionPadding
    ));
    if (ImGui::Button("关闭")) ImGui::CloseCurrentPopup();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.35f));

    // Opt-in toggle for the on-screen material-progress HUD (bottom-left).
    bool materialHud = model.materialHudEnabled;
    if (ImGui::Checkbox("在屏幕上显示材料HUD（左下角）", &materialHud)) {
        if (actions.setMaterialHudEnabled) actions.setMaterialHudEnabled(materialHud);
    }
    ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.35f));

    if (!model.hasLoadedStructure) {
        ImGui::TextDisabled("尚未加载结构文件");
    } else {
        std::uint64_t total{};
        for (auto const& item : model.materials) {
            if (std::numeric_limits<std::uint64_t>::max() - total < item.count) {
                total = std::numeric_limits<std::uint64_t>::max();
                break;
            }
            total += item.count;
        }
        ImGui::Text("共 %llu 个方块，%zu 种材料", static_cast<unsigned long long>(total), model.materials.size());
        ImGui::Separator();
        if (ImGui::BeginChild(
                "##MaterialList",
                ImVec2(0.0f, std::max(line, listHeight)),
                false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings
            )) {
            if (model.materials.empty()) {
                ImGui::TextDisabled("没有可直接放置的实体方块");
            } else if (ImGui::BeginTable(
                           "##MaterialTable", 3,
                           ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg
                               | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings
                       )) {
                ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthStretch, 0.40f);
                ImGui::TableSetupColumn("标识符", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                ImGui::TableSetupColumn("数量", ImGuiTableColumnFlags_WidthStretch, 0.15f);
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
                    renderCenteredCell(item.displayName.c_str());
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
    for (std::size_t index = 0; index < kPageNames.size(); ++index) {
        auto const page = static_cast<MenuPage>(index);
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
        auto const textSize = ImGui::CalcTextSize(kPageNames[index]);
        auto const textInset = metrics.sectionPadding * 1.25f;
        drawList->AddText(
            ImGui::GetFont(),
            ImGui::GetFontSize(),
            ImVec2(min.x + textInset, min.y + (height - textSize.y) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text),
            kPageNames[index]
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
