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

#include "place/PlacementExecutor.h"
#include "place/PlacementState.h"

#include "plugin/LHolo.h"
#include "structure/StructureLoader.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"

#include <Windows.h>

#include <array>
#include <string_view>

#include <algorithm>

namespace lholo::place {
namespace {

auto& placementState() {
    return detail::PlacementState::getInstance();
}

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

LL_TYPE_INSTANCE_HOOK(
    LocalPlayerEasyPlaceHook,
    ll::memory::HookPriority::Normal,
    LocalPlayer,
    &LocalPlayer::$tickWorld,
    void,
    ::Tick const& currentTick
) {
    detail::tickEasyPlace();
    origin(currentTick);
}

// Returns true when manual mode is on and `gm` belongs to the local player, i.e.
// this is the client-side right-click we should take over. The local-player
// check is essential: the server processes LHolo's own placement through these
// same functions on the ServerPlayer, and that must not be suppressed.
bool isLocalManualBuild(GameMode& gm) {
    if (!placementState().manualMode()) return false;
    auto client = ll::service::getClientInstance();
    auto* localPlayer = client ? client->getLocalPlayer() : nullptr;
    return localPlayer && &gm.mPlayer == static_cast<Player*>(localPlayer);
}

// Blocks whose right-click opens/uses them rather than placing. Manual mode must
// let vanilla handle these so a chest still opens and a repeater's delay can be
// changed even while the crosshair sits inside a dense projection.
bool blockAcceptsRightClick(Block const& block) {
    if (block.hasBlockEntity()) return true;  // chest/furnace/hopper/sign/barrel/...
    std::string_view name = block.getTypeName();
    if (name.starts_with("minecraft:")) name.remove_prefix(sizeof("minecraft:") - 1);
    static constexpr std::string_view kExact[]{
        "repeater", "unpowered_repeater", "powered_repeater",
        "comparator", "unpowered_comparator", "powered_comparator",
        "lever", "daylight_detector", "daylight_detector_inverted",
        "crafting_table", "cartography_table", "fletching_table", "smithing_table",
        "loom", "stonecutter_block", "grindstone", "enchanting_table",
        "brewing_stand", "beacon", "cake", "composter", "respawn_anchor",
        "lodestone", "dragon_egg", "flower_pot", "noteblock", "jukebox",
        "cauldron", "lava_cauldron", "lectern", "beehive", "bee_nest", "crafter",
    };
    for (auto const& exact : kExact) if (name == exact) return true;
    static constexpr std::string_view kSuffix[]{
        "_button", "_door", "_trapdoor", "_fence_gate", "_bed", "_anvil",
    };
    for (auto const& suffix : kSuffix) if (name.ends_with(suffix)) return true;
    return false;
}

bool aimedBlockAcceptsRightClick(GameMode& gm, BlockPos const& pos) {
    return blockAcceptsRightClick(gm.mPlayer.getDimensionBlockSource().getBlock(pos));
}

// Manual-mode press edge. If the aimed block is interactive (chest, repeater,
// ...) we let vanilla open/use it. Otherwise we take the right button over: on a
// projection target LHolo places it (from tickEasyPlace), and off-target we block
// the accidental placement and show a one-shot JE-style hint. The vanilla build
// is never allowed through, so no stray block is placed.
LL_TYPE_INSTANCE_HOOK(
    GameModeStartBuildHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$startBuildBlock,
    void,
    ::BlockPos const& pos,
    uchar             face
) {
    if (isLocalManualBuild(*this)) {
        if (aimedBlockAcceptsRightClick(*this, pos)) {
            origin(pos, face);  // let vanilla open/use the block
            return;
        }
        placementState().setManualPressAt(GetTickCount64());
        placementState().setManualHeld(true);
        if (detail::manualTargetUnderCrosshair()) {
            placementState().setManualPlaceRequested(true);
        } else {
            structure::showActionHint("已被手动放置模式阻止（对准投影方块才能放置）");
        }
        return;  // LHolo owns this press; vanilla places nothing.
    }
    origin(pos, face);
}

// Right-clicking a floating projection targets air, so Bedrock calls useItem
// instead of startBuildBlock. Capture it only when a floating projection cell is
// under the crosshair; otherwise let vanilla use the item (eat, etc.).
LL_TYPE_INSTANCE_HOOK(
    GameModeUseItemHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$useItem,
    bool,
    ::ItemStack& item
) {
    if (isLocalManualBuild(*this) && detail::manualTargetUnderCrosshair()) {
        placementState().setManualPressAt(GetTickCount64());
        placementState().setManualPlaceRequested(true);
        placementState().setManualHeld(false);
        return false;
    }
    return origin(item);
}

// Manual-mode release edge: stop the repeat when the button is let go.
LL_TYPE_INSTANCE_HOOK(
    GameModeStopBuildHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$stopBuildBlock,
    void
) {
    if (isLocalManualBuild(*this)) {
        placementState().setManualHeld(false);
    }
    origin();
}

// GameMode::buildBlock is the vanilla continuous-build placement. In manual mode
// we always suppress it (LHolo drives placement from the press edge above): the
// interact for an interactive block already happened there, and this only ever
// carries a block PLACEMENT, which manual mode blocks. No hint here — the press
// edge shows it once, so holding the button never spams the notification.
LL_TYPE_INSTANCE_HOOK(
    GameModeBuildBlockHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$buildBlock,
    bool,
    ::BlockPos const& pos,
    uchar             face,
    bool const        isSimTick
) {
    if (isLocalManualBuild(*this)) {
        return false;
    }
    return origin(pos, face, isSimTick);
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

void setPlacementRadius(int radius) {
    placementState().setRadius(std::clamp(radius, 1, 4));
}

int getPlacementRadius() {
    return placementState().radius();
}

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
        placementState().setManualHeld(false);
        placementState().setManualPlaceRequested(false);
        placementState().setManualPressAt(0);
        placementState().setLastManualPlaceAt(0);
    }
    placementState().setManualMode(manual);
}

bool isManualMode() {
    return placementState().manualMode();
}

std::string getAimedProjectedBlockName() {
    return placementState().aimedProjectedBlockName();
}

bool installHook() {
    if (LocalPlayerEasyPlaceHook::hook() < 0) {
        logger().error("Failed to install easy-place tick hook");
        return false;
    }
    if (GameModeStartBuildHook::hook() < 0) {
        logger().warn("Failed to install manual-place start hook; manual mode will be unavailable");
    }
    if (GameModeUseItemHook::hook() < 0) {
        logger().warn("Failed to install manual-place air-use hook; floating manual placement will be unavailable");
    }
    if (GameModeStopBuildHook::hook() < 0) {
        logger().warn("Failed to install manual-place stop hook; manual mode may keep repeating");
    }
    if (GameModeBuildBlockHook::hook() < 0) {
        logger().warn("Failed to install manual-place build hook; manual mode may double-place");
    }
    return true;
}

void uninstallHook() {
    GameModeBuildBlockHook::unhook();
    GameModeStopBuildHook::unhook();
    GameModeUseItemHook::unhook();
    GameModeStartBuildHook::unhook();
    LocalPlayerEasyPlaceHook::unhook();
}

} // namespace lholo::place
