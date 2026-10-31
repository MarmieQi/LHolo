#include "structure/write/command/CommandWorldWriter.h"
#include "structure/write/command/SetBlockCommandBuilder.h"
#include "structure/write/command/CommandTransport.h"
#include "structure/StructureFileLoader.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "structure/write/StructureWritePlan.h"
#include "mc/platform/UUID.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string_view>

namespace lholo::structure::write::command {
namespace {
std::mutex mutex;
std::optional<Session> session;
std::uint64_t revision{}, sequence{};
std::atomic_bool installed{};
bool resumeRequested{};
std::string requestPrefix;
void logPause(bool wasPaused, std::string_view id = {}, std::string_view messageIds = {}) {
    if (!wasPaused && session->paused())
        LHolo::getInstance().getSelf().getLogger().warn("[command-write] paused: {}; request={}; messages={}",
            session->status().error, id, messageIds);
}
auto milliseconds() { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count()); }
std::optional<bool> matches(LocalPlayer& player, Dispatch const& request) {
    if (!session || static_cast<int>(player.getDimensionId()) != session->dimension()) return {};
    auto& source = player.getDimensionBlockSource();
    auto const& p = request.target.position;
    BlockPos const pos{p[0], p[1], p[2]};
    if (!source.areChunksFullyLoaded(pos, pos)) return {};
    auto const& block = source.getBlock(pos);
    return request.target.palette == 0 ? block.isAir()
        : block.mSerializationIdHash == session->expected(request.target.palette);
}
void process(ClientInstance& client) {
    std::lock_guard lock(mutex);
    if (!session || !session->status().active) { clearCommandReceipts(); return; }
    if (revision != detail::structureFileWorldRevision()) { session->cancel(); clearCommandReceipts(); return; }
    auto* player = client.getLocalPlayer();
    while (auto receipt = takeCommandReceipt()) {
        bool const wasPaused = session->paused();
        session->receive(receipt->id, receipt->reply, std::move(receipt->error));
        logPause(wasPaused, receipt->id, receipt->messageIds);
    }
    if (client.$getClientInstanceState() != ClientInstanceState::Playing || !player) return;
    for (auto const& request : session->checks(resumeRequested)) {
        auto const observed = matches(*player, request);
        if (observed.value_or(false)) {
            session->verify(request.id, true); forgetCommand(request.id);
        } else if (resumeRequested && observed.has_value() && session->retry(request.id)) forgetCommand(request.id);
    }
    if (std::exchange(resumeRequested, false)) session->resume();
    auto const feet = player->getFeetBlockPos();
    unsigned sent{};
    for (unsigned scanned = 0; scanned < Session::ScanBudget && sent < Session::BatchSize; ++scanned) {
        bool const wasPaused = session->paused();
        auto request = session->next(milliseconds(), {feet.x,feet.y,feet.z}, static_cast<int>(player->getDimensionId()),
            requestPrefix + std::to_string(++sequence));
        logPause(wasPaused);
        if (!request) break;
        if (session->skipPlacedEnabled()
            && session->skipExisting(request->id, matches(*player, *request).value_or(false))) continue;
        sendCommand(client, *player, *request);
        ++sent;
    }
}
LL_TYPE_INSTANCE_HOOK(CommandWriteUpdate, ll::memory::HookPriority::Normal,
    ClientInstance, &ClientInstance::$update, bool, bool initialized) {
    bool const result = origin(initialized);
    auto client = ll::service::getClientInstance();
    if (client && &client.get() == this) {
        try { process(*this); }
        catch (std::exception const& error) {
            std::lock_guard lock(mutex);
            if (session && session->status().active && !session->paused()) {
                session->suspend();
                LHolo::getInstance().getSelf().getLogger().error("[command-write] paused: {}", error.what());
            }
        }
    }
    return result;
}
}
bool commandWorldAvailable() { return installed; }
bool installCommandWorldWriter() {
    if (installed) return true;
    if (!installCommandTransport()) return false;
    installed = CommandWriteUpdate::hook() == 0;
    if (!installed) uninstallCommandTransport();
    return installed;
}
void cancelCommandWrite() {
    std::lock_guard lock(mutex);
    if (session) session->cancel();
    resumeRequested = false; clearCommandReceipts();
}
void uninstallCommandWorldWriter() {
    if (installed) CommandWriteUpdate::unhook();
    installed = false; cancelCommandWrite(); uninstallCommandTransport();
}
Status commandWriteStatus() { std::lock_guard lock(mutex); return session ? session->status() : Status{}; }
void resumeCommandWrite() {
    std::lock_guard lock(mutex);
    resumeRequested = session && session->status().canResume;
}
void startCommandWrite(StructureWritePlan const& plan, std::uint64_t worldRevision, bool skipPlaced) {
    auto client = ll::service::getClientInstance();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player || static_cast<int>(player->getDimensionId()) != plan.target.dimension)
        throw std::runtime_error("World context changed before command writing");
    auto& region = player->getDimensionBlockSource();
    std::array<int,3> maximum;
    if (!writeBounds(plan.target, plan.size, maximum)) throw std::runtime_error("Invalid command write coordinates");
    if (plan.target.position[1] < region.getMinHeight() || maximum[1] >= region.getMaxHeight())
        throw std::range_error("writeWorld.heightLimit");
    // Validate command-only limitations before any network side effect.
    auto built = makeCommandPlan(plan);
    built.skipPlaced = skipPlaced;
    auto prefix = "lholo-world-write:" + mce::UUID::random().asString() + ':';
    std::lock_guard lock(mutex);
    clearCommandReceipts(); revision = worldRevision; resumeRequested = false;
    requestPrefix = std::move(prefix); sequence = 0;
    session.emplace(std::move(built));
}
}
