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

#include "place/PlaceHelper.h"

#include "input/ProjectionInputGuard.h"
#include "place/PlacementState.h"
#include "place/ManualPlacementHooks.h"
#include "place/legacy/PlacementExecutor.h"
#include "place/local/LocalPlacement.h"

#include "plugin/LHolo.h"
#include "projection/Projection.h"
#include "structure/MaterialTracker.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/Tick.h"

#include <Windows.h>
#include <algorithm>

namespace lholo::place {
namespace {

auto& placementState() {
    return detail::PlacementState::getInstance();
}

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

void consumeBrokenProjectionCells(LocalPlayer& player, std::uint64_t now) {
    auto const brokenCells = projection::takeBrokenProjectionCells(player);
    auto const cooldownSeconds = placementState().autoPlacementBreakCooldownSeconds();
    if (cooldownSeconds <= 0) return;
    auto const cooldownMs = static_cast<std::uint64_t>(cooldownSeconds) * 1'000;
    for (auto const& broken : brokenCells) {
        // Use the event timestamp, not the consumption timestamp. Events held
        // while placement is inactive therefore still expire after the
        // configured real-time duration and never restart when placement resumes.
        if (now - broken.destroyedAt >= cooldownMs) continue;
        placementState().suppressAutoPlacement(
            detail::packPlacementCell(broken.x, broken.y, broken.z),
            broken.destroyedAt + cooldownMs
        );
    }
}

bool gTickHookInstalled{};

LL_TYPE_INSTANCE_HOOK(
    LocalPlayerEasyPlaceHook,
    ll::memory::HookPriority::Normal,
    LocalPlayer,
    &LocalPlayer::$tickWorld,
    void,
    ::Tick const& currentTick
) {
    structure::detail::tickMaterialTracker(*this);
    if (auto client = ll::service::getClientInstance(); client && client->getLocalPlayer() == this) {
        consumeBrokenProjectionCells(*this, GetTickCount64());
        pollManualInput(*this);
        detail::tickEasyPlace();
        local::submitCandidates(*this);
    }
    origin(currentTick);
}

} // namespace

void setEnabled(bool enabled) {
    if (enabled) {
        logger().info("Easy-place enabled");
    } else {
        logger().info("Easy-place disabled");
    }
    placementState().setEnabled(enabled);
}

bool isEnabled() {
    return placementState().enabled();
}

void setRangeEnabled(bool enabled) {
    if (enabled) {
        logger().info("Range placement enabled (radius {})", placementState().radius());
    } else {
        logger().info("Range placement disabled");
    }
    placementState().setRangeEnabled(enabled);
}

bool isRangeEnabled() {
    return placementState().rangeEnabled();
}

void setLocalPlacementMode(LocalPlacementMode mode) { local::setMode(mode); }
LocalPlacementMode getLocalPlacementMode() { return local::mode(); }
bool localRangeAvailable() { return local::available(); }

void setPlacementRadius(int radius) {
    placementState().setRadius(std::clamp(radius, 1, 4));
}

int getPlacementRadius() {
    return placementState().radius();
}

void setLocalPlacementRadius(int radius) { placementState().setLocalRadius(std::clamp(radius, 1, 64)); }
int getLocalPlacementRadius() { return placementState().localRadius(); }
void setLocalPlacementLiquidsDisabled(bool disabled) { placementState().setLocalLiquidsDisabled(disabled); }
bool getLocalPlacementLiquidsDisabled() { return placementState().localLiquidsDisabled(); }
void setLocalShulkerRestock(bool enabled) { placementState().setLocalShulkerRestock(enabled); }
bool getLocalShulkerRestock() { return placementState().localShulkerRestock(); }
void setLocalCreativeRestock(bool enabled) { placementState().setLocalCreativeRestock(enabled); }
bool getLocalCreativeRestock() { return placementState().localCreativeRestock(); }
void setLocalCreativeRestockOverwrite(bool enabled) { placementState().setLocalCreativeRestockOverwrite(enabled); }
bool getLocalCreativeRestockOverwrite() { return placementState().localCreativeRestockOverwrite(); }

void setAutoPlacementBreakCooldownSeconds(int seconds) {
    placementState().setAutoPlacementBreakCooldownSeconds(std::clamp(seconds, 0, 60));
}

int getAutoPlacementBreakCooldownSeconds() {
    return placementState().autoPlacementBreakCooldownSeconds();
}

void setManualMode(bool manual) {
    if (!manual) {
        // A release hook can be missed while menus or mode switches are active.
        // Never carry a stale press/hold request into the next manual session.
        placementState().resetManualInput();
    }
    placementState().setManualMode(manual);
}

bool isManualMode() {
    return placementState().manualMode();
}

AimedProjectedBlock getAimedProjectedBlock() {
    return placementState().aimedProjectedBlock();
}

void resetDimensionSession() {
    (void)input::takeGameplayRightPress();
    local::cancelPending();
    placementState().resetDimensionSession();
}

void resetWorldSession() {
    (void)input::takeGameplayRightPress();
    local::setMode(LocalPlacementMode::Off);
    placementState().resetWorldSession();
}

bool installHook() {
    gTickHookInstalled = LocalPlayerEasyPlaceHook::hook() == 0;
    if (!gTickHookInstalled) {
        logger().error("Failed to install easy-place tick hook");
        return false;
    }
    installManualHooks();
    local::install();
    return true;
}

void uninstallHook() {
    local::uninstall();
    uninstallManualHooks();
    if (gTickHookInstalled) LocalPlayerEasyPlaceHook::unhook();
    gTickHookInstalled = false;
}

} // namespace lholo::place
