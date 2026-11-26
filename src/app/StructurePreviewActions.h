#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include "structure/capture/CustomStructure.h"
#include "structure/capture/StructureCapture.h"
#include "ui/preview/StructurePreviewFrame.h"
namespace lholo::app {
enum class PreviewSourceKind { TaskFile, CaptureWorld, CaptureCustom };
struct StructurePreviewIntent {
    std::uint64_t id{}, revision{};
    PreviewSourceKind source{PreviewSourceKind::TaskFile};
    std::string taskId, fileId;
    structure::capture::Draft captureDraft;
    structure::capture::CustomStructure customStructure;
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
std::optional<structure::capture::CustomStructure> consumePreviewCustomDraft();
void processStructurePreviewActions();
void resetStructurePreviewActions();
}
