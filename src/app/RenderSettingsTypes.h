#pragma once
#include "structure/LayerSettings.h"
#include <variant>
#include <vector>

namespace lholo::app {
enum class RenderField { Bounds, Axis, Mode, Layer, Fill, Outline, CorrectionSeeThrough, MissingSeeThrough };
using RenderValue = std::variant<bool, int>;
struct RenderValues {
    bool bounds{};
    structure::LayerSettings layers;
    int fill{}, outline{}; // UI percentages; projection retains its float API
    bool correctionSeeThrough{}, missingSeeThrough{};
    bool operator==(RenderValues const&) const = default;
};
struct RenderFieldEdit { RenderField field; RenderValue expected, value; };
struct RenderSettingsEdit {
    std::uint64_t id{}, context{};
    structure::LayerAxis axis{structure::LayerAxis::Y};
    std::vector<RenderFieldEdit> changes;
};
struct RenderSettingsState {
    RenderValues values;
    std::array<int, 3> maximum{};
    std::uint64_t context{}, ack{};
    bool accepted{};
    bool operator==(RenderSettingsState const&) const = default;
};
RenderValue renderValue(RenderValues const& values, RenderField field);
bool validRenderEdit(RenderSettingsEdit const& edit);
}
