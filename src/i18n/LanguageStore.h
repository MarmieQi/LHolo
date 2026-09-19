// LHolo - Language store built from the embedded JSON files
//
// Parses the embedded language JSON (see EmbeddedLanguages.h) once at startup
// into flat TextKey-indexed tables, then serves tr() lookups lock-free.
//
// Completeness: zh_CN and en_US must cover every key. This is enforced by the
// logic tests (testI18n / testLanguageStore), which call the same init path
// and fail on any missing, empty, or unknown entry - the runtime equivalent
// of the old compile-time table check.
//
// Fallback chain: a missing or empty entry resolves through SimplifiedChinese
// (the default UI language, and the last resort) before returning "", so a
// broken translation file can never show a raw key or crash.
//
// Layering: leaf module, same rules as TextKeys.h. Must stay free of Minecraft
// and LeviLamina headers so the logic tests can link it standalone.

#pragma once

#include "i18n/TextKeys.h"

#include <cstddef>

namespace lholo::i18n {

enum class Language : int {
    SimplifiedChinese = 0,
    English           = 1,
};

inline constexpr int kLanguageCount = 2;

constexpr int toInt(Language language) noexcept { return static_cast<int>(language); }

constexpr Language languageFromInt(int value) noexcept {
    return value == toInt(Language::English) ? Language::English : Language::SimplifiedChinese;
}

// Per-language parse diagnostics, filled by initLanguageStore().
struct LanguageStats {
    std::size_t missing{};   // identifiers in TextKeys.h absent from the file
    std::size_t unknown{};   // identifiers in the file unknown to TextKeys.h
    std::size_t nonString{}; // present but not a JSON string
    bool        parsed{};    // the document parsed as a JSON object at all
};

// Parses every embedded language file and publishes the lookup tables.
// Called once from AppKernel::load() before any tr() use; calling it again
// replaces the tables (leaking the previous ones - startup-time only).
// Lookups before the first call return "" for every key, never crash.
void initLanguageStore();

// Diagnostics for the last initLanguageStore() call. Before the first call
// every field reports the "nothing parsed" state.
LanguageStats languageStats(Language language) noexcept;

// Resolves `key` in `language`, falling back to SimplifiedChinese, then "".
// Always returns a valid pointer (possibly to ""). noexcept: the hot path
// used by every rendered frame.
char const* lookupText(TextKey key, Language language) noexcept;

} // namespace lholo::i18n
