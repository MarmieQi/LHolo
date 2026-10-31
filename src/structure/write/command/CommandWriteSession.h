#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace lholo::structure::write::command {
inline int chunkCoordinate(int value) { return value / 16 - (value % 16 < 0); }
enum class State { Idle, Running, WaitingChunks, WaitingDimension, Paused, Unknown, Complete, Cancelled };
enum class Reply { Success, NoChange, Unloaded, Failed };
enum class TargetState { Pending, InFlight, Done };
struct Target {
    std::array<int, 3> position;
    int palette;
    unsigned attempts{};
    TargetState state{TargetState::Pending};
};
struct Chunk {
    std::array<int, 2> position;
    std::vector<Target> targets;
    std::size_t cursor{};
    std::optional<std::array<int, 2>> blockedFrom;
    std::size_t next{};
};
struct Plan {
    int dimension{};
    std::string dimensionName;
    std::vector<std::string> palette;
    std::vector<std::uint64_t> expected;
    bool skipPlaced{};
    std::vector<Chunk> chunks;
};
struct Status {
    State state{State::Idle};
    bool active{}, canResume{};
    std::uint64_t done{}, total{};
    std::string error;
    bool operator==(Status const&) const = default;
};
struct Dispatch { std::string id, text; Target target; };

// Reservations and completion are separate: out-of-order replies only finish
// their own target. The window bounds both network pressure and pending work.
class Session {
    struct Flight {
        Dispatch request;
        std::size_t chunk, target;
        std::array<int, 2> player;
        std::uint64_t sentAt;
        bool verify{}, unknown{};
    };
    Plan mPlan;
    Status mStatus;
    std::vector<Flight> mFlights;
    bool mPaused{};

