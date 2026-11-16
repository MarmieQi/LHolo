#pragma once

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <vector>

namespace lholo::structure::capture {

struct CustomLayerFill {
    int y;
    int endY;
    std::string block;

    bool operator==(CustomLayerFill const&) const = default;
};

struct CustomStructure {
    std::array<int, 3> size{};
    std::vector<CustomLayerFill> layers;
};

// Bedrock's dense order is Z, then Y, then X.
inline void fillCustomLayers(
    std::span<int> indices, std::array<int, 3> const& size, CustomLayerFill const& layer, int paletteIndex
) {
    auto const stride = static_cast<std::size_t>(size[1]) * size[2];
    auto const row = static_cast<std::size_t>(layer.y) * size[2];
    auto const count = static_cast<std::size_t>(layer.endY - layer.y + 1) * size[2];
    for (int x = 0; x < size[0]; ++x)
        std::fill_n(indices.begin() + x * stride + row, count, paletteIndex);
}

} // namespace lholo::structure::capture
