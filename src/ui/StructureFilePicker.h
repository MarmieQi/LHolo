#pragma once

#include "ui/FileDialog.h"

#include <mutex>
#include <optional>
#include <thread>

namespace lholo::ui {

enum class StructureFileTypes { Projection, Mcstructure };

// Owns one asynchronous system file picker.
class StructureFilePicker {
public:
    using Dialog = StructureFileDialogResult (*)(std::filesystem::path const&, std::stop_token);
    explicit StructureFilePicker(Dialog dialog = selectStructureFile, StructureFileTypes types = StructureFileTypes::Projection)
        : mDialog(dialog), mTypes(types) {}
    ~StructureFilePicker();
    bool start(std::filesystem::path current);
    bool busy() const;
    std::optional<StructureFileDialogResult> takeResult();
    void cancel();
    void shutdown();

private:
    Dialog mDialog;
    StructureFileTypes mTypes;
    mutable std::mutex mMutex;
    std::jthread mWorker;
    bool mBusy{};
    std::optional<StructureFileDialogResult> mResult;
};
}
