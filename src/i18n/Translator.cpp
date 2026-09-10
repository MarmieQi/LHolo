// LHolo - Interface language selection and lookup

#include "i18n/Translator.h"

#include "i18n/TextTable.h"

#include <atomic>

namespace lholo::i18n {
namespace {

std::atomic_int gLanguage{toInt(Language::SimplifiedChinese)};

TextTable const& tableFor(Language language) noexcept {
    return language == Language::English ? kEnglishTable : kSimplifiedChineseTable;
}

} // namespace

Language language() noexcept {
    return languageFromInt(gLanguage.load(std::memory_order_acquire));
}

void setLanguage(Language value) noexcept {
    gLanguage.store(toInt(value), std::memory_order_release);
}

char const* tr(TextKey key) noexcept { return tr(key, language()); }

char const* tr(TextKey key, Language value) noexcept {
    auto const index = static_cast<std::size_t>(key);
    if (index >= kTextKeyCount) return "";
    auto const* text = tableFor(value)[index];
    return text != nullptr ? text : "";
}

char const* languageName(Language value) noexcept {
    return value == Language::English ? "English" : "简体中文";
}

} // namespace lholo::i18n