    auto find(std::string const& id) {
        return std::find_if(mFlights.begin(), mFlights.end(), [&](Flight const& f) { return f.request.id == id; });
    }
    bool needsCheck() const {
        return std::any_of(mFlights.begin(), mFlights.end(), [](Flight const& f) { return f.verify || f.unknown; });
    }
    void complete(std::string const& id) {
        auto flight = find(id);
        auto& chunk = mPlan.chunks[flight->chunk];
        chunk.targets[flight->target].state = TargetState::Done;
        mFlights.erase(flight);
        while (chunk.cursor < chunk.targets.size() && chunk.targets[chunk.cursor].state == TargetState::Done) ++chunk.cursor;
        if (chunk.cursor == chunk.targets.size()) {
            std::vector<Target>().swap(chunk.targets); chunk.cursor = chunk.next = 0;
        }
        ++mStatus.done;
        if (mStatus.done == mStatus.total) {
            mStatus.state = State::Complete; mStatus.active = false; mStatus.canResume = false;
            mStatus.error.clear(); mPlan = {};
        } else if (!mPaused && !needsCheck()) {
            mStatus.state = State::Running; mStatus.canResume = false; mStatus.error.clear();
        }
    }
public:
    static constexpr unsigned Window = 1024;
    static constexpr unsigned BatchSize = 128;
    static constexpr unsigned ScanBudget = 4096;
    explicit Session(Plan plan) : mPlan(std::move(plan)) {
        for (auto const& chunk : mPlan.chunks) mStatus.total += chunk.targets.size();
        mStatus.active = mStatus.total != 0;
        mStatus.state = mStatus.active ? State::Running : State::Complete;
        if (!mStatus.active) mPlan = {};
        mFlights.reserve(Window);
    }
    Status const& status() const { return mStatus; }
    int dimension() const { return mPlan.dimension; }
    std::uint64_t expected(int palette) const { return mPlan.expected[palette]; }
    bool skipPlacedEnabled() const { return mPlan.skipPlaced; }
    bool skipExisting(std::string const& id, bool matches) {
        if (!mPlan.skipPlaced || !matches || find(id) == mFlights.end()) return false;
        complete(id); return true;
    }
    std::vector<Dispatch> checks(bool includeUnknown) const {
        std::vector<Dispatch> result;
        for (auto const& f : mFlights)
            if (f.verify || (includeUnknown && f.unknown)) result.push_back(f.request);
        return result;
    }
    void suspend() {
        for (auto& f : mFlights) f.unknown = true;
        mPaused = true; mStatus.state = State::Unknown; mStatus.canResume = true;
        mStatus.error = "writeWorld.commandUnknown";
    }
    std::optional<Dispatch> next(std::uint64_t now, std::array<int, 3> player, int dimension, std::string id) {
        if (!mStatus.active) return {};
        for (auto& f : mFlights) if (now - f.sentAt >= 5000) {
            f.unknown = true; mPaused = true;
            mStatus.state = State::Unknown; mStatus.canResume = true; mStatus.error = "writeWorld.commandUnknown";
        }
        if (mPaused) return {};
        if (dimension != mPlan.dimension) { mStatus.state = State::WaitingDimension; mStatus.canResume = false; return {}; }
        if (mFlights.size() == Window) return {};
        std::array<int, 2> const current{chunkCoordinate(player[0]), chunkCoordinate(player[2])};
        std::size_t best = mPlan.chunks.size(), targetIndex{};
        long long distance = std::numeric_limits<long long>::max();
        for (std::size_t i = 0; i < mPlan.chunks.size(); ++i) {
            auto const& c = mPlan.chunks[i];
            auto const dx = static_cast<long long>(c.position[0]) - current[0];
            auto const dz = static_cast<long long>(c.position[1]) - current[1];
            if (dx < -1 || dx > 1 || dz < -1 || dz > 1 || c.blockedFrom == current) continue;
            auto target = c.next;
            while (target < c.targets.size() && c.targets[target].state != TargetState::Pending) ++target;
            if (target == c.targets.size()) continue;
            auto const d = dx * dx + dz * dz;
            if (d < distance) { best = i; targetIndex = target; distance = d; }
        }
        if (best == mPlan.chunks.size()) {
            if (mFlights.empty()) { mStatus.state = State::WaitingChunks; mStatus.canResume = true; }
            return {};
        }
        auto& target = mPlan.chunks[best].targets[targetIndex];
        if (target.attempts >= 3) {
            mPaused = true; mStatus.state = State::Paused; mStatus.canResume = true;
            mStatus.error = "writeWorld.commandRetryLimit"; return {};
        }
        ++target.attempts;
        auto const& p = target.position;
        Dispatch request{std::move(id), "/execute in " + mPlan.dimensionName + " run setblock "
            + std::to_string(p[0]) + " " + std::to_string(p[1]) + " "
            + std::to_string(p[2]) + " " + mPlan.palette[target.palette] + " replace", target};
        mFlights.push_back({request, best, targetIndex, current, now});
        target.state = TargetState::InFlight;
        mPlan.chunks[best].next = targetIndex + 1;
        mStatus.state = State::Running; mStatus.canResume = false; mStatus.error.clear();
        return request;
    }
    void receive(std::string const& id, Reply reply, std::string error) {
        auto flight = find(id);
        if (flight == mFlights.end()) return;
        if (reply == Reply::NoChange) {
            flight->verify = true;
            if (!mPaused) { mStatus.state = State::Unknown; mStatus.canResume = true; mStatus.error = "writeWorld.commandVerify"; }
            return;
        }
        if (reply == Reply::Success) {
            complete(id);
            return;
        }
        auto const sentPlayer = flight->player;
        auto& c = mPlan.chunks[flight->chunk];
        auto& target = c.targets[flight->target];
        target.state = TargetState::Pending;
        c.next = std::min(c.next, flight->target);
        mFlights.erase(flight);
        if (target.attempts >= 3 || reply == Reply::Failed) {
            mPaused = true; mStatus.state = State::Paused; mStatus.canResume = true;
            mStatus.error = reply == Reply::Failed ? std::move(error) : "writeWorld.commandRetryLimit";
        } else if (reply == Reply::Unloaded) {
            c.blockedFrom = sentPlayer;
            if (!mPaused) { mStatus.state = State::WaitingChunks; mStatus.canResume = true; mStatus.error = std::move(error); }
        }
    }
    void verify(std::string const& id, bool matches) {
        auto f = find(id);
        if (matches && f != mFlights.end() && (f->verify || f->unknown)) complete(id);
    }
    // Explicit user retries discard only inspected, unresolved commands. Other
    // outstanding requests retain their identities and remain acknowledgement-driven.
    bool retry(std::string const& id) {
        auto f = find(id);
        if (f == mFlights.end() || (!f->verify && !f->unknown)) return false;
        auto& target = mPlan.chunks[f->chunk].targets[f->target];
        target.state = TargetState::Pending; target.attempts = 0;
        auto& c = mPlan.chunks[f->chunk]; c.next = std::min(c.next, f->target);
        mFlights.erase(f); return true;
    }
    void resume() {
        if (!mStatus.active || needsCheck()) return;
        mPaused = false; mStatus.canResume = false; mStatus.error.clear(); mStatus.state = State::Running;
        for (auto& c : mPlan.chunks) {
            c.blockedFrom.reset();
            for (auto& t : c.targets) if (t.state == TargetState::Pending) t.attempts = 0;
        }
    }
    void cancel() {
        mFlights.clear(); mPlan = {}; mStatus.error.clear();
        mStatus.active = false; mStatus.canResume = false; mStatus.state = State::Cancelled;
    }
};
}
