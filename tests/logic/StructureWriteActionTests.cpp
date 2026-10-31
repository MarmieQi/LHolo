#include "app/StructureWriteActions.h"
#include "app/TaskActions.h"
#include "app/TaskContext.h"
#include "structure/StructureLoader.h"
#include "structure/StructureFileLoader.h"
#include "structure/write/StructureWritePlan.h"
#include "structure/write/local/LocalWorldWriter.h"
#include "structure/write/command/CommandWorldWriter.h"
#include <cstdio>
#include <optional>
#include <utility>

namespace {
bool local = true;
std::optional<lholo::structure::write::StructureWritePlan> queued;
std::optional<lholo::structure::write::WriteCompletion> completion;
lholo::structure::write::command::Status commandStatus;
bool commandSkipPlaced{};
}
// Replace only native serialization and the server executor; use the real task
// fixture, catalog, cached-file selection and write application control frame.
namespace lholo::structure::write {
bool localWorldAvailable() { return local; }
StructureWritePlan makeWritePlan(LoadedStructure const& source, WriteTarget target) {
    StructureWritePlan plan;
    plan.target = target; plan.size = {source.sizeX, source.sizeY, source.sizeZ};
    return plan;
}
bool queueLocalWrite(StructureWritePlan plan, std::uint64_t) { queued = std::move(plan); return true; }
std::optional<WriteCompletion> takeLocalWriteResult() { return std::exchange(completion, {}); }
void cancelLocalWrite() { queued.reset(); completion.reset(); }
namespace command {
bool commandWorldAvailable() { return true; }
Status commandWriteStatus() { return commandStatus; }
void startCommandWrite(StructureWritePlan const&, std::uint64_t, bool skipPlaced) { commandSkipPlaced = skipPlaced; commandStatus = {State::Running,true,false,0,4}; }
void resumeCommandWrite() { commandStatus.state = State::Running; commandStatus.canResume = false; }
void cancelCommandWrite() { commandStatus.active = false; commandStatus.state = State::Cancelled; }
}
}

