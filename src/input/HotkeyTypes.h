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
    // Held (not pressed) while scrolling to move the projection along the axis
    // the player faces. Default is Alt. Polled via scrollModifierHeld(), never
    // dispatched as a press action, so a key of 0 simply means "unbound".
    ScrollMove,
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
