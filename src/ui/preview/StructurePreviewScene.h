#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include "ui/preview/StructurePreviewFrame.h"
namespace lholo::structure { struct LoadedStructure; }
namespace lholo::ui::preview {
bool installStructurePreviewScene();
void uninstallStructurePreviewScene();
bool requestStructurePreviewScene(std::uint64_t id, std::string name, PreviewFrame frame = {});
void completeStructurePreviewRead(std::uint64_t id, std::shared_ptr<structure::LoadedStructure const>, std::string error);
bool structurePreviewActive();
// An event only after leaving the stack; false aborts returning after context loss.
std::optional<bool> consumeStructurePreviewCompletion();
}
