#pragma once
#include <cstdint>
#include <string>
#include "ui/preview/StructurePreviewFrame.h"
namespace lholo::app {
struct StructurePreviewIntent {
    std::uint64_t id{}, revision{};
    std::string taskId, fileId;
    ui::preview::PreviewFrame frame;
};
struct StructurePreviewStatus {
    std::uint64_t ack{};
    bool accepted{};
    std::string error;
    bool operator==(StructurePreviewStatus const&) const = default;
};
bool submitStructurePreview(StructurePreviewIntent);
StructurePreviewStatus structurePreviewStatus();
void processStructurePreviewActions();
void resetStructurePreviewActions();
}
