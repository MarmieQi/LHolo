#pragma once

#include <filesystem>
#include <optional>
#include <stop_token>

namespace lholo::ui {

struct StructureFileDialogResult {
    std::optional<std::filesystem::path> path;
    unsigned long errorCode{}; // zero with no path means Cancel
};

// Modern Explorer-style dialog, called on the asynchronous picker STA.
StructureFileDialogResult selectStructureFile(
    std::filesystem::path const& current, std::stop_token stop = {}
);

StructureFileDialogResult selectMcstructureOutput(
    std::filesystem::path const& current, std::stop_token stop = {}
);

} // namespace lholo::ui