// Called while TaskActionsTests owns a successfully loaded selected-file fixture.
int runStructureWriteActionTests(lholo::structure::detail::StructureLoadOrigin& playerOrigin) {
    using namespace lholo;
    using namespace structure::write;
    int failures{};
    auto check = [&](bool valid, char const* message) {
        if (!valid) { ++failures; std::fprintf(stderr, "World-write actions: %s\n", message); }
    };
    app::resetStructureWriteActions();
    app::processStructureWriteActions();
    auto task = app::getTaskSnapshot();
    auto* selectedTask = tasks::findTask(task.catalog, task.activeTaskId);
    check(selectedTask != nullptr, "active task fixture exists");
    if (!selectedTask) return failures;
    auto* file = tasks::findFile(*selectedTask, selectedTask->selectedFileId);
    check(file != nullptr, "selected file fixture exists");
    if (!file) return failures;
    auto request = [&] {
        return app::StructureWriteIntent{app::structureWriteStatus().ack + 1, task.revision,
            app::detail::taskWorldRevision(), selectedTask->id, file->id};
    };
    auto send = [&](app::StructureWriteIntent intent) {
        check(app::submitStructureWrite(std::move(intent)), "request enqueued");
        app::processStructureWriteActions();
    };
    local = false; send(request());
    check(!queued && app::structureWriteStatus().result == WriteResult::NoLocalWorld, "remote connection cannot write");
    local = true;
    check(app::structureWriteStatus().dimension == file->dimension, "writer publishes the player's dimension");
    auto const originalDimension = playerOrigin.dimensionId;
    playerOrigin.dimensionId = *file->dimension + 1;
    send(request());
    check(!queued && app::structureWriteStatus().result == WriteResult::NoSelection
        && app::structureWriteStatus().dimension == playerOrigin.dimensionId,
        "dimension change is published and blocks even a manually submitted write");
    playerOrigin.dimensionId = originalDimension;
    // The fixture cache was created from mutable LoadedStructure objects.
    auto source = std::const_pointer_cast<structure::LoadedStructure>(structure::detail::cachedTaskFile(file->path));
    auto const oldUnmapped = source->unmappedBlocks;
    source->unmappedBlocks = 1;
    for (bool skipAir : {false, true}) {
        auto unmapped = request(); unmapped.skipAir = skipAir;
        send(unmapped);
        check(!queued && !app::structureWriteStatus().busy
            && app::structureWriteStatus().result == WriteResult::UnmappedBlocks,
            "mixed mapped/unmapped file is rejected before queueing in both air modes");
    }
    source->unmappedBlocks = oldUnmapped;
    auto stale = request(); ++stale.worldRevision; send(stale);
    check(!queued && app::structureWriteStatus().result == WriteResult::ContextChanged, "old world cannot write");
    stale = request(); ++stale.revision; send(stale);
    check(!queued && app::structureWriteStatus().result == WriteResult::ContextChanged, "stale task cannot write");
    auto missing = request(); missing.fileId = "another-file"; send(missing);
    check(!queued && app::structureWriteStatus().result == WriteResult::NoSelection, "client cannot choose a different file");
    local = false;
    auto commands = request(); commands.mode = WriteMode::ServerCommands; commands.skipPlaced = true; send(commands);
    check(!queued && app::structureWriteStatus().busy && commandStatus.active, "remote commands use independent executor");
    check(commandSkipPlaced && app::structureWriteStatus().skipPlaced, "placed-block option reaches the executor and active snapshot");
    auto const acceptedOperation = app::structureWriteStatus().operation;
    check(acceptedOperation == commands.id && !app::structureWriteStatus().finishedAt, "accepted command operation owns active HUD lifecycle");
    auto control = request(); control.action = app::StructureWriteIntent::Action::Cancel;
    ++control.worldRevision; send(control);
    check(commandStatus.active, "stale control cannot cancel active writing");
    commandStatus.state = command::State::Paused; commandStatus.canResume = true;
    control = request(); control.action = app::StructureWriteIntent::Action::Resume; send(control);
    check(commandStatus.state == command::State::Running, "paused writing can resume");
    check(app::structureWriteStatus().operation == acceptedOperation, "resume acknowledgement does not replace the HUD operation identity");
    control = request(); control.action = app::StructureWriteIntent::Action::Cancel; send(control);
    check(!app::structureWriteStatus().busy && !commandStatus.active, "cancel releases active command job");
    check(app::structureWriteStatus().finishedAt != 0, "cancel records terminal time for HUD expiry");
    local = true;
    send(request());
    check(queued && app::structureWriteStatus().busy, "valid local request queued");
    check(app::structureWriteStatus().operation && !app::structureWriteStatus().finishedAt, "new local operation clears old terminal feedback");
    if (queued) {
        check(!queued->skipAir, "default request retains air-writing behavior");
        check(queued->target.position == std::array<int, 3>{file->x, file->y, file->z}, "native file coordinates used");
        check(queued->target.dimension == *file->dimension && queued->target.rotation == file->rotation / 90,
            "native dimension and rotation used");
    }
    check(!app::submitStructureWrite(request()), "duplicate while writing rejected");
    completion = WriteCompletion{WriteResult::Complete, {}};
    app::processStructureWriteActions();
    check(!app::structureWriteStatus().busy && app::structureWriteStatus().result == WriteResult::Complete,
        "server completion published");
    check(app::structureWriteStatus().finishedAt != 0, "local completion stamps actual finish for HUD expiry");
    queued.reset();
    auto skipAirRequest = request(); skipAirRequest.skipAir = true;
    send(skipAirRequest);
    check(queued && queued->skipAir, "application freezes skip-air choice in the queued plan");
    app::resetStructureWriteActions();
    check(!queued && !app::structureWriteStatus().busy && !app::structureWriteStatus().operation, "shutdown revokes queued operation and HUD feedback");
    std::fprintf(stdout, "StructureWriteActionTests: %d failures\n", failures);
    return failures;
}
