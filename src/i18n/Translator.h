// LHolo - Interface language selection and lookup
//
// The persisted preference is a locale code or "auto". The client control
// frame supplies the game locale, and lookups use the resolved language index.
//
// Language, the dynamic registry and the lookup tables live in LanguageStore.h;
// this header keeps the selection state and the tr() entry points the display
// boundary calls.
//
// Layering: leaf module. Only the display boundary (ui/, HUD rendering, action
// hints, file dialogs) may call tr(); logic modules pass TextKey values around
// instead of rendered text.

#pragma once

#include "i18n/LanguageStore.h"
#include "i18n/TextKeys.h"

namespace lholo::i18n {

inline constexpr std::string_view kFollowGameLanguageCode = "auto";

Language language() noexcept;
void     setLanguage(Language language) noexcept;
bool     setLanguageByCode(std::string_view code) noexcept;
bool     followsGameLanguage() noexcept;
std::string_view languagePreferenceCode() noexcept;
void     updateGameLanguage(std::string_view code) noexcept;

char const* tr(TextKey key) noexcept;
char const* tr(TextKey key, Language language) noexcept;

// Language names are intentionally not translated: each is listed in its own
// language, the way in-game language pickers behave.
char const* languageName(Language language) noexcept;

} // namespace lholo::i18n
