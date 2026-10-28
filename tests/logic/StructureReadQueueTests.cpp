#include "structure/StructureReadQueue.h"
#include "structure/StructureLoader.h"
#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>
int runStructureReadQueueTests() {
    using namespace lholo::structure;
    using namespace std::chrono_literals;
    int failures{};
    auto check=[&](bool ok,char const* message) {
        if (!ok) { ++failures; std::printf("StructureReadQueueTests FAILED: %s\n",message); }
    };
    std::promise<void> entered,release;
    auto gate=release.get_future().share();
    detail::StructureReadQueue queue{[&](std::string const& path,std::string&,std::atomic_bool const&) {
        if (path=="old") { entered.set_value(); gate.wait(); }
        auto result=std::make_shared<LoadedStructure>(); result->sourcePath=path;
        return result;
    }};
    check(queue.start(1,7,"old"),"first read starts");
    auto entry=entered.get_future();
    bool const started=entry.wait_for(2s)==std::future_status::ready;
    check(started,"blocked reader started");
    if (!started) { release.set_value(); return failures; }
    check(!queue.start(99,7,"duplicate"),"live preview owns only one request");
    queue.cancel(1);
    check(queue.start(2,8,"new"),"reopen is accepted while cancelled parser still runs");
    check(!queue.take(),"cancelled in-flight reader cannot publish or block control-frame polling");
    release.set_value();
    std::shared_ptr<detail::StructureReadResult const> result;
    auto const deadline=std::chrono::steady_clock::now()+2s;
    while (!result && std::chrono::steady_clock::now()<deadline) { result=queue.take(); std::this_thread::yield(); }
    check(result && result->id==2 && result->worldRevision==8 && result->loaded
        && result->loaded->sourcePath=="new","late old result is discarded; only reopened preview publishes");
    check(!queue.take(),"result is delivered once");

    std::promise<void> drainEntered,drainRelease;
    auto drainGate=drainRelease.get_future().share();
    std::atomic_int reads{};
    detail::StructureReadQueue closing{[&](std::string const&,std::string&,std::atomic_bool const&) {
        ++reads; drainEntered.set_value(); drainGate.wait();
        return std::make_shared<LoadedStructure>();
    }};
    check(closing.start(3,9,"world-old"),"world read starts");
    auto startedDrain=drainEntered.get_future();
    bool const draining=startedDrain.wait_for(2s)==std::future_status::ready;
    check(draining,"world reader entered");
    if (!draining) { drainRelease.set_value(); return failures; }
    closing.cancel(3);
    check(closing.start(4,9,"queued"),"replacement queues behind old parser");
    closing.cancelAll();
    drainRelease.set_value(); closing.drain();
    check(reads==1 && !closing.take(),"world shutdown joins active parser and discards queued/late results");
    detail::StructureReadQueue retry{[](std::string const& path,std::string&,std::atomic_bool const&) {
        if (path=="bad") throw std::runtime_error("broken file");
        auto loaded=std::make_shared<LoadedStructure>(); loaded->sourcePath=path; return loaded;
    }};
    auto receive=[&] {
        std::shared_ptr<detail::StructureReadResult const> next;
        auto const limit=std::chrono::steady_clock::now()+2s;
        while (!next && std::chrono::steady_clock::now()<limit) { next=retry.take(); std::this_thread::yield(); }
        return next;
    };
    check(retry.start(5,10,"bad"),"failed read can start");
    auto failed=receive();
    check(failed && failed->id==5 && !failed->loaded && failed->error=="broken file",
        "parser exception is an owned result rather than an escaped future exception");
    check(retry.start(6,11,"repaired"),"failure does not strand the read channel");
    auto repaired=receive();
    check(repaired && repaired->id==6 && repaired->error.empty() && repaired->loaded
        && repaired->loaded->sourcePath=="repaired","retry cannot inherit old error or file state");
    std::printf("StructureReadQueueTests: %d failures\n",failures);
    return failures;
}
