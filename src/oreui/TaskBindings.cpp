#include "oreui/TaskBindings.h"
#include "oreui/TaskProtocol.h"
#include "structure/StructurePaths.h"
#include "ui/StructureFilePicker.h"
#include "i18n/Translator.h"
#include "nlohmann/json.hpp"
#include <mutex>
namespace lholo::oreui {
namespace {
std::mutex mutex;
ui::StructureFilePicker picker;
std::optional<app::TaskIntent> browse;
std::optional<app::TaskStatus> published;
bool publishedBusy{};
} // namespace
bool queueTaskIntent(std::string_view payload) {
    auto intent = parseTaskIntent(payload);
    if (!intent)
        return false;
    std::lock_guard lock(mutex);
    if (intent->action == app::TaskAction::AddFile && intent->value.empty()) {
        auto snapshot = app::getTaskSnapshot();
        if (!snapshot.writable || snapshot.loading || snapshot.revision != intent->revision)
            return app::submitTaskIntent(std::move(*intent));
        if (browse)
            return false;
        auto* task = tasks::findTask(snapshot.catalog, intent->taskId);
        if (!task)
            return false;
        auto failed = [&] {
            intent->action = app::TaskAction::Acknowledge;
            intent->value = i18n::tr(i18n::TextKey::OreUiBrowseFailed);
            return app::submitTaskIntent(std::move(*intent));
        };
        try {
            if (!intent->fileId.empty()) {
                auto* file = tasks::findFile(*task, intent->fileId);
                if (!file)
                    return false;
                if (!picker.start(structure::detail::pathFromUtf8(file->path)))
                    return failed();
            } else if (!picker.start({}))
                return failed();
        } catch (std::exception const&) {
            return failed();
        }
        browse = std::move(intent);
        return true;
    }
    return app::submitTaskIntent(std::move(*intent));
}
std::optional<std::string> takeTaskUpdate(bool force) {
    std::lock_guard lock(mutex);
    if (auto selected = picker.takeResult(); selected && browse) {
        auto intent = std::move(*browse);
        browse.reset();
        if (selected->path) {
            intent.action = intent.fileId.empty() ? app::TaskAction::AddFile : app::TaskAction::Path;
            intent.value = structure::detail::pathToUtf8(*selected->path);
            app::submitTaskIntent(std::move(intent));
        } else {
            intent.action = app::TaskAction::Acknowledge;
            if (selected->errorCode)
                intent.value = i18n::tr(i18n::TextKey::OreUiBrowseFailed);
            app::submitTaskIntent(std::move(intent));
        }
    }
    auto status = app::getTaskStatus();
    auto busy = picker.busy();
    if (!force && published == status && publishedBusy == busy)
        return {};
    auto s = app::getTaskSnapshot();
    auto list = nlohmann::json::array();
    for (auto const& task : s.catalog.tasks) {
        auto files = nlohmann::json::array();
        for (auto const& file : task.files)
            files.push_back({{"id", file.id},
                             {"path", file.path},
                             {"name", structure::detail::pathToUtf8(
                                          structure::detail::pathFromUtf8(file.path).filename())},
                             {"hidden", !file.visible},
                             {"dimension", file.dimension ? nlohmann::json(*file.dimension) : nlohmann::json(nullptr)}});
        list.push_back({{"id", task.id},
                        {"name", task.name},
                        {"selectedFileId", task.selectedFileId},
                        {"layers", files}});
    }
    auto json = nlohmann::json{{"tasks", list},
                               {"currentTaskId", s.catalog.selectedTaskId},
                               {"activeTaskId", s.activeTaskId},
                               {"revision", s.revision},
                               {"ack", s.ack},
                               {"accepted", s.accepted},
                               {"loading", s.loading},
                               {"browsing", picker.busy()},
                               {"worldAvailable", s.worldAvailable},
                               {"writable", s.writable},
                               {"error", s.error},
                               {"errorFileId", s.errorFileId}}
                    .dump();
    published = static_cast<app::TaskStatus const&>(s);
    publishedBusy = busy;
    return json;
}
void resetTaskBindings() {
    std::lock_guard lock(mutex);
    picker.cancel();
    browse.reset();
    published.reset();
}
void shutdownTaskBindings() {
    resetTaskBindings();
    picker.shutdown();
}
} // namespace lholo::oreui
