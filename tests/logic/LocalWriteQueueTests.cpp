#include "structure/write/local/LocalWriteQueue.h"
#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>

int runLocalWriteQueueTests() {
    using namespace lholo::structure::write;
    using namespace std::chrono_literals;
    int failures{};
    auto check = [&](bool valid, char const* message) {
        if (!valid) { ++failures; std::fprintf(stderr, "LocalWriteQueue: %s\n", message); }
    };
    LocalWriteQueue queue;
    check(!queue.enabled() && !queue.enqueue({}, 7), "stopped queue rejects requests");
    queue.enable();
    check(queue.enqueue({}, 7), "enabled queue accepts one request");
    check(!queue.enqueue({}, 7), "pending request cannot be replaced");
    int calls{};
    queue.run(8, [&](auto const&) { ++calls; return WriteResult::Complete; });
    auto stale = queue.take();
    check(calls == 0 && stale && stale->result == WriteResult::ContextChanged, "old-world requests never execute");
    check(!queue.take(), "completion is consumed once");

    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    check(queue.enqueue({}, 8), "next request accepted after consuming completion");
    auto running = std::async(std::launch::async, [&] {
        queue.run(8, [&](auto const&) { entered.set_value(); gate.wait(); return WriteResult::Complete; });
    });
    auto entry = entered.get_future();
    bool started = entry.wait_for(2s) == std::future_status::ready;
    check(started, "server writer entered");
    if (!started) { release.set_value(); running.get(); return failures; }
    auto polling = std::async(std::launch::async, [&] {
        return !queue.take() && queue.enabled() && !queue.enqueue({}, 8);
    });
    bool responsive = polling.wait_for(2s) == std::future_status::ready;
    check(responsive, "UI polling does not wait for a blocked native placement");
    // Always release the worker, including when the regression makes polling block.
    release.set_value();
    running.get();
    check(polling.get(), "in-flight request remains exclusive and has no premature result");
    auto done = queue.take();
    check(done && done->result == WriteResult::Complete, "completed write becomes readable");

    std::promise<void> cancelEntered, cancelRelease, cancelling;
    auto cancelGate = cancelRelease.get_future().share();
    queue.enqueue({}, 9);
    auto late = std::async(std::launch::async, [&] {
        queue.run(9, [&](auto const&) { cancelEntered.set_value(); cancelGate.wait(); return WriteResult::Complete; });
    });
    auto cancellationEntry = cancelEntered.get_future();
    started = cancellationEntry.wait_for(2s) == std::future_status::ready;
    check(started, "cancellation fixture entered");
    if (!started) { cancelRelease.set_value(); late.get(); return failures; }
    auto cancelled = std::async(std::launch::async, [&] { cancelling.set_value(); queue.cancel(); });
    cancelling.get_future().wait();
    check(cancelled.wait_for(0ms) != std::future_status::ready, "lifecycle cancel waits for active native placement");
    cancelRelease.set_value(); late.get(); cancelled.get();
    check(!queue.take() && queue.enqueue({}, 10), "cancel discards late completion and permits the next world");
    queue.cancel();
    queue.enqueue({}, 10);
    queue.disable();
    queue.run(10, [&](auto const&) { ++calls; return WriteResult::Complete; });
    check(!queue.enabled() && !queue.take() && calls == 0, "disable revokes pending and delayed tick callbacks");
    queue.enable();
    check(queue.enqueue({}, 11), "queue can be enabled again without old work");
    queue.run(11, [](auto const&) -> WriteResult { throw std::runtime_error("native failure"); });
    auto failed = queue.take();
    check(failed && failed->result == WriteResult::Failed && failed->detail == "native failure",
        "native exception becomes a completion with diagnostic details");
    check(queue.enqueue({}, 11), "exception releases the in-flight state");
    queue.run(11, [](auto const&) -> WriteResult { throw 1; });
    failed = queue.take();
    check(failed && failed->result == WriteResult::Failed && !failed->detail.empty(), "unknown exception also completes");
    std::printf("LocalWriteQueueTests: %d failures\n", failures);
    return failures;
}
