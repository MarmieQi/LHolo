#pragma once

#include "structure/capture/CustomStructure.h"
#include <filesystem>

class StructureTemplate;

namespace lholo::structure::capture {

bool exportMcstructure(StructureTemplate const& structure, std::filesystem::path const& output);
bool exportCustomMcstructure(CustomStructure const& draft, std::filesystem::path const& output);

} // namespace lholo::structure::capture
