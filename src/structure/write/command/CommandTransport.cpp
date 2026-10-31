#include "structure/write/command/CommandTransport.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/network/ClientNetworkHandler.h"
#include "mc/network/PacketSender.h"
#include "mc/network/packet/CommandRequestPacket.h"
#include "mc/network/packet/CommandOutputPacket.h"
#include "mc/server/commands/CommandContext.h"
#include "mc/server/commands/PlayerCommandOrigin.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include <deque>
#include <mutex>
#include <unordered_map>

namespace lholo::structure::write::command {
namespace {
std::mutex mutex;
std::deque<Receipt> receipts;
std::unordered_map<mce::UUID, std::string> pending;
bool installed{};
LL_TYPE_INSTANCE_HOOK(CommandWriteOutput, ll::memory::HookPriority::Normal,
    ClientNetworkHandler, &ClientNetworkHandler::$handle, void,
    NetworkIdentifier const& source, CommandOutputPacket const& packet) {
    // Player origins need not preserve an arbitrary textual request ID. The
    // packet's origin UUID is the identity shared by request and response.
    std::optional<std::string> tracked;
    {
        std::lock_guard lock(mutex);
        auto request = pending.find(packet.mOriginData->mUUID.get());
        if (request != pending.end()) {
            tracked = std::move(request->second); pending.erase(request);
        }
    }
    if (!tracked) { origin(source, packet); return; }
    auto const& id = *tracked;
    Receipt receipt{id, Reply::Failed, "writeWorld.commandFailed"};
    if (packet.mOutput->mSuccessCount > 0) { receipt.reply = Reply::Success; receipt.error.clear(); }
    else for (auto const& message : packet.mOutput->mMessages) {
        auto const& key = message.mMessageId;
        if (key == "commands.setblock.noChange") {
            receipt.reply = Reply::NoChange; receipt.error = "writeWorld.commandVerify"; break;
        }
        if (key == "commands.setblock.outOfWorld" || key == "commands.generic.outOfWorld"
            || key == "commands.generic.destinationNotLoaded") {
            receipt.reply = Reply::Unloaded; receipt.error = "writeWorld.commandWaiting"; break;
        }
        if (key == "commands.generic.error.permissions" || key == "commands.generic.disabled"
            || key == "commands.generic.unknown") receipt.error = "writeWorld.commandPermission";
    }
    if (receipt.reply == Reply::Failed) {
        auto& logger = LHolo::getInstance().getSelf().getLogger();
        for (auto const& message : packet.mOutput->mMessages)
            logger.warn("[command-write] {}: {}", id, message.mMessageId);
    }
    std::lock_guard lock(mutex);
    receipts.push_back(std::move(receipt));
}
}
bool installCommandTransport() { if (!installed) installed = CommandWriteOutput::hook() == 0; return installed; }
void clearCommandReceipts() { std::lock_guard lock(mutex); receipts.clear(); pending.clear(); }
void forgetCommand(std::string const& id) {
    std::lock_guard lock(mutex);
    std::erase_if(pending, [&](auto const& entry) { return entry.second == id; });
}
void uninstallCommandTransport() {
    if (installed) CommandWriteOutput::unhook(); installed = false; clearCommandReceipts();
}
void sendCommand(ClientInstance& client, LocalPlayer& player, Dispatch const& request) {
    CommandContext context{request.text,
        std::make_unique<PlayerCommandOrigin>(player.getLevel(), player.getOrCreateUniqueID()),
        static_cast<int>(CurrentCmdVersion::Latest)};
    CommandRequestPacketPayload payload{context, false};
    CommandRequestPacket packet{std::move(payload)};
    {
        std::lock_guard lock(mutex);
        pending.emplace(packet.mOrigin->mUUID.get(), request.id);
    }
    client.getPacketSender().sendToServer(packet);
}
std::optional<Receipt> takeCommandReceipt() {
    std::lock_guard lock(mutex);
    if (receipts.empty()) return {};
    auto result = std::move(receipts.front()); receipts.pop_front(); return result;
}
}
