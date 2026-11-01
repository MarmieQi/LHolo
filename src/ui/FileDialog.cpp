#include "ui/FileDialog.h"

#include "i18n/Translator.h"

#include <iterator>
#include <memory>
#include <array>
#include <string>
#include <string_view>

#include <Windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace lholo::ui {
namespace {

struct DialogApartment {
    HRESULT result{CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)};
    ~DialogApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};

// This worker-owned timer is not a file-dialog hook or customization.
// Its callback runs in Show's modal loop on the same STA as IFileDialog.
class DialogCancellation {
public:
    DialogCancellation(IFileDialog* dialog, std::stop_token stop) : mDialog(dialog), mStop(stop) {
        if (!stop.stop_possible()) return;
        mWindow = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
        if (!mWindow) { mError = GetLastError(); return; }
        SetWindowLongPtrW(mWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        if (!SetTimer(mWindow, 1, 100, onTimer)) {
            mError = GetLastError();
            if (!mError) mError = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    ~DialogCancellation() {
        if (mWindow) { KillTimer(mWindow, 1); DestroyWindow(mWindow); }
    }
    bool ready() const { return !mStop.stop_possible() || (mWindow && !mError); }
    unsigned long error() const { return mError ? mError : ERROR_NOT_ENOUGH_MEMORY; }
private:
    static void CALLBACK onTimer(HWND window, UINT, UINT_PTR, DWORD) {
        auto* self = reinterpret_cast<DialogCancellation*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self && self->mStop.stop_requested()) {
            self->mDialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
        }
    }
    IFileDialog* mDialog;
    std::stop_token mStop;
    HWND mWindow{};
    unsigned long mError{};
};

HWND dialogOwner() {
    auto window = GetForegroundWindow();
    DWORD process{};
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId()) return GetAncestor(window, GA_ROOT);
    // If focus changed before the worker started, find this process's main
    // visible window instead of attaching the picker to an unrelated app.
    HWND owner{};
    EnumWindows([](HWND candidate, LPARAM data) -> BOOL {
        DWORD candidateProcess{};
        GetWindowThreadProcessId(candidate, &candidateProcess);
        if (candidateProcess == GetCurrentProcessId() && IsWindowVisible(candidate)
            && !GetWindow(candidate, GW_OWNER)) {
            *reinterpret_cast<HWND*>(data) = candidate;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&owner));
    return owner;
}

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

} // namespace

static StructureFileDialogResult selectFile(std::filesystem::path const& current, std::stop_token stop, bool save) {
    if (stop.stop_requested()) return {};
    DialogApartment apartment;
    if (FAILED(apartment.result)) return {std::nullopt, static_cast<unsigned long>(apartment.result)};

    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    auto result = CoCreateInstance(save ? __uuidof(FileSaveDialog) : __uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(dialog.GetAddressOf()));
    if (FAILED(result)) return {std::nullopt, static_cast<unsigned long>(result)};

    FILEOPENDIALOGOPTIONS options{};
    result = dialog->GetOptions(&options);
    if (SUCCEEDED(result)) result = dialog->SetOptions(options | FOS_FORCEFILESYSTEM
        | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
    if (FAILED(result)) return {std::nullopt, static_cast<unsigned long>(result)};

    std::array<std::wstring, 4> const names{
        utf8ToWide(i18n::tr(i18n::TextKey::DialogFilterProjection)),
        utf8ToWide(i18n::tr(i18n::TextKey::DialogFilterBedrock)),
        utf8ToWide(i18n::tr(i18n::TextKey::DialogFilterLitematica)),
        utf8ToWide(i18n::tr(i18n::TextKey::DialogFilterAll))
    };
    COMDLG_FILTERSPEC const filters[]{
        {names[0].c_str(), L"*.mcstructure;*.litematic"},
        {names[1].c_str(), L"*.mcstructure"},
        {names[2].c_str(), L"*.litematic"},
        {names[3].c_str(), L"*.*"}
    };
    COMDLG_FILTERSPEC const saveFilter{names[1].c_str(), L"*.mcstructure"};
    result = dialog->SetFileTypes(save ? 1 : static_cast<UINT>(std::size(filters)), save ? &saveFilter : filters);
    if (SUCCEEDED(result)) result = dialog->SetFileTypeIndex(1);
    if (SUCCEEDED(result)) result = dialog->SetDefaultExtension(L"mcstructure");
    if (FAILED(result)) return {std::nullopt, static_cast<unsigned long>(result)};

    if (!current.empty()) {
        Microsoft::WRL::ComPtr<IShellItem> folder;
        auto const parent = current.parent_path();
        if (!parent.empty() && SUCCEEDED(SHCreateItemFromParsingName(parent.c_str(), nullptr,
                IID_PPV_ARGS(folder.GetAddressOf())))) {
            dialog->SetFolder(folder.Get());
        }
        dialog->SetFileName(current.filename().c_str());
    }
    if (save && current.empty()) dialog->SetFileName(L"structure.mcstructure");
    DialogCancellation cancellation{dialog.Get(), stop};
    if (!cancellation.ready()) return {std::nullopt, cancellation.error()};
    if (stop.stop_requested()) return {};
    result = dialog->Show(dialogOwner());
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED) || stop.stop_requested()) return {};
    if (FAILED(result)) return {std::nullopt, static_cast<unsigned long>(result)};

    Microsoft::WRL::ComPtr<IShellItem> selected;
    result = dialog->GetResult(selected.GetAddressOf());
    if (FAILED(result)) return {std::nullopt, static_cast<unsigned long>(result)};
    PWSTR rawPath{};
    result = selected->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> path{rawPath, CoTaskMemFree};
    if (FAILED(result)) return {std::nullopt, static_cast<unsigned long>(result)};
    return {std::filesystem::path{path.get()}, 0};
}

StructureFileDialogResult selectStructureFile(std::filesystem::path const& current, std::stop_token stop) {
    return selectFile(current, stop, false);
}

StructureFileDialogResult selectMcstructureOutput(std::filesystem::path const& current, std::stop_token stop) {
    return selectFile(current, stop, true);
}

} // namespace lholo::ui
