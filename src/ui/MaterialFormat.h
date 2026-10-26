#pragma once

#include "i18n/Translator.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lholo::ui {
// Shared by menu and native HUD; no dependency on either rendering backend.
// Counts use each item's stack limit: 111 -> "111 (1 x 64 + 47)".
std::string formatStackCount(std::uint64_t count, int stackSize);
// Liquids use the interface language key; other names come from the game.
char const* materialDisplayName(std::string const& displayName, std::optional<i18n::TextKey> const& nameKey);
// Display identity only: never changes inventory availability or placement.
// An aux value >= 0 selects one variant of the item ("minecraft:banner:15").
std::string materialIconId(std::string const& itemId, std::string const& typeName, int aux = -1);
}
