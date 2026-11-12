#pragma once

#include <array>
#include <filesystem>

class StructureTemplate;

namespace lholo::structure::capture {

bool exportMcstructure(StructureTemplate const& structure, std::filesystem::path const& output);
bool exportEmptyMcstructure(std::array<int, 3> const& size, std::filesystem::path const& output);

} // namespace lholo::structure::capture
