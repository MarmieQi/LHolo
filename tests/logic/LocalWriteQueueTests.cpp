#include "structure/write/local/LocalWriteQueue.h"
#include "structure/write/local/LocalWriteSession.h"
#include "structure/StructureTransform.h"
#include <chrono>
#include <cstdio>
#include <future>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>

int runLocalWriteQueueTests() {
    using namespace lholo::structure::write;
    using namespace std::chrono_literals;
    int failures{};
    auto check = [&](bool valid, char const* message) {
        if (!valid) { ++failures; std::fprintf(stderr, "LocalWriteQueue: %s\n", message); }
    };

    // 1. Oversized sparse bounding box (> INT_MAX total volume) succeeds via chunk slicing.
    {
        StructureWritePlan huge;
        huge.target = {{100, 64, 200}, 0, 0, 0};
        huge.size = {20000, 300, 20000};
        huge.palette = {"stone"};
        huge.cells = {{{0, 0, 0}, 0, -1, {}}, {{19999, 299, 19999}, 0, -1, {}}};
        huge.skipAir = true;
        LocalWriteSession session(huge);
        check(session.status().active && session.status().total == 2,
            "sparse oversized structure produces only non-empty chunks without INT_MAX overflow");
        check(session.yBounds() == std::array<int, 2>{64, 363}, "tight Y bounds track active cells");
        std::vector<LocalChunkCandidate> order;
        session.fillPendingOrder(std::array<int, 3>{20099, 64, 20199}, order);
        check(order.size() == 2 && order[0].cx == 1256 && order[0].cz == 1262,
            "pending chunks are ordered closest to the player first");
        auto sub = session.makeChunkPlan(order[0].index);
        check(sub && sub->size == std::array<int, 3>{4, 1, 8}
            && sub->target.position == std::array<int, 3>{20096, 363, 20192}
            && sub->cells.size() == 1
            && sub->cells[0].position == std::array<int, 3>{3, 0, 7}
            && sub->palette[sub->cells[0].block] == "stone",
            "chunk sub-plan shifts local coordinates and remaps palette");
    }

    // 2. All rotation/mirror combinations preserve exact world coordinates and omitted cells.
    for (int mirror = 0; mirror < 3; ++mirror) {
        for (int rotation = 0; rotation < 4; ++rotation) {
            StructureWritePlan plan;
            plan.target = {{-24, 60, 10}, 0, rotation, mirror};
            plan.size = {20, 4, 22};
            plan.regions = {{{0, 0, 0}, {20, 4, 22}}};
            plan.palette = {"stone", "glass"};
            std::set<std::tuple<int, int, int, std::string>> expected;
            for (int x : {0, 1, 15, 16, 19}) {
                for (int z : {0, 5, 16, 21}) {
                    int const y = (x + z) % 4;
                    int const pal = (x + z) % 2;
                    plan.cells.push_back({{x, y, z}, pal, -1, {}});
                    auto wp = lholo::structure::transformCell(x, y, z, 20, 22, mirror, rotation);
                    expected.emplace(wp[0] - 24, wp[1] + 60, wp[2] + 10, plan.palette[pal]);
                }
            }
            // Omit local cell (2, 1, 3)
            plan.omittedCells.push_back((2ULL * 4 + 1) * 22 + 3);
            LocalWriteSession session(plan);
            std::vector<LocalChunkCandidate> order;
            session.fillPendingOrder(std::nullopt, order);
            std::set<std::tuple<int, int, int, std::string>> actual;
            bool sawOmitted = false;
            for (auto const& c : order) {
                auto sub = session.makeChunkPlan(c.index);
                if (!sub) continue;
                for (auto const& cell : sub->cells) {
                    auto wp = lholo::structure::transformCell(
                        cell.position[0], cell.position[1], cell.position[2],
                        sub->size[0], sub->size[2], sub->target.mirror, sub->target.rotation);
                    actual.emplace(
                        wp[0] + sub->target.position[0],
                        wp[1] + sub->target.position[1],
                        wp[2] + sub->target.position[2],
                        sub->palette[cell.block]);
                }
                if (!sub->omittedCells.empty()) sawOmitted = true;
            }
            check(actual == expected && sawOmitted,
                "chunk slicing preserves exact world cell coordinates and omitted cells across transforms");
        }
    }

    // 3. Session state transitions: waiting chunks, resume, waiting dimension, complete, cancel.
    {
        StructureWritePlan plan;
        plan.target = {{0, 64, 0}, 0, 0, 0};
        plan.size = {32, 1, 16};
        plan.palette = {"stone"};
        plan.cells = {{{0, 0, 0}, 0, -1, {}}, {{20, 0, 0}, 0, -1, {}}};
        plan.skipAir = true;
        LocalWriteSession session(plan);
        session.setWaitingChunks();
        check(session.status().active && session.status().state == WriteState::WaitingChunks && session.status().canResume,
            "unloaded chunks transition session to WaitingChunks with resume");
        session.resume();
        check(session.status().state == WriteState::Running && !session.status().canResume, "resume returns to Running");
        session.setWaitingDimension();
        check(session.status().state == WriteState::WaitingDimension && !session.status().canResume,
            "dimension mismatch pauses without cancel");
        session.completeChunk(0);
        check(session.status().active && session.status().done == 1 && session.status().total == 2,
            "completing one chunk advances progress");
        session.completeChunk(1);
        check(!session.status().active && session.status().state == WriteState::Complete && session.status().done == 2,
            "completing all chunks finishes session");
    }

    // 4. Queue concurrency & lifecycle isolation.
    LocalWriteQueue queue;
    StructureWritePlan singleChunk;
    singleChunk.target = {{0, 64, 0}, 0, 0, 0};
    singleChunk.size = {2, 1, 2};
    singleChunk.palette = {"stone"};
    singleChunk.cells = {{{0, 0, 0}, 0, -1, {}}};
    singleChunk.skipAir = true;

    check(!queue.enabled() && !queue.start(singleChunk, 7), "stopped queue rejects requests");
    queue.enable();
    check(queue.start(singleChunk, 7), "enabled queue accepts one request");
    check(!queue.start(singleChunk, 7), "active request cannot be replaced");

    int calls{};
    queue.runTick(8,
        [&](int, std::array<int, 2> const&) { return LocalEnvironment{}; },
        [&](int, int, std::array<int, 3> const&, std::array<int, 3> const&) { return true; },
        [&](auto const&) { ++calls; return WriteResult::Complete; });
    auto stale = queue.take();
    check(calls == 0 && stale && stale->result == WriteResult::ContextChanged, "old-world requests never execute");
    check(!queue.take(), "completion is consumed once");

    // Waiting chunks state when no chunk is writable.
    check(queue.start(singleChunk, 8), "next request accepted after consuming completion");
    queue.runTick(8,
        [&](int, std::array<int, 2> const&) { return LocalEnvironment{LocalEnvironment::State::Ready, std::array<int, 3>{0, 64, 0}}; },
        [&](int, int, std::array<int, 3> const&, std::array<int, 3> const&) { return false; },
        [&](auto const&) { ++calls; return WriteResult::Complete; });
    check(calls == 0 && queue.status().active && queue.status().state == WriteState::WaitingChunks && queue.status().canResume,
        "unwritable chunks leave session waiting");
    queue.resume();
    check(queue.status().state == WriteState::Running, "resume updates waiting state");

    // UI polling does not wait for a blocked native placement.
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    auto running = std::async(std::launch::async, [&] {
        queue.runTick(8,
            [&](int, std::array<int, 2> const&) { return LocalEnvironment{LocalEnvironment::State::Ready, std::array<int, 3>{0, 64, 0}}; },
            [&](int, int, std::array<int, 3> const&, std::array<int, 3> const&) { return true; },
            [&](auto const&) { entered.set_value(); gate.wait(); return WriteResult::Complete; });
    });
    auto entry = entered.get_future();
    bool started = entry.wait_for(2s) == std::future_status::ready;
    check(started, "server writer entered");
    if (!started) { release.set_value(); running.get(); return failures; }
    auto polling = std::async(std::launch::async, [&] {
        return !queue.take() && queue.enabled() && queue.status().active && !queue.start(singleChunk, 8);
    });
    bool responsive = polling.wait_for(2s) == std::future_status::ready;
    check(responsive, "UI polling does not wait for a blocked native placement");
    release.set_value();
    running.get();
    check(polling.get(), "in-flight request remains exclusive and has no premature result");
    auto done = queue.take();
    check(done && done->result == WriteResult::Complete && !queue.status().active, "completed write becomes readable");

    // Lifecycle cancel waits for active placement and discards late result.
    std::promise<void> cancelEntered, cancelRelease, cancelling;
    auto cancelGate = cancelRelease.get_future().share();
    queue.start(singleChunk, 9);
    auto late = std::async(std::launch::async, [&] {
        queue.runTick(9,
            [&](int, std::array<int, 2> const&) { return LocalEnvironment{}; },
            [&](int, int, std::array<int, 3> const&, std::array<int, 3> const&) { return true; },
            [&](auto const&) { cancelEntered.set_value(); cancelGate.wait(); return WriteResult::Complete; });
    });
    auto cancellationEntry = cancelEntered.get_future();
    started = cancellationEntry.wait_for(2s) == std::future_status::ready;
    check(started, "cancellation fixture entered");
    if (!started) { cancelRelease.set_value(); late.get(); return failures; }
    auto cancelled = std::async(std::launch::async, [&] { cancelling.set_value(); queue.cancel(); });
    cancelling.get_future().wait();
    check(cancelled.wait_for(0ms) != std::future_status::ready, "lifecycle cancel waits for active native placement");
    cancelRelease.set_value();
    late.get();
    cancelled.get();
    check(!queue.take() && queue.status().state == WriteState::Cancelled && queue.start(singleChunk, 10),
        "cancel discards late completion and permits the next world");

    queue.disable();
    queue.runTick(10,
        [&](int, std::array<int, 2> const&) { return LocalEnvironment{}; },
        [&](int, int, std::array<int, 3> const&, std::array<int, 3> const&) { return true; },
        [&](auto const&) { ++calls; return WriteResult::Complete; });
    check(!queue.enabled() && !queue.take() && calls == 0, "disable revokes active session and delayed tick callbacks");

    queue.enable();
    check(queue.start(singleChunk, 11), "queue can be enabled again without old work");
    queue.runTick(11,
        [&](int, std::array<int, 2> const&) { return LocalEnvironment{}; },
        [&](int, int, std::array<int, 3> const&, std::array<int, 3> const&) { return true; },
        [](auto const&) -> WriteResult { throw std::runtime_error("native failure"); });
    auto failed = queue.take();
    check(failed && failed->result == WriteResult::Failed && failed->detail == "native failure",
        "native exception becomes a completion with diagnostic details");
    check(queue.start(singleChunk, 11), "exception releases the in-flight state");
    queue.runTick(11,
        [&](int, std::array<int, 2> const&) { return LocalEnvironment{}; },
        [&](int, int, std::array<int, 3> const&, std::array<int, 3> const&) { return true; },
        [](auto const&) -> WriteResult { throw 1; });
    failed = queue.take();
    check(failed && failed->result == WriteResult::Failed && !failed->detail.empty(), "unknown exception also completes");

    std::printf("LocalWriteQueueTests: %d failures\n", failures);
    return failures;
}
