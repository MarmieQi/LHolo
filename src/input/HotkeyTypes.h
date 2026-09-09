// LHolo - Shared hotkey slot definitions
#pragma once

#include <cstddef>
#include <cstdint>

namespace lholo::input {

// This order is the persistent/runtime slot order. Every hotkey array and
// lookup derives its size or index from this enum so adding a slot cannot
// silently desynchronize input handling, settings, and menu presentation.
enum class HotkeyId : std::uint8_t {
    Gui,
    ProjectionOffset,
    MoveXMinus,
    MoveXPlus,
    MoveZMinus,
    MoveZPlus,
    MoveYPlus,
    MoveYMinus,
    LayerIncrease,
    LayerDecrease,
    LoadProjection,
    CloseProjection,
    Count
};

[[nodiscard]] constexpr std::size_t hotkeyIndex(HotkeyId id) noexcept {
    return static_cast<std::size_t>(id);
}

inline constexpr std::size_t kHotkeyCount = hotkeyIndex(HotkeyId::Count);
inline constexpr std::size_t kMoveHotkeyFirst = hotkeyIndex(HotkeyId::MoveXMinus);
inline constexpr std::size_t kMoveHotkeyCount
    = hotkeyIndex(HotkeyId::MoveYMinus) - kMoveHotkeyFirst + 1;

} // namespace lholo::input
