// LHolo - Fluent-style menu

#include "ui/LHoloMenu.h"

#include "ui/MenuWidgets.h"

#include "ui/MenuPages.h"

#include <algorithm>

namespace lholo::ui {

void renderMenu(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    bool open = true;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    if (ImGui::Begin(
            "##LHoloFullscreen", &open,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings
        )) {
        ImGui::TextUnformatted("LHolo");
        auto const closeLabel = i18n::tr(i18n::TextKey::MenuClose);
        auto const closeWidth = ImGui::CalcTextSize(closeLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - closeWidth - metrics.outerPadding));
        if (ImGui::Button(closeLabel)) open = false;
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        auto const available = ImGui::GetContentRegionAvail();
        auto navText = 0.0f;
        for (std::size_t page = 0; page < kMenuPageCount; ++page) {
            navText = std::max(
                navText,
                ImGui::CalcTextSize(pageName(static_cast<MenuPage>(page))).x
            );
        }
        auto navWidth = std::max(navText + metrics.outerPadding * 2.2f, available.x * 0.17f);
        navWidth = std::min(navWidth, available.x * 0.34f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
        if (ImGui::BeginChild("##Navigation", ImVec2(navWidth, 0.0f), false, ImGuiWindowFlags_NoSavedSettings)) {
            renderNavigation(model, metrics);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::BeginChild(
                "##Page", ImVec2(0.0f, 0.0f), false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings
            )) {
            switch (model.page) {
            case MenuPage::Projection: renderProjectionPage(model, actions, metrics); break;
            case MenuPage::CreateStructure: renderCreateStructurePage(model, actions, metrics); break;
            case MenuPage::Experimental: renderExperimentalPage(model, actions, metrics); break;
            case MenuPage::Transform: renderTransformPage(model, metrics); break;
            case MenuPage::Render: renderRenderPage(model, actions, metrics); break;
            case MenuPage::Hotkeys: renderHotkeysPage(model, actions, metrics); break;
            case MenuPage::Hud: renderHudPage(model, metrics); break;
            case MenuPage::Interface: renderInterfacePage(model, metrics); break;
            }
            if (model.materialPopupRequested) {
                auto const popupName = materialPopupName();
                ImGui::OpenPopup(popupName.c_str());
            }
            // Both opening and rendering happen in this page child. Dear
            // ImGui popup IDs are scoped to the current window.
            renderMaterialPopup(model, metrics);
        }
        ImGui::EndChild();
    }
    ImGui::End();
    if (!open) model.closeRequested = true;
}

} // namespace lholo::ui
