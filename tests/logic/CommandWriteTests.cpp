#include "structure/write/command/CommandWriteSession.h"
#include <cstdio>

int runCommandWriteTests() {
    using namespace lholo::structure::write::command;
    int failures{};
    auto check = [&](bool value, char const* message) {
        if (!value) { ++failures; std::fprintf(stderr, "Command write: %s\n", message); }
    };
    auto plan = [] {
        Plan p; p.dimensionName = "overworld"; p.palette = {"minecraft:stone"}; p.expected = {1};
        p.chunks = {{{0,0}, {{{0,64,0},0}, {{1,64,0},0}}}, {{3,0}, {{{48,64,0},0}}}};
        return p;
    };
    check(chunkCoordinate(-1) == -1 && chunkCoordinate(-16) == -1 && chunkCoordinate(-17) == -2,
        "negative positions use floor chunk coordinates");
    Session s(plan());
    check(!s.next(0, {0,64,0}, 1, "wrong-dimension"), "other dimension never sends");
    auto first = s.next(0, {0,64,0}, 0, "first");
    check(first && first->text == "/execute in overworld run setblock 0 64 0 minecraft:stone replace",
        "setblock binds the frozen dimension and uses absolute coordinates");
    auto second = s.next(0, {0,64,0}, 0, "second");
    check(second && second->target.position[0] == 1 && s.status().done == 0, "pipeline sends another target without waiting or counting it as complete");
    check(!s.next(0, {0,64,0}, 0, "duplicate"), "already reserved targets are not sent twice");
    s.receive("unrelated", Reply::Success, {});
    check(s.status().done == 0, "unrelated receipts cannot advance progress");
    s.receive("first", Reply::Success, {});
    check(s.status().done == 1, "acknowledgement completes its own target");
    s.receive("second", Reply::NoChange, {});
    s.verify("second", false);
    check(s.status().done == 1 && !s.next(200, {0,64,0}, 0, "unverified"), "no-change requires observed matching state");
    s.verify("second", true);
    check(s.status().done == 2 && !s.next(200, {0,64,0}, 0, "far"), "distant chunk stays pending");
    auto far = s.next(200, {32,64,0}, 0, "far");
    check(far && far->target.position[0] == 48, "moving brings the next nearby chunk into range");
    s.receive("far", Reply::Success, {});
    check(!s.status().active && s.status().state == State::Complete && !s.status().canResume, "completed task releases controls");

    Session retry(plan());
    for (int attempt = 1; attempt <= 3; ++attempt) {
        auto id = std::to_string(attempt);
        auto sent = retry.next(attempt * 1000, {(attempt % 2) * 16,64,0}, 0, id);
        check(sent && sent->target.attempts == static_cast<unsigned>(attempt), "moving does not reset retry count");
        retry.receive(id, Reply::Unloaded, "writeWorld.commandWaiting");
        check(!retry.next(attempt * 1000 + 200, {(attempt % 2) * 16,64,0}, 0, "stationary"),
            "unloaded chunk is not hammered each tick");
    }
    check(retry.status().canResume && retry.status().state == State::Paused, "three failures pause for manual retry");
    retry.resume();
    auto manual = retry.next(4000, {0,64,0}, 0, "manual");
    check(manual && manual->target.attempts == 1, "explicit continue permits a new attempt round");

    Session timeout(plan());
    timeout.next(0, {0,64,0}, 0, "unknown");
    timeout.next(5000, {0,64,0}, 0, "never-resend");
    check(timeout.status().state == State::Unknown && timeout.status().done == 0, "timeout is an unresolved result");
    timeout.resume();
    check(!timeout.next(6000, {0,64,0}, 0, "unloaded-retry"), "unloaded unknown target cannot be retried");
    check(timeout.retry("unknown"), "explicit retry releases only the inspected unknown request");
    timeout.resume();
    auto replacement = timeout.next(6100, {0,64,0}, 0, "replacement");
    check(replacement.has_value(), "manual inspection permits retrying an unconfirmed target");
    timeout.receive("unknown", Reply::Success, {});
    check(timeout.status().done == 0, "late original response cannot complete replacement request");
    timeout.cancel();
    timeout.receive("replacement", Reply::Success, {});
    check(!timeout.status().active && timeout.status().done == 0 && !timeout.next(6400, {0,64,0}, 0, "cancelled"),
        "cancel ignores late receipts and stops sending");
    auto large = plan(); large.chunks.clear();
    Chunk chunk; chunk.position = {0,0};
    auto const count = Session::Window + 16;
    for (unsigned i = 0; i < count; ++i)
        chunk.targets.push_back({{static_cast<int>(i % 16),64 + static_cast<int>(i / 256),static_cast<int>((i / 16) % 16)},0});
    large.chunks.push_back(std::move(chunk));
    Session pipeline(std::move(large));
    std::vector<Dispatch> sent;
    for (unsigned batch = 0; batch < Session::Window / Session::BatchSize; ++batch) {
        for (unsigned i = 0; i < Session::BatchSize; ++i) {
            auto request = pipeline.next(0, {0,64,0}, 0, "pipe:" + std::to_string(sent.size()));
            check(request.has_value(), "batch fills without awaiting individual receipts");
            if (request) sent.push_back(*request);
        }
    }
    check(sent.size() == Session::Window && pipeline.status().done == 0
        && !pipeline.next(200, {0,64,0}, 0, "window-full"), "pending window stays bounded until acknowledgements arrive");
    pipeline.receive(sent[30].id, Reply::Success, {});
    auto refill = pipeline.next(200, {0,64,0}, 0, "refill");
    check(refill && refill->target.position == std::array<int,3>{0,68,0} && pipeline.status().done == 1,
        "out-of-order receipt frees a slot without duplicating earlier targets");
    for (auto it = sent.rbegin(); it != sent.rend(); ++it) pipeline.receive(it->id, Reply::Success, {});
    check(pipeline.status().done == Session::Window, "duplicate receipts are ignored and reversed receipts complete exactly once");
    if (refill) pipeline.receive(refill->id, Reply::Success, {});
    for (unsigned i = 0; i < 15; ++i) {
        auto request = pipeline.next(250, {0,64,0}, 0, "finish:" + std::to_string(i));
        check(request.has_value(), "remaining targets stay available after a full window");
        if (request) pipeline.receive(request->id, Reply::Success, {});
    }
    check(pipeline.status().done == count && !pipeline.status().active, "all pipelined targets complete and release the plan");

    Session unplaceable(plan());
    unplaceable.next(0, {0,64,0}, 0, "unplaceable");
    unplaceable.receive("unplaceable", Reply::Skip, {});
    unplaceable.receive("unplaceable", Reply::Skip, {});
    check(unplaceable.status().state == State::Running && unplaceable.status().active
        && unplaceable.status().done == 1 && unplaceable.status().failedSkipped == 1,
        "placement rejection skips its own cell once without pausing");
    auto afterSkipFailure = unplaceable.next(1, {0,64,0}, 0, "after-failure");
    check(afterSkipFailure && afterSkipFailure->target.position[0] == 1, "placement rejection does not block following cells");
    unplaceable.receive("after-failure", Reply::Skip, {});
    check(unplaceable.status().active && unplaceable.status().failedSkipped == 2 && !unplaceable.status().canResume,
        "unplaceable block is skipped without pausing or retrying");
    unplaceable.next(2, {32,64,0}, 0, "last");
    unplaceable.receive("last", Reply::Success, {});
    check(unplaceable.status().state == State::Complete && unplaceable.status().done == 3
        && unplaceable.status().failedSkipped == 2, "terminal progress retains rejected blocks separately from successful cells");

    Session syntax(plan());
    syntax.next(0, {0,64,0}, 0, "syntax-error");
    syntax.receive("syntax-error", Reply::Failed, "writeWorld.commandSyntax");
    check(syntax.status().state == State::Paused && syntax.status().canResume
        && syntax.status().done == 0 && syntax.status().failedSkipped == 0 && syntax.status().error == "writeWorld.commandSyntax"
        && !syntax.next(1, {0,64,0}, 0, "must-not-send"), "syntax failures pause instead of skipping the target");

    Session diagnostics(plan());
    unsigned pauseEvents{};
    auto receiveDiagnostic = [&](std::string const& id, std::string error = "writeWorld.commandPermission") {
        bool const wasPaused = diagnostics.paused();
        diagnostics.receive(id, Reply::Failed, std::move(error));
        if (!wasPaused && diagnostics.paused()) ++pauseEvents;
    };
    diagnostics.next(0, {0,64,0}, 0, "batch-one");
    diagnostics.next(0, {0,64,0}, 0, "batch-two");
    receiveDiagnostic("batch-one"); receiveDiagnostic("batch-two", "writeWorld.commandFailed");
    check(pauseEvents == 1 && diagnostics.status().error == "writeWorld.commandPermission",
        "one failed batch produces a single pause event and preserves its original reason");
    diagnostics.resume();
    diagnostics.next(1, {0,64,0}, 0, "manual-retry"); receiveDiagnostic("manual-retry");
    check(pauseEvents == 2, "a new failure after manual resume reports one new pause event");

    Session paused(plan());
    paused.next(0, {0,64,0}, 0, "failed"); paused.next(0, {0,64,0}, 0, "late-success");
    paused.receive("failed", Reply::Failed, "writeWorld.commandPermission");
    paused.next(6000, {0,64,0}, 0, "must-not-send-on-timeout");
    check(paused.status().state == State::Paused && paused.status().error == "writeWorld.commandPermission",
        "late in-flight timeouts do not overwrite the cause of an existing pause");
    paused.receive("late-success", Reply::Success, {});
    check(paused.status().state == State::Paused && paused.status().done == 1
        && !paused.next(100, {0,64,0}, 0, "must-not-send"), "other batch replies cannot resume a failed task");
    paused.resume();
    auto retryFailed = paused.next(6001, {0,64,0}, 0, "retry-failed");
    check(retryFailed && retryFailed->target.position[0] == 0, "manual continue retries the failed cell without repeating confirmed cells");
    Session verification(plan());
    verification.next(0, {0,64,0}, 0, "already-present");
    verification.receive("already-present", Reply::NoChange, {});
    auto following = verification.next(0, {0,64,0}, 0, "following");
    check(following && following->target.position[0] == 1 && verification.status().done == 0,
        "pending no-change verification does not block other targets or count as complete");
    Session skipOff(plan());
    skipOff.next(0, {0,64,0}, 0, "off");
    check(!skipOff.skipExisting("off", true) && skipOff.status().done == 0,
        "default-off option keeps matching cells pending a real command response");
    auto matchingPlan = plan(); matchingPlan.skipPlaced = true;
    Session skipOn(std::move(matchingPlan));
    skipOn.next(0, {0,64,0}, 0, "match");
    check(!skipOn.skipExisting("match", false) && skipOn.status().done == 0,
        "different type or state is not skipped");
    check(skipOn.skipExisting("match", true) && skipOn.status().done == 1,
        "matching cell completes without a command response when enabled");
    skipOn.receive("match", Reply::Success, {});
    auto afterSkip = skipOn.next(0, {0,64,0}, 0, "after-skip");
    check(afterSkip && afterSkip->target.position[0] == 1 && skipOn.status().done == 1,
        "skipped positions are not repeated or counted again by stale replies");
    std::printf("CommandWriteTests: %d failures\n", failures);
    return failures;
}
