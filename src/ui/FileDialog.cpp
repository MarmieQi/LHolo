#include "ui/FileDialog.h"

#include "i18n/Translator.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Windows.h>
#include <commdlg.h>

namespace lholo::ui {
namespace {

std::wstring utf8ToWide(std::string_view text) {
    if (text.empty()) return {};
    auto const size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0
    );
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size
    );
    return result;
}

// The common dialog expects "label\0pattern\0" pairs terminated by an extra
// null. Labels follow the interface language; patterns are language-neutral.
std::wstring buildFilter(std::initializer_list<std::pair<i18n::TextKey, char const*>> entries) {
    std::wstring filter;
    for (auto const& [label, pattern] : entries) {
        filter += utf8ToWide(i18n::tr(label));
        filter.push_back(L'\0');
        filter += utf8ToWide(pattern);
        filter.push_back(L'\0');
    }
    filter.push_back(L'\0');
    return filter;
}

} // namespace

std::optional<std::filesystem::path> openStructureFile(std::filesystem::path const& current) {
    std::vector<wchar_t> buffer(32768, L'\0');
    if (!current.empty()) {
        auto const value = current.native();
        std::copy_n(value.data(), std::min(value.size(), buffer.size() - 1), buffer.data());
    }

    auto const filter = buildFilter({
        {i18n::TextKey::DialogFilterProjection, "*.mcstructure;*.litematic"},
        {i18n::TextKey::DialogFilterBedrock, "*.mcstructure"},
        {i18n::TextKey::DialogFilterLitematica, "*.litematic"},
        {i18n::TextKey::DialogFilterAll, "*.*"}
    });
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrFilter = filter.c_str();
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    dialog.lpstrDefExt = L"mcstructure";
    if (!GetOpenFileNameW(&dialog)) return std::nullopt;
    return std::filesystem::path{buffer.data()};
}

std::optional<std::filesystem::path> saveMcstructureFile() {
    std::vector<wchar_t> buffer(32768, L'\0');
    constexpr wchar_t defaultName[] = L"structure.mcstructure";
    std::copy_n(defaultName, std::size(defaultName), buffer.data());

    auto const filter = buildFilter({
        {i18n::TextKey::DialogFilterBedrock, "*.mcstructure"},
        {i18n::TextKey::DialogFilterAll, "*.*"}
    });
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrFilter = filter.c_str();
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    dialog.lpstrDefExt = L"mcstructure";
    if (!GetSaveFileNameW(&dialog)) return std::nullopt;
    return std::filesystem::path{buffer.data()};
}

} // namespace lholo::ui
