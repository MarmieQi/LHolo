// LHolo - Interface language selection and lookup
//
// The selected language is a plain persisted preference; there is no
// follow-the-game mode, so lookup never resolves a locale at call time. The
// language is stored as an integer in config.json and converted immediately at
// the settings boundary, matching the LayerAxis convention.
//
// Language, toInt/languageFromInt and the lookup tables live in
// LanguageStore.h; this header keeps the selection state and the tr() entry
// points the display boundary calls.
//
// Layering: leaf module. Only the display boundary (ui/, HUD rendering, action
// hints, file dialogs) may call tr(); logic modules pass TextKey values around
// instead of rendered text.

#pragma once

#include "i18n/LanguageStore.h"
#include "i18n/TextKeys.h"

namespace lholo::i18n {

// Current interface language. Reads are lock-free because the value is a single
// persisted preference; a switch applies to the next rendered frame.
Language language() noexcept;
void     setLanguage(Language language) noexcept;

char const* tr(TextKey key) noexcept;
char const* tr(TextKey key, Language language) noexcept;

// Language names are intentionally not translated: each is listed in its own
// language, the way in-game language pickers behave.
char const* languageName(Language language) noexcept;

} // namespace lholo::i18n
