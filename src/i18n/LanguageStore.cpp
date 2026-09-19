// LHolo - Language store implementation

#include "i18n/LanguageStore.h"

#include "i18n/EmbeddedLanguages.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace lholo::i18n {
namespace {

// Identifier -> TextKey, built once from the X-macro list.
std::unordered_map<std::string, TextKey> const& idToKey() {
    static std::unordered_map<std::string, TextKey> const map = [] {
        std::unordered_map<std::string, TextKey> result;
        result.reserve(kTextKeyCount * 2);
        for (std::size_t index = 0; index < kTextKeyCount; ++index) {
            result.emplace(std::string{kTextKeyIds[index]}, static_cast<TextKey>(index));
        }
        return result;
    }();
    return map;
}

// Storage for parsed strings. Published tables point into these strings, so
// they must outlive every lookup. The array is allocated (never freed) by
// initLanguageStore() and fully written before publication.
struct ParsedLanguage {
    std::vector<std::string> owned;
    std::array<char const*, kTextKeyCount> table{};  // pointers into `owned`
    LanguageStats             stats{};
};

ParsedLanguage* gParsedLanguages = nullptr;

// SimplifiedChinese is the default UI language and doubles as the fallback.
constexpr Language kFallbackLanguage = Language::SimplifiedChinese;

void buildLanguage(ParsedLanguage& entry, std::string_view json) {
    entry.owned.reserve(kTextKeyCount + 16);
    entry.table.fill(nullptr);

    auto const document = nlohmann::json::parse(json, nullptr, false, true);
    if (document.is_discarded() || !document.is_object()) {
        entry.stats.missing = kTextKeyCount;
        entry.stats.parsed  = false;
        return;
    }
    entry.stats.parsed = true;

    std::vector<bool> seen(kTextKeyCount, false);
    for (auto const& [identifier, value] : document.items()) {
        auto const found = idToKey().find(identifier);
        if (found == idToKey().end()) {
            ++entry.stats.unknown;
            continue;
        }
        if (!value.is_string()) {
            ++entry.stats.nonString;
            continue;
        }
        auto const index = static_cast<std::size_t>(found->second);
        // A JSON object cannot repeat a key, so `seen` only tracks coverage.
        entry.owned.emplace_back(value.get_ref<nlohmann::json::string_t const&>());
        entry.table[index] = entry.owned.back().c_str();
        seen[index]        = true;
    }
    for (std::size_t index = 0; index < kTextKeyCount; ++index) {
        if (!seen[index]) ++entry.stats.missing;
    }
}

char const* tableLookup(ParsedLanguage const& entry, TextKey key) noexcept {
    auto const index = static_cast<std::size_t>(key);
    if (index >= kTextKeyCount) return nullptr;
    return entry.table[index];
}

} // namespace

void initLanguageStore() {
    auto* parsed = new ParsedLanguage[static_cast<std::size_t>(kLanguageCount)];

    buildLanguage(
        parsed[static_cast<std::size_t>(toInt(Language::SimplifiedChinese))],
        kEmbeddedSimplifiedChineseLanguageJson
    );
    buildLanguage(
        parsed[static_cast<std::size_t>(toInt(Language::English))],
        kEmbeddedEnglishLanguageJson
    );

    // Published once, on the main thread, before any render or worker thread
    // can call lookupText(); plain publication is sufficient.
    gParsedLanguages = parsed;
}

LanguageStats languageStats(Language language) noexcept {
    if (gParsedLanguages == nullptr) return {};
    auto const index = toInt(language);
    if (index < 0 || index >= kLanguageCount) return {};
    return gParsedLanguages[static_cast<std::size_t>(index)].stats;
}

char const* lookupText(TextKey key, Language language) noexcept {
    if (gParsedLanguages != nullptr) {
        auto const languageIndex = static_cast<std::size_t>(toInt(language));
        if (languageIndex < static_cast<std::size_t>(kLanguageCount)) {
            if (char const* text = tableLookup(gParsedLanguages[languageIndex], key);
                text != nullptr && *text != '\0') {
                return text;
            }
        }
        auto const fallbackIndex = static_cast<std::size_t>(toInt(kFallbackLanguage));
        if (fallbackIndex != languageIndex) {
            if (char const* text = tableLookup(gParsedLanguages[fallbackIndex], key);
                text != nullptr && *text != '\0') {
                return text;
            }
        }
    }
    return "";
}

} // namespace lholo::i18n
