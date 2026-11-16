#pragma once

#include <algorithm>
#include <array>
#include <map>
#include <span>
#include <string>

namespace lholo::structure::capture {

struct CustomStructure {
    std::array<int, 3> size{};
    std::map<int, std::string> layers;
};

// Bedrock's dense order is Z, then Y, then X.
inline void fillCustomLayer(std::span<int> indices, std::array<int, 3> const& size, int y, int paletteIndex) {
    auto const stride = static_cast<std::size_t>(size[1]) * size[2];
    auto const row = static_cast<std::size_t>(y) * size[2];
    for (int x = 0; x < size[0]; ++x)
        std::fill_n(indices.begin() + x * stride + row, size[2], paletteIndex);
}

} // namespace lholo::structure::capture
