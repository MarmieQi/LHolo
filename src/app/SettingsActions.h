#pragma once
#include "input/HotkeyTypes.h"
#include <array>
#include <cstdint>
#include <string>
#include <variant>

namespace lholo::app {
enum class SettingsField {
    HudEnabled, HudPosition, HudFileName, HudLayer, HudOverallProgress, HudProgress,
    HudProjectedBlockName, HudWrongState, HudWrongType, HudExtraBlocks,
    MaterialHudEnabled, MaterialHudStackConversion, MaterialHudMaxItems, MaterialHudPosition, AltWheel, Radius, Cooldown, Mode, Language, WriteHudEnabled, Count
};
inline constexpr auto kSettingsFieldCount = static_cast<std::size_t>(SettingsField::Count);
using SettingsValue = std::variant<bool, int, std::string>;
enum class SettingsAction { Set, BeginCapture, ClearHotkey, ResetHotkey, ResetHotkeys, Consent };
struct SettingsIntent {
    std::uint64_t id{}, context{};
    SettingsAction action{SettingsAction::Set};
    SettingsField field{};
    SettingsValue expected{}, value{};
    std::size_t slot{};
    std::uint64_t hotkeysRevision{};
};
struct SettingsHotkey {
    unsigned key{}, modifiers{};
    bool capturing{};
    bool operator==(SettingsHotkey const&) const = default;
};
struct SettingsSnapshot {
    std::array<SettingsValue, kSettingsFieldCount> values;
    std::array<SettingsHotkey, input::kHotkeyCount> hotkeys;
    std::uint64_t hotkeysRevision{}, context{}, ack{};
    bool consent{}, accepted{};
};
bool validSettingsIntent(SettingsIntent const& intent);
bool submitSettingsIntent(SettingsIntent const& intent);
SettingsSnapshot getSettingsSnapshot();
bool processSettingsActions(); // true only for persistent preferences, not live placement modes
void requestHotkeyCaptureCancel();
void requestHudLayoutEdit();
bool consumeHudLayoutEditRequest();
void resetSettingsActions();
}
