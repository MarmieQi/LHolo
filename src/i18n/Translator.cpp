// LHolo - Interface language selection and lookup

#include "i18n/Translator.h"

#include "i18n/LanguageStore.h"

#include <atomic>

namespace lholo::i18n {
namespace {

std::atomic<std::size_t> gLanguage{kInvalidLanguage};

} // namespace

Language language() noexcept {
    auto const value = gLanguage.load(std::memory_order_acquire);
    return isValidLanguage(value) ? value : defaultLanguage();
}

void setLanguage(Language value) noexcept {
    if (!isValidLanguage(value)) value = defaultLanguage();
    gLanguage.store(value, std::memory_order_release);
}

bool setLanguageByCode(std::string_view code) noexcept {
    auto const value = languageFromCode(code);
    if (value == kInvalidLanguage) {
        setLanguage(defaultLanguage());
        return false;
    }
    setLanguage(value);
    return true;
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
