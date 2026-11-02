#include "projection/core/ProjectionVisibilityRules.h"
#include "projection/correction/CorrectionFrameBudget.h"
#include "projection/correction/CorrectionScanScheduler.h"
#include "projection/correction/CorrectionWorkQueues.h"
#include "projection/world/ProjectionWorldBounds.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <limits>
#include <numeric>
#include <vector>

using namespace lholo::projection::detail;

int runCorrectionSchedulingTests() {
    int checks{}, failures{};
    auto check = [&](bool condition) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "Correction scheduling check %d failed\n", checks);
        }
    };
    using namespace std::chrono;
    using Clock = CorrectionFrameBudget::Clock;
    auto start = Clock::time_point{};
    CorrectionFrameBudget timed{microseconds{1200}, 16384, start};
    check(timed.tryConsumeAt(start));
    check(timed.tryConsumeAt(start + microseconds{400}));
    check(timed.tryConsumeAt(start + microseconds{800}));
    check(!timed.tryConsumeAt(start + microseconds{1200}));
    check(!timed.tryConsumeAt(start)); // Once exhausted, another file cannot revive it.
    check(timed.work() == 3);
    CorrectionFrameBudget limited{hours{1}, 5, start};
    for (int file = 0; file < 3; ++file)
        for (int i = 0; i < 3; ++i)
            limited.tryConsumeAt(start);
    check(limited.work() == 5);
    check(!limited.availableAt(start));
    CorrectionFrameBudget empty{microseconds{0}, 100, start};
    check(!empty.tryConsumeAt(start));

    CorrectionEventQueue events;
    for (std::uint64_t i = 0; i < 10000; ++i)
        events.push({-16, 4, 3}, i % 3 ? i : 0);
    events.push({2, 3, 4}, 0);
    check(events.front()->position == CorrectionPosition{-16, 4, 3});
    check(events.front()->destroyedAt == 9998);
    events.pop();
    check(events.front()->position == CorrectionPosition{2, 3, 4});
    events.pop();
    check(events.empty());
    events.push({1, 1, 1}, 300);
    events.push({1, 1, 1}, 0); // Re-placement does not erase destruction suppression.
    check(events.front()->destroyedAt == 300);
    events.clear();
    check(!events.front());

    CorrectionSubChunkQueue chunks;
    chunks.push({-1, -4, 2});
    chunks.push({-1, -4, 2}); // Duplicate before any scan does not request another pass.
    chunks.push({7, 0, 8});
    chunks.advance(32);
    check(chunks.front()->position == CorrectionPosition{7, 0, 8});
    chunks.push({-1, -4, 2}); // Data arrives after part of this chunk has been checked.
    chunks.advance(4096);
    check(chunks.front()->cell == 32);
    check(chunks.front()->position == CorrectionPosition{-1, -4, 2});
    chunks.advance(4064);
    check(chunks.front()->cell == 0);
    chunks.advance(4096);
    check(chunks.empty());
    chunks.push({0, 0, 0});
    chunks.advance(16);
    chunks.clear();
    check(!chunks.front());

    // World events outside a projection cannot occupy its correction queues.
    ProjectionWorldBounds bounds{{-17, -32, 31}, {16, 16, 65}};
    check(bounds.intersects({-17, -32, 31}, {-17, -32, 31}));
    check(bounds.intersects({15, 15, 64}, {15, 15, 64}));
    check(!bounds.intersects({16, 0, 40}, {16, 0, 40}));
    check(!bounds.intersects({-18, 0, 40}, {-18, 0, 40}));
    check(bounds.intersects({-32, -48, 16}, {-17, -33, 31}) == false);
    check(bounds.intersects({-32, -32, 16}, {-17, -17, 31}));
    check(!bounds.intersects({16, -16, 32}, {31, -1, 47}));
    ProjectionWorldBounds wide{{-2147483648LL, 0, 0}, {-2147483632LL, 16, 16}};
    check(wide.intersects({-2147483648LL, 0, 0}, {-2147483633LL, 15, 15}));
    check(!wide.intersects({2147483632LL, 0, 0}, {2147483647LL, 15, 15}));
    check(!ProjectionWorldBounds{}.intersects({0, 0, 0}, {15, 15, 15}));

    // One quantum per simulated frame: every class and file must receive
    // service even with permanent, nearer foreground events in file zero.
    constexpr std::size_t files = 19;
    std::array<std::array<int, 4>, files> services{};
    CorrectionWorkCursor cursor;
    for (int frame = 0; frame < 3000; ++frame) {
        CorrectionFrameBudget frameBudget{microseconds{1200}, 1, start};
        auto work = selectCorrectionWork(files, cursor,
                                         [](std::size_t file, CorrectionWorkKind) -> std::optional<CorrectionPriority> {
                                             return CorrectionPriority{0, static_cast<float>(file * file)};
                                         });
        check(work.has_value());
        if (!work)
            break;
        check(frameBudget.tryConsumeAt(start));
        ++services[work->file][static_cast<std::size_t>(work->kind)];
        check(!frameBudget.tryConsumeAt(start));
    }
    for (auto const& file : services)
        for (auto count : file)
            check(count > 0);
    check(services[0][0] > services[18][0]);
    check(services[0][2] > services[18][2]);
    auto none = [](std::size_t, CorrectionWorkKind) -> std::optional<CorrectionPriority> { return std::nullopt; };
    check(!selectCorrectionWork(0, cursor, none));
    check(!selectCorrectionWork(files, cursor, none));
    auto one =
        selectCorrectionWork(1, cursor, [](std::size_t, CorrectionWorkKind kind) -> std::optional<CorrectionPriority> {
            if (kind == CorrectionWorkKind::Loaded)
                return CorrectionPriority{0, 0};
            return std::nullopt;
        });
    check(one && one->file == 0 && one->kind == CorrectionWorkKind::Loaded);

    // Nearby sections at the far end of a huge layout are selected immediately.
    std::vector<std::vector<std::size_t>> sections(16384);
    for (std::size_t i = 0; i < sections.size(); ++i)
        sections[i] = {i};
    CorrectionScanScheduler scan;
    scan.reset(sections);
    std::size_t probes{};
    auto priority = [&](std::size_t section) {
        ++probes;
        return CorrectionPriority{0, static_cast<float>(16384 - section)};
    };
    auto selected = scan.select(priority, 16383);
    check(selected && *selected == 16383);
    check(probes <= 65);
    scan.advance(*selected, 1, 1);
    std::vector<bool> visited(16384);
    visited[16383] = true;
    while (!scan.empty()) {
        probes = 0;
        selected = scan.select(priority, 16383); // Stale hint to a completed section is harmless.
        check(selected && !visited[*selected]);
        check(probes <= 65);
        if (!selected)
            break;
        visited[*selected] = true;
        scan.advance(*selected, 1, 1);
    }
    for (auto seen : visited)
        check(seen);
    check(!scan.select(priority, std::nullopt));

    // Interrupt an unfinished section and resume exactly at its next cell.
    sections.assign(17, std::vector<std::size_t>(4096));
    scan.reset(sections);
    for (int quantum = 0; quantum < 17 * 8; ++quantum) {
        auto work = scan.select(
            [](std::size_t section) { return CorrectionPriority{section == 0 ? 0 : 1, static_cast<float>(section)}; },
            0);
        check(work.has_value());
        auto before = scan.offset(*work);
        scan.advance(*work, 1, 4096);
        check(scan.offset(*work) == before + 1);
    }
    for (std::size_t section = 0; section < 17; ++section)
        check(scan.offset(section) > 0);
    check(scan.offset(0) > scan.offset(16));
    sections = {{}, {1, 2}, {}};
    scan.reset(sections);
    selected = scan.select([](std::size_t) { return CorrectionPriority{}; }, 0);
    check(selected && *selected == 1);
    scan.advance(1, 1, 2);
    check(!scan.empty() && scan.offset(1) == 1);
    scan.advance(1, 1, 2);
    check(scan.empty());
    scan.reset({});
    check(scan.empty());

    // Interactive dirty geometry outranks nearer initial geometry; the
    // existing eighth-dispatch fairness still gives background work a turn.
    std::array<ProjectionBuildCandidate, 2> builds{
        {{true, false, true, true, false, 1, false}, {true, false, true, true, true, 100, true}}};
    auto readBuild = [&](std::size_t index) { return builds[index]; };
    check(selectProjectionBuild(2, 0, 0, readBuild) == std::optional<std::size_t>{1});
    check(selectProjectionBuild(2, 7, 0, readBuild) == std::optional<std::size_t>{0});
    builds[1].inFlight = true;
    check(selectProjectionBuild(2, 0, 0, readBuild) == std::optional<std::size_t>{0});

    std::printf("CorrectionSchedulingTests: %d checks, %d failures\n", checks, failures);
    return failures;
}
