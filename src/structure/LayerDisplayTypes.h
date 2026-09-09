// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Shared, type-safe layer grouping and visibility modes. Persistent settings
// and Dear ImGui keep their stable integer representation only at the edges.

#pragma once

#include <algorithm>

namespace lholo::structure {

enum class LayerAxis : int {
    Y        = 0,
    X        = 1,
    Material = 2,
};

enum class LayerDisplayMode : int {
    All           = 0,
    Single        = 1,
    UpToCurrent   = 2,
    FromCurrent   = 3,
};

[[nodiscard]] constexpr int toInt(LayerAxis value) {
    return static_cast<int>(value);
}

[[nodiscard]] constexpr int toInt(LayerDisplayMode value) {
    return static_cast<int>(value);
}

[[nodiscard]] constexpr LayerAxis layerAxisFromInt(int value) {
    return static_cast<LayerAxis>(std::clamp(value, 0, 2));
}

[[nodiscard]] constexpr LayerDisplayMode layerDisplayModeFromInt(int value) {
    return static_cast<LayerDisplayMode>(std::clamp(value, 0, 3));
}

} // namespace lholo::structure
