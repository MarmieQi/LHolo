#include "structure/StructureReadQueue.h"
#include <chrono>
#include <exception>
#include <utility>
namespace lholo::structure::detail {
StructureReadQueue::StructureReadQueue(Reader reader) : mReader(std::move(reader)) {}
StructureReadQueue::~StructureReadQueue() { drain(); }
void StructureReadQueue::launch(std::shared_ptr<Request> request) {
    mActive = std::move(request);
    try {
        mTask = std::async(std::launch::async,[request=mActive,reader=mReader] {
            if (request->cancelled.load()) return;
            try { request->result.loaded = reader(request->path,request->result.error,request->cancelled); }
            catch (std::exception const& e) { request->result.error = e.what(); }
            catch (...) { request->result.error = "Unknown structure read error"; }
        });
    } catch (std::exception const& e) {
        mActive->result.error = e.what();
        mCompleted = std::make_shared<StructureReadResult>(std::move(mActive->result));
        mActive.reset();
    }
}
void StructureReadQueue::advance() {
    if (mTask.valid() && mTask.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
        mTask.get();
        if (!mActive->cancelled.load())
            mCompleted = std::make_shared<StructureReadResult>(std::move(mActive->result));
        mActive.reset();
    }
    if (!mActive && mPending) launch(std::exchange(mPending,{}));
}
bool StructureReadQueue::start(std::uint64_t id,std::uint64_t revision,std::string path) {
    advance();
    if (!id || mPending || mCompleted || (mActive && !mActive->cancelled.load())) return false;
    auto request = std::make_shared<Request>();
    request->result.id=id; request->result.worldRevision=revision; request->path=std::move(path);
    if (mActive) mPending=std::move(request);
    else launch(std::move(request));
    return true;
}
void StructureReadQueue::cancel(std::uint64_t id) {
    if (mActive && mActive->result.id == id) mActive->cancelled.store(true);
    if (mPending && mPending->result.id == id) mPending.reset();
    if (mCompleted && mCompleted->id == id) mCompleted.reset();
}
void StructureReadQueue::cancelAll() {
    if (mActive) mActive->cancelled.store(true);
    mPending.reset(); mCompleted.reset();
}
std::shared_ptr<StructureReadResult const> StructureReadQueue::take() {
    advance();
    return std::exchange(mCompleted,{});
}
void StructureReadQueue::drain() {
    cancelAll();
    if (mTask.valid()) mTask.get();
    mActive.reset();
}
}
