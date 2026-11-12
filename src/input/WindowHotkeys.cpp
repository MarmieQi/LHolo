// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "input/WindowHotkeys.h"

#include "input/HotkeyCapture.h"
#include "ll/api/service/Bedrock.h"
#include "mc/gamecore/AppPlatform_GameCore.h"
#include "oreui/OreUiScreen.h"
#include "structure/StructureLoader.h"
#include "ui/hud/editor/HudEditor.h"
#include "ui/preview/StructurePreviewScene.h"

#include <Windows.h>
#include <windowsx.h>
#include <array>
#include <atomic>

namespace lholo::input {
namespace {
HWND installedWindow{};
std::atomic<WNDPROC> originalWndProc{};
std::array<bool, 256> captureKeyReleases{}; // Window thread only.

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto const original = originalWndProc.load();
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && wParam == FALSE)) {
        captureKeyReleases.fill(false);
        structure::resetHotkeyState();
    }
    unsigned captureKey{};
    bool captureDown{}, captureUp{};
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) { captureKey = static_cast<unsigned>(wParam); captureDown = true; }
    if (message == WM_KEYUP || message == WM_SYSKEYUP) { captureKey = static_cast<unsigned>(wParam); captureUp = true; }
    if (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP) {
        captureKey = VK_MBUTTON; captureDown = message == WM_MBUTTONDOWN; captureUp = !captureDown;
    }
    if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP) {
        captureKey = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
        captureDown = message == WM_XBUTTONDOWN; captureUp = !captureDown;
    }
    if (captureUp && captureKey < captureKeyReleases.size() && captureKeyReleases[captureKey]) {
        captureKeyReleases[captureKey] = false; return 1;
    }
    if (captureDown && captureKey < captureKeyReleases.size() && captureKeyReleases[captureKey]) return 1;
    if (oreui::isMenuOnTop() || ui::hud::hudLayoutEditorActive() || ui::preview::structurePreviewActive()) {
        if (captureDown && input::hotkeyCaptureActive()) {
            auto modifiers = ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 1u : 0u)
                | ((GetAsyncKeyState(VK_MENU) & 0x8000) ? 2u : 0u)
                | ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 4u : 0u);
            if (input::captureHotkeyKey(captureKey, modifiers)) {
                if (captureKey < captureKeyReleases.size()) captureKeyReleases[captureKey] = true;
                return 1;
            }
        }
        return CallWindowProcW(original, window, message, wParam, lParam);
    }
    if (captureDown && structure::handleGuiHotkeyKeyDown(captureKey)) return 1;
    if (captureUp && structure::handleGuiHotkeyKeyUp(captureKey)) return 1;
    if (message == WM_MOUSEWHEEL && structure::handleProjectionOffsetWheel(GET_WHEEL_DELTA_WPARAM(wParam)))
        return 1;
    return CallWindowProcW(original, window, message, wParam, lParam);
}
}

void updateWindowHotkeys() {
    auto platform = ll::service::getAppPlatform();
    if (!platform) return;
    auto const window = static_cast<AppPlatform_GameCore&>(*platform).mHWnd;
    if (!window || window == installedWindow) return;
    uninstallWindowHotkeys();

    auto const previous = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
    if (!previous) return;
    originalWndProc = previous;
    if (auto const replaced = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(windowProc))) {
        originalWndProc = reinterpret_cast<WNDPROC>(replaced);
        installedWindow = window;
    }
}

void uninstallWindowHotkeys() {
    if (!installedWindow) return;
    if (IsWindow(installedWindow))
        SetWindowLongPtrW(installedWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWndProc.load()));
    installedWindow = nullptr;
    structure::resetHotkeyState();
}
}
