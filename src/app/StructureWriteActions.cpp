#include "app/StructureWriteActions.h"
#include "app/TaskActions.h"
#include "app/TaskContext.h"
#include "structure/StructureFileLoader.h"
#include "structure/write/StructureWritePlan.h"
#include "structure/write/local/LocalWorldWriter.h"
#include "structure/write/command/CommandWorldWriter.h"
#include "structure/StructurePaths.h"
#include <mutex>
#include <optional>
#include <utility>
#include <string_view>
#include <chrono>

namespace lholo::app {
namespace {
std::mutex mutex;
std::optional<StructureWriteIntent> pending;
StructureWriteStatus status;
std::uint64_t observedWorld{};
std::uint64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void finishOperation() { if (status.operation && !status.finishedAt) status.finishedAt = nowMs(); }
}
bool submitStructureWrite(StructureWriteIntent intent) {
    std::lock_guard lock(mutex);
    if (!intent.id || intent.id <= status.ack || pending
        || (status.busy && intent.action == StructureWriteIntent::Action::Write)) return false;
    pending = std::move(intent);
    return true;
}
StructureWriteStatus structureWriteStatus() { std::lock_guard lock(mutex); return status; }
void resetStructureWriteActions() {
    std::lock_guard lock(mutex);
    pending.reset();
    structure::write::cancelLocalWrite();
    structure::write::command::cancelCommandWrite();
    status.busy = false;
    status.operation = status.finishedAt = 0;
}
void processStructureWriteActions() {
    using namespace structure::write;
    std::lock_guard lock(mutex);
    auto const world = detail::taskWorldRevision();
    if (world != observedWorld) {
        cancelLocalWrite();
        command::cancelCommandWrite();
        status.result = status.busy ? WriteResult::ContextChanged : WriteResult::None;
        status.detail.clear();
        status.busy = false;
        observedWorld = world;
        status.operation = status.finishedAt = 0;
    }
    auto player = detail::taskPlayerOrigin();
    status.worldRevision = world;
    status.dimension = player ? player->dimensionId : std::nullopt;
    status.localAvailable = player.has_value() && localWorldAvailable();
    status.commandAvailable = player.has_value() && command::commandWorldAvailable();
    status.command = command::commandWriteStatus();
    if (status.mode == WriteMode::ServerCommands && status.busy) {
        status.busy = status.command.active;
        if (!status.busy) {
            status.result = status.command.state == command::State::Complete ? WriteResult::Complete : WriteResult::ContextChanged;
            finishOperation();
        }
    }
    if (auto result = takeLocalWriteResult()) {
        status.busy = false;
        status.result = result->result;
        status.detail = std::move(result->detail);
        finishOperation();
    }
    if (!pending) return;
    auto request = std::move(*std::exchange(pending, {}));
    status.ack = request.id;
    status.detail.clear();
    if (request.action != StructureWriteIntent::Action::Write) {
        if (request.worldRevision != world || status.mode != WriteMode::ServerCommands || !status.command.active) return;
        if (request.action == StructureWriteIntent::Action::Cancel) {
            command::cancelCommandWrite(); status.busy = false; status.result = WriteResult::None;
            finishOperation();
        } else command::resumeCommandWrite();
        status.command = command::commandWriteStatus();
        return;
    }
    status.mode = request.mode;
    status.operation = status.finishedAt = 0;
    status.result = request.mode == WriteMode::LocalWorld ? WriteResult::NoLocalWorld : WriteResult::NoCommandWorld;
    if (request.mode == WriteMode::LocalWorld ? !status.localAvailable : !status.commandAvailable) return;
    status.result = WriteResult::ContextChanged;
    auto snapshot = getTaskSnapshot();
    if (request.worldRevision != world || request.revision != snapshot.revision || snapshot.loading
        || request.taskId != snapshot.activeTaskId) return;
    auto* task = tasks::findTask(snapshot.catalog, request.taskId);
    auto* file = task ? tasks::findFile(*task, request.fileId) : nullptr;
    status.result = WriteResult::NoSelection;
    if (!file || task->selectedFileId != request.fileId || !file->visible
        || !file->dimension || file->dimension != player->dimensionId) return;
    auto loaded = structure::detail::cachedTaskFile(file->path);
    if (!loaded) return;
    if (loaded->unmappedBlocks) {
        status.result = WriteResult::UnmappedBlocks;
        return;
    }
    WriteTarget target{{file->x, file->y, file->z}, *file->dimension, file->rotation / 90,
        file->mirror == "x" ? 1 : file->mirror == "z" ? 2 : 0};
    std::array<int, 3> maximum;
    status.result = WriteResult::CoordinateLimit;
    if (!writeBounds(target, {loaded->sizeX, loaded->sizeY, loaded->sizeZ}, maximum)) return;
    try {
        auto plan = makeWritePlan(*loaded, target);
        plan.skipAir = request.skipAir;
        status.skipAir = request.skipAir;
        status.skipPlaced = request.skipPlaced;
        status.mode = request.mode;
        status.targetName = structure::detail::pathToUtf8(structure::detail::pathFromUtf8(file->path).filename());
        if (request.mode == WriteMode::LocalWorld) {
            status.busy = queueLocalWrite(std::move(plan), world);
            status.result = status.busy ? WriteResult::None : WriteResult::NoLocalWorld;
            if (status.busy) status.operation = request.id;
        } else {
            command::startCommandWrite(plan, world, request.skipPlaced);
            status.command = command::commandWriteStatus();
            status.busy = status.command.active;
            status.result = status.busy ? WriteResult::None : WriteResult::Complete;
            status.operation = request.id;
            if (!status.busy) finishOperation();
        }
    } catch (std::exception const& error) {
        status.result = std::string_view(error.what()) == "writeWorld.commandUnsupported" ? WriteResult::CommandUnsupported
            : std::string_view(error.what()) == "writeWorld.heightLimit" ? WriteResult::HeightLimit : WriteResult::Failed;
        status.detail = error.what();
    }
}
}
