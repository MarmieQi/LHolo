// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "input/ProjectionInputGuard.h"

#include "structure/StructureLoader.h"

#include "ll/api/memory/Hook.h"

#include "mc/deps/input/MouseAction.h"
#include "mc/deps/input/MouseDevice.h"

namespace lholo::input {
namespace {

bool gInstalled{};

LL_TYPE_INSTANCE_HOOK(
    ProjectionMouseInputHook,
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
    if (actionButtonId == MouseAction::ActionWheel && structure::scrollLockActive()) return;
    origin(actionButtonId, buttonData, x, y, dx, dy, forceMotionlessPointer);
}

} // namespace

bool installProjectionInputGuard() {
    if (!gInstalled) gInstalled = ProjectionMouseInputHook::hook() == 0;
    return gInstalled;
}

void uninstallProjectionInputGuard() {
    if (gInstalled) ProjectionMouseInputHook::unhook();
    gInstalled = false;
}

} // namespace lholo::input
