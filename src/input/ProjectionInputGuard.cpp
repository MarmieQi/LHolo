// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "input/ProjectionInputGuard.h"

#include "structure/StructureLoader.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/deps/input/MouseDevice.h"

#include <Windows.h>
#include <atomic>

namespace lholo::input {
namespace {

bool gInstalled{};
std::atomic_uint64_t gRightPress{};

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
    if (actionButtonId == MouseAction::ActionRight && buttonData == MouseAction::DataDown) {
        if (auto client = ll::service::getClientInstance(); client && client->isInGameInputEnabled())
            gRightPress.store(GetTickCount64());
    }
    origin(actionButtonId, buttonData, x, y, dx, dy, forceMotionlessPointer);
}

} // namespace

bool installProjectionInputGuard() {
    if (!gInstalled) gInstalled = ProjectionMouseInputHook::hook() == 0;
    return gInstalled;
}

std::uint64_t takeGameplayRightPress() { return gRightPress.exchange(0); }

void uninstallProjectionInputGuard() {
    if (gInstalled) ProjectionMouseInputHook::unhook();
    gInstalled = false;
    gRightPress.store(0);
}

} // namespace lholo::input
