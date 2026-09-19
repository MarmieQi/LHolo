// LHolo - Interface language selection and lookup

#include "i18n/Translator.h"

#include "i18n/LanguageStore.h"

#include <atomic>

namespace lholo::i18n {
namespace {

std::atomic_int gLanguage{toInt(Language::SimplifiedChinese)};

} // namespace

Language language() noexcept {
    return languageFromInt(gLanguage.load(std::memory_order_acquire));
}

void setLanguage(Language value) noexcept {
    gLanguage.store(toInt(value), std::memory_order_release);
}

char const* tr(TextKey key) noexcept { return tr(key, language()); }

char const* tr(TextKey key, Language value) noexcept {
    return lookupText(key, value);
}

char const* languageName(Language value) noexcept {
    return value == Language::English ? "English" : "简体中文";
}

} // namespace lholo::i18n
