#include "app/StructureWriteActions.h"
#include "app/TaskActions.h"
#include "app/TaskContext.h"
#include "structure/StructureFileLoader.h"
#include "structure/write/StructureWritePlan.h"
#include "structure/write/local/LocalWorldWriter.h"
#include <mutex>
#include <optional>
#include <utility>

namespace lholo::app {
namespace {
std::mutex mutex;
std::optional<StructureWriteIntent> pending;
StructureWriteStatus status;
std::uint64_t observedWorld{};
}
bool submitStructureWrite(StructureWriteIntent intent) {
    std::lock_guard lock(mutex);
    if (!intent.id || intent.id <= status.ack || pending || status.busy) return false;
    pending = std::move(intent);
    return true;
}
StructureWriteStatus structureWriteStatus() { std::lock_guard lock(mutex); return status; }
void resetStructureWriteActions() {
    std::lock_guard lock(mutex);
    pending.reset();
    structure::write::cancelLocalWrite();
    status.busy = false;
}
void processStructureWriteActions() {
    using namespace structure::write;
    std::lock_guard lock(mutex);
    auto const world = detail::taskWorldRevision();
    if (world != observedWorld) {
        cancelLocalWrite();
        status.result = status.busy ? WriteResult::ContextChanged : WriteResult::None;
        status.detail.clear();
        status.busy = false;
        observedWorld = world;
    }
    auto player = detail::taskPlayerOrigin();
    status.worldRevision = world;
    status.localAvailable = player.has_value() && localWorldAvailable();
    if (auto result = takeLocalWriteResult()) {
        status.busy = false;
        status.result = result->result;
        status.detail = std::move(result->detail);
    }
    if (!pending) return;
    auto request = std::move(*std::exchange(pending, {}));
    status.ack = request.id;
    status.detail.clear();
    status.result = WriteResult::NoLocalWorld;
    if (!status.localAvailable || request.mode != WriteMode::LocalWorld) return;
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
    WriteTarget target{{file->x, file->y, file->z}, *file->dimension, file->rotation / 90,
        file->mirror == "x" ? 1 : file->mirror == "z" ? 2 : 0};
    std::array<int, 3> maximum;
    status.result = WriteResult::CoordinateLimit;
    if (!writeBounds(target, {loaded->sizeX, loaded->sizeY, loaded->sizeZ}, maximum)) return;
    try {
        auto plan = makeWritePlan(*loaded, target);
        plan.skipAir = request.skipAir;
        status.busy = queueLocalWrite(std::move(plan), world);
        status.result = status.busy ? WriteResult::None : WriteResult::NoLocalWorld;
    } catch (std::exception const& error) { status.result = WriteResult::Failed; status.detail = error.what(); }
}
}
