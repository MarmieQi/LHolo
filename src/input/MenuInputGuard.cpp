// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "input/MenuInputGuard.h"

#include "structure/StructureLoader.h"

#include "ll/api/memory/Hook.h"

#include "mc/deps/input/Keyboard.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/deps/input/MouseDevice.h"
#include "mc/deps/input/win/HIDControllerGameCoreDesktop.h"

#include <cstdint>

namespace lholo::input {
namespace {

MenuInputGuardStatus gInstallStatus{};
thread_local std::uint32_t gInputHandoffDepth{};

bool menuOwnsGameInput() {
    return gInputHandoffDepth == 0 && structure::isMenuInputCaptured();
}

bool projectionOwnsMouseWheel(char actionButtonId) {
    return actionButtonId == MouseAction::ActionWheel && structure::scrollLockActive();
}

LL_TYPE_INSTANCE_HOOK(
    MenuMouseInputHook,
    ll::memory::HookPriority::Highest,
    MouseDevice,
    &MouseDevice::feed,
    void,
    char  actionButtonId,
    schar buttonData,
    short x,
    short y,
    short dx,
    short dy,
    bool  forceMotionlessPointer
) {
    if (menuOwnsGameInput() || projectionOwnsMouseWheel(actionButtonId)) return;
    origin(actionButtonId, buttonData, x, y, dx, dy, forceMotionlessPointer);
}

LL_TYPE_INSTANCE_HOOK(
    MenuKeyDownInputHook,
    ll::memory::HookPriority::Highest,
    HIDControllerGameCoreDesktop,
    &HIDControllerGameCoreDesktop::$onKeyDown,
    void,
    int                                                 keyCode,
    Bedrock::Input::KeyboardEventProcessor::InputOrigin originType
) {
    if (menuOwnsGameInput() && keyCode != Keyboard::F11) return;
    origin(keyCode, originType);
}

LL_TYPE_INSTANCE_HOOK(
    MenuKeyUpInputHook,
    ll::memory::HookPriority::Highest,
    HIDControllerGameCoreDesktop,
    &HIDControllerGameCoreDesktop::$onKeyUp,
    void,
    int keyCode
) {
    if (menuOwnsGameInput() && keyCode != Keyboard::F11) return;
    origin(keyCode);
}

} // namespace

MenuInputHandoffScope::MenuInputHandoffScope() { ++gInputHandoffDepth; }

MenuInputHandoffScope::~MenuInputHandoffScope() { --gInputHandoffDepth; }

MenuInputGuardStatus installMenuInputGuard() {
    if (!gInstallStatus.mouseInputHookInstalled) {
        gInstallStatus.mouseInputHookInstalled = MenuMouseInputHook::hook() == 0;
    }
    if (!gInstallStatus.keyDownInputHookInstalled) {
        gInstallStatus.keyDownInputHookInstalled = MenuKeyDownInputHook::hook() == 0;
    }
    if (!gInstallStatus.keyUpInputHookInstalled) {
        gInstallStatus.keyUpInputHookInstalled = MenuKeyUpInputHook::hook() == 0;
    }
    return gInstallStatus;
}

void uninstallMenuInputGuard() {
    if (gInstallStatus.keyUpInputHookInstalled) MenuKeyUpInputHook::unhook();
    if (gInstallStatus.keyDownInputHookInstalled) MenuKeyDownInputHook::unhook();
    if (gInstallStatus.mouseInputHookInstalled) MenuMouseInputHook::unhook();

    gInstallStatus = {};
}

} // namespace lholo::input
