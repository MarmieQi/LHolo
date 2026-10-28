#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
namespace lholo::structure {
struct LoadedStructure;
namespace detail {
struct StructureReadResult {
    std::uint64_t id{}, worldRevision{};
    std::shared_ptr<LoadedStructure const> loaded;
    std::string error;
};
// Called under the owning loader's mutex. Cancellation revokes publication;
// it never destroys a running async future on the UI thread.
class StructureReadQueue {
public:
    using Reader = std::function<std::shared_ptr<LoadedStructure const>(
        std::string const&,std::string&,std::atomic_bool const&)>;
    explicit StructureReadQueue(Reader);
    ~StructureReadQueue();
    bool start(std::uint64_t id,std::uint64_t revision,std::string path);
    void cancel(std::uint64_t id);
    void cancelAll();
    std::shared_ptr<StructureReadResult const> take();
    void drain(); // Only world destruction / DLL shutdown may wait.
private:
    struct Request {
        StructureReadResult result;
        std::string path;
        std::atomic_bool cancelled{};
    };
    void launch(std::shared_ptr<Request>);
    void advance();
    Reader mReader;
    std::future<void> mTask;
    std::shared_ptr<Request> mActive,mPending;
    std::shared_ptr<StructureReadResult> mCompleted;
};
}
}
