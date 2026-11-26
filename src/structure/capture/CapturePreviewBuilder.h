// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// In-memory LoadedStructure builders for previewing world selections and custom
// structures through the shared native StructurePreviewScene.

#pragma once

#include "structure/capture/CustomStructure.h"
#include "structure/capture/StructureCapture.h"

#include <memory>
#include <string>

namespace lholo::structure {
struct LoadedStructure;
}

namespace lholo::structure::capture {

struct PreviewBuildResult {
    std::shared_ptr<LoadedStructure const> loaded;
    std::string                            error;
};

[[nodiscard]] PreviewBuildResult buildWorldSelectionPreview(Draft const& draft);
[[nodiscard]] PreviewBuildResult buildCustomStructurePreview(CustomStructure const& draft);

} // namespace lholo::structure::capture
