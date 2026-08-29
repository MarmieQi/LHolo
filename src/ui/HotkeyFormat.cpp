// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "ui/HotkeyFormat.h"

#include <cstdio>
#include <string>
#include <string_view>

#include <Windows.h>

namespace lholo::ui {
namespace {

std::string wideToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    auto const size = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr
    );
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr
    );
    return result;
}

} // namespace

bool isModifierKey(unsigned int key) {
    return key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU
        || key == VK_LSHIFT || key == VK_RSHIFT
        || key == VK_LCONTROL || key == VK_RCONTROL
        || key == VK_LMENU || key == VK_RMENU
        || key == VK_LWIN || key == VK_RWIN;
}

std::string hotkeyName(unsigned int key) {
    if (key == 0) return "未设置";
    switch (key) {
    case VK_BACK: return "Backspace";
    case VK_DELETE: return "Delete";
    case VK_ESCAPE: return "Esc";
    case VK_RETURN: return "Enter";
    case VK_SPACE: return "Space";
    case VK_TAB: return "Tab";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    // Mouse buttons carry no keyboard scan code, so name them explicitly.
    case VK_MBUTTON: return "鼠标中键";
    case VK_XBUTTON1: return "鼠标侧键1";
    case VK_XBUTTON2: return "鼠标侧键2";
    default: break;
    }
    auto scanCode = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    if (key == VK_LEFT || key == VK_UP || key == VK_RIGHT || key == VK_DOWN
        || key == VK_PRIOR || key == VK_NEXT || key == VK_END || key == VK_HOME
        || key == VK_INSERT || key == VK_DELETE || key == VK_DIVIDE || key == VK_NUMLOCK) {
        scanCode |= 1u << 24;
    }
    wchar_t name[128]{};
    auto const length = GetKeyNameTextW(static_cast<LONG>(scanCode << 16), name, static_cast<int>(std::size(name)));
    if (length > 0) return wideToUtf8(std::wstring_view{name, static_cast<std::size_t>(length)});
    char fallback[24]{};
    std::snprintf(fallback, sizeof(fallback), "VK 0x%02X", key);
    return fallback;
}

std::string hotkeyChordName(unsigned int modifiers, unsigned int key) {
    if (key == 0) return "未设置";
    std::string result;
    if ((modifiers & kHotkeyModifierControl) != 0) result += "Ctrl + ";
    if ((modifiers & kHotkeyModifierAlt) != 0) result += "Alt + ";
    if ((modifiers & kHotkeyModifierShift) != 0) result += "Shift + ";
    result += hotkeyName(key);
    return result;
}

} // namespace lholo::ui
