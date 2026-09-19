// LHolo - Language store built from embedded Windows resource JSON files
//
// Parses every locale JSON listed in xmake's generated resource registry once
// at startup into flat TextKey-indexed tables, then serves tr() lookups
// lock-free.
//
// Language values are runtime indices only. They must never be persisted:
// config.json stores the stable locale code (for example, "zh_CN") instead.
//
// Fallback chain: a missing or empty entry resolves through zh_CN (the default
// UI language, and the last resort) before returning "".
//
// Layering: leaf module, same rules as TextKeys.h. Must stay free of Minecraft
// and LeviLamina headers so the logic tests can link it standalone.

#pragma once

#include "i18n/TextKeys.h"

#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace lholo::i18n {

// Runtime index into languages(). This is intentionally not a persisted ID:
// adding a locale may change the order of the registry.
using Language = std::size_t;

inline constexpr Language kInvalidLanguage = std::numeric_limits<Language>::max();
inline constexpr std::string_view kDefaultLanguageCode = "zh_CN";

struct LanguageInfo {
    std::string code;
    std::string displayName;
};

// Per-language parse diagnostics, filled by initLanguageStore().
struct LanguageStats {
    std::size_t missing{};   // identifiers in TextKeys.h absent from the file
    std::size_t unknown{};   // identifiers in the file unknown to TextKeys.h
    std::size_t nonString{}; // present but not a JSON string
    std::size_t empty{};     // empty string values, excluding TextKey::None
    bool        parsed{};    // the document parsed as a JSON object at all
    bool        metadataValid{}; // _meta.displayName is a non-empty string
};

// Parses every embedded language resource and publishes the lookup tables.
// Called once from AppKernel::load() before any tr() use; calling it again
// replaces the tables (leaking the previous ones - startup-time only).
// Lookups before the first call return "" for every key, never crash.
void initLanguageStore();

// The registry is sorted by locale code and remains stable after initialization.
std::span<LanguageInfo const> languages() noexcept;

Language defaultLanguage() noexcept;
Language languageFromCode(std::string_view code) noexcept;
bool     isValidLanguage(Language language) noexcept;
std::string_view languageCode(Language language) noexcept;

// Diagnostics for the last initLanguageStore() call. Before the first call
// every field reports the "nothing parsed" state.
LanguageStats languageStats(Language language) noexcept;

// Resolves `key` in `language`, falling back to zh_CN, then "".
// Always returns a valid pointer (possibly to ""). noexcept: the hot path
// used by every rendered frame.
char const* lookupText(TextKey key, Language language) noexcept;

} // namespace lholo::i18n
