#include "structure/StructureSession.h"

#include <atomic>
#include <cstdio>
#include <thread>

int runStructureLoadOriginTests() {
    using namespace lholo::structure::detail;
    using lholo::i18n::Message;
    using lholo::i18n::TextKey;
    int checks{}, failures{};
    auto check = [&](bool value) {
        ++checks;
        if (!value)
            ++failures;
    };
    auto makeStructure = [](std::uint64_t generation) {
        auto loaded = std::make_shared<lholo::structure::LoadedStructure>();
        loaded->generation = generation;
        return loaded;
    };
    auto& session = StructureSession::getInstance();
    session.clearLoaded(Message{TextKey::StatusProjectionClosed});
    session.resetTransform();
    check(!session.snapshot().hasVisibleSelection());

    StructureLoadOrigin clicked{10, 63, -21, 0};
    auto const request = session.beginFileLoad(clicked);
    clicked = {500, 99, 600, 1}; // simulate moving/changing dimension while parsing
    check(!session.activation().loaded && !session.activation().origin);
    auto first = makeStructure(100);
    check(session.completeFileLoad(request, first, "first.mcstructure", {}));
    check(session.snapshot().hasVisibleSelection()); // Legacy standalone load has no task source index.
    auto activation = session.activation();
    check(activation.loaded == first);
    check(activation.origin == std::optional{StructureLoadOrigin{10, 63, -21, 0}});
    check(!session.completeFileLoad(request, makeStructure(101), "duplicate.mcstructure", {}));

    // Starting/failing another load must not change the current projection's
    // origin or reset its transform. A failure owns no next-activation anchor.
    session.setRotation(2);
    auto failed = session.beginFileLoad(clicked);
    check(session.activation().origin == activation.origin);
    check(session.failFileLoad(failed, Message{TextKey::StatusLoadFailed, {"test"}}));
    check(session.activation().loaded == first && session.activation().origin == activation.origin);
    check(session.transform().rotation == 2);
    check(!session.completeFileLoad(failed, makeStructure(102), "failed.mcstructure", {}));

    // Close/world cleanup cancels both successful and failed late publication.
    auto canceled = session.beginFileLoad(clicked);
    session.clearLoaded(Message{TextKey::StatusProjectionClosed});
    auto const closed = session.snapshot().status;
    check(!session.completeFileLoad(canceled, makeStructure(103), "closed.mcstructure", {}));
    check(!session.failFileLoad(canceled, Message{TextKey::StatusLoadFailed, {"stale"}}));
    check(!session.activation().loaded && !session.activation().origin);
    check(session.snapshot().status == closed && session.transform().rotation == 2);

    // A newer request wins. Publication resets transform and binds structure
    // and origin under the same lock; older snapshots remain value snapshots.
    auto old = session.beginFileLoad(StructureLoadOrigin{-50, 70, -50, 0});
    StructureLoadOrigin newest{200, 80, 300, 1};
    auto current = session.beginFileLoad(newest);
    check(!session.completeFileLoad(old, makeStructure(104), "old.mcstructure", {}));
    check(!session.failFileLoad(old, Message{TextKey::StatusLoadFailed, {"old"}}));
    auto second = makeStructure(105);
    check(session.completeFileLoad(current, second, "second.mcstructure", {}));
    check(session.activation().loaded == second && session.activation().origin == std::optional{newest});
    check(session.transform().rotation == 0);
    check(activation.loaded == first && activation.origin->x == 10);

    // Saved restoration carries its saved origin atomically and invalidates an
    // older ordinary load. Saved coordinates retain their existing dimension policy.
    auto interrupted = session.beginFileLoad(clicked);
    SavedProjectionSnapshot saved;
    saved.available = true;
    saved.anchorX = -100;
    saved.anchorY = 45;
    saved.anchorZ = 75;
    saved.structurePath = "saved.mcstructure";
    saved.transform.rotation = 3;
    saved.transform.offsetY = 8;
    auto restored = makeStructure(106);
    auto restoreRequest = session.beginFileLoad({saved.anchorX, saved.anchorY, saved.anchorZ, std::nullopt});
    check(session.completeRestoreLoad(restoreRequest, restored, saved, Message{TextKey::StatusRestoredPending}));
    auto const restoration = session.activation();
    check(restoration.loaded == restored);
    check(restoration.origin == std::optional{StructureLoadOrigin{-100, 45, 75, std::nullopt}});
    check(session.transform().rotation == 3 && session.transform().offsetY == 8);
    check(!session.completeFileLoad(interrupted, makeStructure(107), "interrupted.mcstructure", {}));
    check(session.activation().origin == restoration.origin);

    // A file loaded after restoration cannot inherit the saved origin.
    auto afterRestore = session.beginFileLoad(clicked);
    check(session.completeFileLoad(afterRestore, makeStructure(108), "new.mcstructure", {}));
    check(session.activation().origin == std::optional{clicked});

    // A late restore cannot re-open a closed/new world or overwrite a newer
    // ordinary load. Its saved transform must not leak on rejected publication.
    auto staleRestore = session.beginFileLoad({});
    session.clearLoaded(Message{TextKey::StatusWorldExited});
    check(!session.completeRestoreLoad(staleRestore, restored, saved, {}));
    check(!session.activation().loaded && !session.activation().origin);
    check(session.transform().rotation == 0);
    staleRestore = session.beginFileLoad({});
    auto newerLoad = session.beginFileLoad(clicked);
    check(session.completeFileLoad(newerLoad, second, "newer.mcstructure", {}));
    check(!session.completeRestoreLoad(staleRestore, restored, saved, {}));
    check(session.activation().loaded == second && session.transform().rotation == 0);
    auto beforeShutdown = session.beginFileLoad({});
    session.cancelPendingLoad();
    check(!session.completeRestoreLoad(beforeShutdown, restored, saved, {}));
    check(!session.completeFileLoad(beforeShutdown, first, "shutdown.mcstructure", {}));
    check(!session.failFileLoad(beforeShutdown, {}));
    check(session.activation().loaded == second);

    // Exercise the actual publication/clear mutex race in both possible orders.
    for (int run = 0; run < 64; ++run) {
        auto ticket = session.beginFileLoad(StructureLoadOrigin{run, 60, run, 0});
        auto parsed = makeStructure(200 + run);
        std::atomic<bool> start{};
        std::thread worker([&] {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            session.completeFileLoad(ticket, parsed, "race.mcstructure", {});
        });
        start.store(true, std::memory_order_release);
        session.clearLoaded(Message{TextKey::StatusProjectionClosed});
        worker.join();
        check(!session.activation().loaded && !session.activation().origin);
    }
    for (int run = 0; run < 64; ++run) {
        auto ticket = session.beginFileLoad({});
        std::atomic<bool> start{};
        std::thread worker([&] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            session.completeRestoreLoad(ticket, restored, saved, {});
        });
        start.store(true, std::memory_order_release);
        session.clearLoaded(Message{TextKey::StatusWorldExited});
        worker.join();
        check(!session.activation().loaded && !session.activation().origin);
    }
    session.resetTransform();
    std::printf("StructureLoadOriginTests: %d checks, %d failures\n", checks, failures);
    return failures;
}
