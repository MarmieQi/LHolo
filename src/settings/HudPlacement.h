#pragma once
#include <array>
#include <cstddef>

namespace lholo::settings {
struct HudPlacement {
    bool custom{};
    float x{}, y{}; // Top-left position, normalized to the native viewport.
    bool operator==(HudPlacement const&) const = default;
};
enum class HudPanelId : std::size_t { Projection, Materials, Write, Count };
inline constexpr auto kHudPanelCount = static_cast<std::size_t>(HudPanelId::Count);
inline constexpr int kTopCenterHudPosition = 4;
using HudPlacements = std::array<HudPlacement, kHudPanelCount>;
using HudPercentages = std::array<int, kHudPanelCount>;
inline constexpr HudPercentages kDefaultHudOpacities{60, 60, 60};
inline constexpr HudPercentages kDefaultHudScales{100, 100, 100};
}
