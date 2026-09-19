// LHolo - Language store implementation

#include "i18n/LanguageStore.h"

#include "../../build/generated/i18n/LanguageRegistry.generated.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <string_view>
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
// they must outlive every lookup. The arrays are allocated (never freed) by
// initLanguageStore() and fully written before publication.
struct ParsedLanguage {
    std::vector<std::string> owned;
    std::array<char const*, kTextKeyCount> table{}; // pointers into `owned`
    LanguageStats stats{};
};

struct LanguageStoreState {
    std::vector<LanguageInfo> infos;
    std::vector<ParsedLanguage> parsed;
    Language                   fallback{kInvalidLanguage};
};

LanguageStoreState* gState = nullptr;

// Address used to locate the DLL containing these resources. Passing nullptr
// to FindResourceW would look in the Bedrock executable instead of LHolo.dll.
char const kResourceModuleAnchor = 0;

std::string_view embeddedLanguageJson(wchar_t const* resourceName) noexcept {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&kResourceModuleAnchor),
            &module
        )) {
        return {};
    }

    auto const resource = FindResourceW(module, resourceName, MAKEINTRESOURCEW(10));
    if (resource == nullptr) return {};

    auto const size = SizeofResource(module, resource);
    auto const data = LockResource(LoadResource(module, resource));
    if (data == nullptr || size == 0) return {};
    return {static_cast<char const*>(data), size};
}

void buildLanguage(
    LanguageInfo& info,
    ParsedLanguage& entry,
    std::string_view code,
    std::string_view json
) {
    info.code        = code;
    info.displayName = std::string{code}; // Safe UI fallback for broken metadata.
    entry.owned.reserve(kTextKeyCount + 16);
    entry.table.fill(nullptr);

    auto const document = nlohmann::json::parse(json, nullptr, false, true);
    if (document.is_discarded() || !document.is_object()) {
        entry.stats.missing = kTextKeyCount;
        entry.stats.parsed  = false;
        return;
    }
    entry.stats.parsed = true;

    auto const metadata = document.find("_meta");
    if (metadata != document.end() && metadata->is_object()) {
        auto const displayName = metadata->find("displayName");
        if (displayName != metadata->end()
            && displayName->is_string()
            && !displayName->get_ref<nlohmann::json::string_t const&>().empty()) {
            info.displayName = displayName->get_ref<nlohmann::json::string_t const&>();
        }
    }
    entry.stats.metadataValid = !info.displayName.empty() && info.displayName != info.code;

    std::vector<bool> seen(kTextKeyCount, false);
    for (auto const& [identifier, value] : document.items()) {
        if (identifier == "_meta") continue;

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
        auto const& text = value.get_ref<nlohmann::json::string_t const&>();
        if (found->second != TextKey::None && text.empty()) {
            ++entry.stats.empty;
        }
        // A JSON object cannot repeat a key, so `seen` only tracks coverage.
        entry.owned.emplace_back(text);
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
    auto* state = new LanguageStoreState;
    auto const count = generated::kLanguageResources.size();
    state->infos.resize(count);
    state->parsed.resize(count);

    for (std::size_t index = 0; index < count; ++index) {
        auto const& resource = generated::kLanguageResources[index];
        buildLanguage(
            state->infos[index],
            state->parsed[index],
            resource.code,
            embeddedLanguageJson(resource.resourceName)
        );
    }

    for (std::size_t index = 0; index < count; ++index) {
        if (state->infos[index].code == kDefaultLanguageCode) {
            state->fallback = index;
            break;
        }
    }

    // Published once, on the main thread, before any render or worker thread
    // can call lookupText(); plain publication is sufficient.
    gState = state;
}

std::span<LanguageInfo const> languages() noexcept {
    if (gState == nullptr) return {};
    return {gState->infos.data(), gState->infos.size()};
}

Language defaultLanguage() noexcept {
    return gState == nullptr ? kInvalidLanguage : gState->fallback;
}

Language languageFromCode(std::string_view code) noexcept {
    if (gState == nullptr) return kInvalidLanguage;
    for (std::size_t index = 0; index < gState->infos.size(); ++index) {
        if (gState->infos[index].code == code) return index;
    }
    return kInvalidLanguage;
}

bool isValidLanguage(Language language) noexcept {
    return gState != nullptr && language < gState->infos.size();
}

std::string_view languageCode(Language language) noexcept {
    if (!isValidLanguage(language)) return {};
    return gState->infos[language].code;
}

LanguageStats languageStats(Language language) noexcept {
    if (!isValidLanguage(language)) return {};
    return gState->parsed[language].stats;
}

char const* lookupText(TextKey key, Language language) noexcept {
    if (gState != nullptr) {
        if (isValidLanguage(language)) {
            if (char const* text = tableLookup(gState->parsed[language], key);
                text != nullptr && *text != '\0') {
                return text;
            }
        }
        auto const fallback = gState->fallback;
        if (fallback != kInvalidLanguage && fallback != language) {
            if (char const* text = tableLookup(gState->parsed[fallback], key);
                text != nullptr && *text != '\0') {
                return text;
            }
        }
    }
    return "";
}

} // namespace lholo::i18n
