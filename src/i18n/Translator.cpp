// LHolo - Interface language selection and lookup

#include "i18n/Translator.h"

#include "i18n/LanguageStore.h"

#include <atomic>

namespace lholo::i18n {
namespace {

std::atomic<Language> gSelectedLanguage{kInvalidLanguage};
std::atomic<Language> gGameLanguage{kInvalidLanguage};

} // namespace

Language language() noexcept {
    auto const selected = gSelectedLanguage.load(std::memory_order_acquire);
    if (selected != kInvalidLanguage) return selected;
    auto const game = gGameLanguage.load(std::memory_order_acquire);
    return game == kInvalidLanguage ? defaultLanguage() : game;
}

void setLanguage(Language value) noexcept {
    if (!isValidLanguage(value)) value = defaultLanguage();
    gSelectedLanguage.store(value, std::memory_order_release);
}

bool setLanguageByCode(std::string_view code) noexcept {
    if (code == kFollowGameLanguageCode) {
        gSelectedLanguage.store(kInvalidLanguage, std::memory_order_release);
        return true;
    }
    auto const value = languageFromCode(code);
    if (value == kInvalidLanguage) {
        setLanguage(defaultLanguage());
        return false;
    }
    setLanguage(value);
    return true;
}

bool followsGameLanguage() noexcept {
    return gSelectedLanguage.load(std::memory_order_acquire) == kInvalidLanguage;
}

std::string_view languagePreferenceCode() noexcept {
    auto const selected = gSelectedLanguage.load(std::memory_order_acquire);
    return selected == kInvalidLanguage ? kFollowGameLanguageCode : languageCode(selected);
}

void updateGameLanguage(std::string_view code) noexcept {
    gGameLanguage.store(languageFromCode(code), std::memory_order_release);
}

char const* tr(TextKey key) noexcept { return tr(key, language()); }

char const* tr(TextKey key, Language value) noexcept {
    return lookupText(key, value);
}

char const* languageName(Language value) noexcept {
    if (!isValidLanguage(value)) return "";
    auto const& info = languages()[value];
    return info.displayName.empty() ? info.code.c_str() : info.displayName.c_str();
}

} // namespace lholo::i18n
