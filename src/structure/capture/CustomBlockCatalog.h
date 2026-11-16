#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lholo::structure::capture {

struct CustomBlockEntry {
    std::string id;
    std::string name;
};

// A snapshot of registered default block states, independent of the world.
std::vector<CustomBlockEntry> customBlockCatalog(std::string_view localeCode);

} // namespace lholo::structure::capture
