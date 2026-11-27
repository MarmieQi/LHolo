#pragma once
#include <cstdint>

namespace lholo::structure {
enum class TransformField { Rotation, Mirror, X, Y, Z };
struct TransformValues {
    int rotation{}, mirror{}, x{}, y{}, z{};
    bool operator==(TransformValues const&) const = default;
};
struct TransformEdit {
    std::uint64_t id{}, context{};
    TransformField field{};
    int expected{}, value{};
};
struct TransformState {
    TransformValues values;
    std::uint64_t context{}, ack{};
    bool accepted{};
    bool operator==(TransformState const&) const = default;
};
inline bool validTransformEdit(TransformEdit const& edit) {
    if (!edit.id) return false;
    switch (edit.field) {
    case TransformField::Rotation: return edit.value >= 0 && edit.value <= 3 && edit.expected >= 0 && edit.expected <= 3;
    case TransformField::Mirror: return edit.value >= 0 && edit.value <= 2 && edit.expected >= 0 && edit.expected <= 2;
    case TransformField::X: case TransformField::Y: case TransformField::Z: return true;
    }
    return false;
}
}
