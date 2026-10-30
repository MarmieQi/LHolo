#pragma once
#include "structure/write/StructureWriteTypes.h"
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace lholo::structure::write {
// Mailbox access never waits for vanilla placement. Only lifecycle operations
// acquire the execution gate, so stop/cancel also drain an in-flight write.
class LocalWriteQueue {
    struct Request { StructureWritePlan plan; std::uint64_t worldRevision; };
    mutable std::mutex mMutex;
    std::mutex mExecution;
    bool mEnabled{}, mExecuting{};
    std::optional<Request> mPending;
    std::optional<WriteCompletion> mCompleted;
public:
    void enable() {
        std::lock_guard execution(mExecution);
        std::lock_guard lock(mMutex);
        mEnabled = true;
    }
    bool enabled() const { std::lock_guard lock(mMutex); return mEnabled; }
    bool enqueue(StructureWritePlan plan, std::uint64_t revision) {
        std::lock_guard lock(mMutex);
        if (!mEnabled || mPending || mExecuting || mCompleted) return false;
        mPending = Request{std::move(plan), revision};
        return true;
    }
    void run(std::uint64_t revision, std::function<WriteResult(StructureWritePlan const&)> const& writer) {
        std::lock_guard execution(mExecution);
        std::optional<Request> request;
        {
            std::lock_guard lock(mMutex);
            if (!mEnabled || !mPending) return;
            request = std::exchange(mPending, {});
            mExecuting = true;
        }
        WriteCompletion result{WriteResult::ContextChanged, {}};
        if (request->worldRevision == revision) {
            try { result.result = writer(request->plan); }
            catch (std::exception const& error) { result = {WriteResult::Failed, error.what()}; }
            catch (...) { result = {WriteResult::Failed, "Unknown local world write error"}; }
        }
        std::lock_guard lock(mMutex);
        mExecuting = false;
        mCompleted = std::move(result);
    }
    std::optional<WriteCompletion> take() {
        std::lock_guard lock(mMutex);
        return std::exchange(mCompleted, {});
    }
    void cancel() {
        std::lock_guard execution(mExecution);
        std::lock_guard lock(mMutex);
        mPending.reset();
        mCompleted.reset();
    }
    void disable() {
        std::lock_guard execution(mExecution);
        std::lock_guard lock(mMutex);
        mEnabled = false;
        mPending.reset();
        mCompleted.reset();
    }
};
}
