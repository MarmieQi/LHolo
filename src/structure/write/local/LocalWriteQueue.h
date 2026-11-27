#pragma once
#include "structure/write/StructureWriteTypes.h"
#include "structure/write/local/LocalWriteSession.h"
#include <array>
#include <exception>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace lholo::structure::write {

struct LocalEnvironment {
    enum class State { Ready, WaitingDimension, ContextChanged, HeightLimit };
    State state{State::Ready};
    std::optional<std::array<int, 3>> playerFeet;
};

// Mailbox access never waits for vanilla placement. Only lifecycle operations
// acquire the execution gate, so stop/cancel also drain an in-flight write.
class LocalWriteQueue {
    mutable std::mutex mMutex;
    std::mutex mExecution;
    bool mEnabled{};
    std::optional<LocalWriteSession> mSession;
    std::uint64_t mWorldRevision{};
    std::optional<WriteCompletion> mCompleted;
    std::vector<LocalChunkCandidate> mScratch;

    bool activeFor(std::uint64_t revision) const {
        return mEnabled && mSession && mSession->status().active && mWorldRevision == revision;
    }

public:
    void enable() {
        std::lock_guard execution(mExecution);
        std::lock_guard lock(mMutex);
        mEnabled = true;
    }

    bool enabled() const {
        std::lock_guard lock(mMutex);
        return mEnabled;
    }

    bool start(StructureWritePlan plan, std::uint64_t revision) {
        LocalWriteSession created(std::move(plan));
        std::lock_guard lock(mMutex);
        if (!mEnabled || (mSession && mSession->status().active) || mCompleted) return false;
        mWorldRevision = revision;
        mSession.emplace(std::move(created));
        if (!mSession->status().active) {
            mCompleted = WriteCompletion{WriteResult::Complete, {}};
        }
        return true;
    }

    WriteProgress status() const {
        std::lock_guard lock(mMutex);
        return mSession ? mSession->status() : WriteProgress{};
    }

    void resume() {
        std::lock_guard lock(mMutex);
        if (mSession) mSession->resume();
    }

    template <class ProbeEnv, class CheckWritable, class PlaceChunk>
    void runTick(
        std::uint64_t revision,
        ProbeEnv&& probeEnv,
        CheckWritable&& isWritable,
        PlaceChunk&& placeChunk
    ) {
        std::lock_guard execution(mExecution);
        int dimension{};
        std::array<int, 2> yBounds{};
        std::uint64_t activeRevision{};
        {
            std::lock_guard lock(mMutex);
            if (!mEnabled || !mSession || !mSession->status().active) return;
            if (mWorldRevision != revision) {
                mSession->cancel();
                mCompleted = WriteCompletion{WriteResult::ContextChanged, {}};
                return;
            }
            dimension = mSession->dimension();
            yBounds = mSession->yBounds();
            activeRevision = mWorldRevision;
        }

        LocalEnvironment const env = probeEnv(dimension, yBounds);
        if (env.state == LocalEnvironment::State::ContextChanged || env.state == LocalEnvironment::State::HeightLimit) {
            std::lock_guard lock(mMutex);
            if (!activeFor(activeRevision)) return;
            mSession->cancel();
            mCompleted = WriteCompletion{
                env.state == LocalEnvironment::State::HeightLimit ? WriteResult::HeightLimit : WriteResult::ContextChanged,
                {}
            };
            return;
        }
        if (env.state == LocalEnvironment::State::WaitingDimension) {
            std::lock_guard lock(mMutex);
            if (activeFor(activeRevision)) mSession->setWaitingDimension();
            return;
        }

        {
            std::lock_guard lock(mMutex);
            if (!activeFor(activeRevision)) return;
            mSession->fillPendingOrder(env.playerFeet, mScratch);
        }

        for (auto const& candidate : mScratch) {
            if (!isWritable(candidate.cx, candidate.cz, candidate.worldMin, candidate.worldMax)) continue;

            std::optional<StructureWritePlan> subPlan;
            {
                std::lock_guard lock(mMutex);
                if (!activeFor(activeRevision)) return;
                subPlan = mSession->makeChunkPlan(candidate.index);
            }

            if (!subPlan) {
                std::lock_guard lock(mMutex);
                if (!activeFor(activeRevision)) return;
                mSession->completeChunk(candidate.index);
                if (!mSession->status().active) mCompleted = WriteCompletion{WriteResult::Complete, {}};
                continue;
            }

            WriteCompletion outcome{WriteResult::Complete, {}};
            try {
                outcome.result = placeChunk(*subPlan);
            } catch (std::exception const& error) {
                outcome = {WriteResult::Failed, error.what()};
            } catch (...) {
                outcome = {WriteResult::Failed, "Unknown local world write error"};
            }

            std::lock_guard lock(mMutex);
            if (!activeFor(activeRevision)) return;
            if (outcome.result == WriteResult::Complete) {
                mSession->completeChunk(candidate.index);
                if (!mSession->status().active) mCompleted = WriteCompletion{WriteResult::Complete, {}};
            } else {
                mSession->cancel();
                mCompleted = std::move(outcome);
            }
            return;
        }

        std::lock_guard lock(mMutex);
        if (activeFor(activeRevision)) mSession->setWaitingChunks();
    }

    std::optional<WriteCompletion> take() {
        std::lock_guard lock(mMutex);
        return std::exchange(mCompleted, {});
    }

    void cancel() {
        std::lock_guard execution(mExecution);
        std::lock_guard lock(mMutex);
        if (mSession) mSession->cancel();
        mCompleted.reset();
    }

    void disable() {
        std::lock_guard execution(mExecution);
        std::lock_guard lock(mMutex);
        mEnabled = false;
        mSession.reset();
        mCompleted.reset();
    }
};

} // namespace lholo::structure::write
