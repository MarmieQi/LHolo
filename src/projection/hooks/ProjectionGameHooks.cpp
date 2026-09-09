// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/hooks/ProjectionGameHooks.h"

#include "overlay/ImGuiOverlay.h"
#include "plugin/LHolo.h"
#include "projection/world/ProjectionVirtualWorld.h"
#include "structure/StructureLoader.h"

#include <cstddef>
#include <string_view>

#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"

namespace lholo::projection::detail {
namespace {

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

bool isMenuCommand(std::string_view message) {
    constexpr std::string_view command{"lholo"};
    if (message.size() != command.size()) return false;
    for (std::size_t index = 0; index < command.size(); ++index) {
        auto character = message[index];
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        if (character != command[index]) return false;
    }
    return true;
}

bool filterProjectionPacket(Packet& packet) {
    if (packet.getId() != MinecraftPacketIds::Text) return false;
    auto& textPacket = static_cast<TextPacket&>(packet);
    if (textPacket.getType() != TextPacketType::Chat
        || !isMenuCommand(textPacket.getMessage())) return false;

    if (overlay::ensureInstalled()) {
        structure::requestOpenGui();
    } else {
        logger().error("Could not initialize the injected ImGui overlay");
    }
    return true;
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetBlockHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<Block const& (BlockSource::*)(BlockPos const&) const>(&BlockSource::$getBlock),
    Block const&,
    BlockPos const& position
) {
    if (auto const* block = findTessellationBlock(position)) return *block;
    return origin(position);
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetBlockLayerHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<Block const& (BlockSource::*)(BlockPos const&, uint) const>(&BlockSource::$getBlock),
    Block const&,
    BlockPos const& position,
    uint layer
) {
    if (layer == 0) {
        if (auto const* block = findTessellationBlock(position)) return *block;
    }
    return origin(position, layer);
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetBlockEntityHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<BlockActor const* (BlockSource::*)(BlockPos const&) const>(&BlockSource::$getBlockEntity),
    BlockActor const*,
    BlockPos const& position
) {
    if (auto const* actor = findTessellationBlockActor(position)) return actor;
    return origin(position);
}

LL_TYPE_INSTANCE_HOOK(
    LoopbackPacketSenderSendToServerHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    &LoopbackPacketSender::$sendToServer,
    void,
    Packet& packet
) {
    if (filterProjectionPacket(packet)) return;
    origin(packet);
}

LL_TYPE_INSTANCE_HOOK(
    LoopbackPacketSenderSendHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    &LoopbackPacketSender::$send,
    void,
    Packet& packet
) {
    if (filterProjectionPacket(packet)) return;
    origin(packet);
}

} // namespace

bool installProjectionGameHooks() {
    if (BlockSourceGetBlockHook::hook() < 0) return false;
    if (BlockSourceGetBlockLayerHook::hook() < 0) {
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    if (BlockSourceGetBlockEntityHook::hook() < 0) {
        BlockSourceGetBlockLayerHook::unhook();
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    if (LoopbackPacketSenderSendToServerHook::hook() < 0) {
        BlockSourceGetBlockEntityHook::unhook();
        BlockSourceGetBlockLayerHook::unhook();
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    if (LoopbackPacketSenderSendHook::hook() < 0) {
        LoopbackPacketSenderSendToServerHook::unhook();
        BlockSourceGetBlockEntityHook::unhook();
        BlockSourceGetBlockLayerHook::unhook();
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    return true;
}

void uninstallProjectionGameHooks() {
    LoopbackPacketSenderSendHook::unhook();
    LoopbackPacketSenderSendToServerHook::unhook();
    BlockSourceGetBlockEntityHook::unhook();
    BlockSourceGetBlockLayerHook::unhook();
    BlockSourceGetBlockHook::unhook();
}

} // namespace lholo::projection::detail
