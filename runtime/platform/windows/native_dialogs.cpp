/// UzEngine Native Dialogs Implementation (Windows)
///
/// File dialogs via IFileOpenDialog / IFileSaveDialog COM interfaces.
/// Message box via Win32 MessageBoxW.

#include "native_dialogs.h"

#ifdef _WIN32

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>

UZ_DEFINE_LOG_CATEGORY(NativeDialog, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// UTF-8 / Wide-char helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a UTF-8 string to a wide string. Returns empty on failure.
static std::wstring Utf8ToWide(const char* utf8) {
    if (!utf8 || utf8[0] == '\0') return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, result.data(), len);
    return result;
}

/// Converts a wide string to UTF-8. Returns empty on failure.
static std::string WideToUtf8(const wchar_t* wide) {
    if (!wide || wide[0] == L'\0') return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), len, nullptr, nullptr);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Filter conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Holds converted wide-string filter data whose lifetime must outlast the
/// COMDLG_FILTERSPEC array passed to the dialog.
struct WideFilters {
    std::vector<std::wstring> descriptions;
    std::vector<std::wstring> patterns;
    std::vector<COMDLG_FILTERSPEC> specs;
};

/// Converts DialogFilter array to COMDLG_FILTERSPEC array.
/// The returned WideFilters must stay alive while specs are in use.
static WideFilters BuildFilterSpecs(const DialogFilter* filters, int count) {
    WideFilters wf;
    if (!filters || count <= 0) return wf;
    wf.descriptions.reserve(static_cast<size_t>(count));
    wf.patterns.reserve(static_cast<size_t>(count));
    wf.specs.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        wf.descriptions.push_back(Utf8ToWide(filters[i].description));
        wf.patterns.push_back(Utf8ToWide(filters[i].extensions));
        wf.specs.push_back({ wf.descriptions.back().c_str(), wf.patterns.back().c_str() });
    }
    return wf;
}

/// Sets the initial directory on a file dialog via IShellItem.
static void SetDefaultDirectory(IFileDialog* dialog, const char* dir) {
    if (!dir) return;
    const std::wstring wideDir = Utf8ToWide(dir);
    if (wideDir.empty()) return;

    IShellItem* folder = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(wideDir.c_str(), nullptr, IID_PPV_ARGS(&folder));
    if (SUCCEEDED(hr) && folder) {
        dialog->SetFolder(folder);
        folder->Release();
    }
}

/// Extracts the file path string from an IShellItem.
static std::string GetPathFromShellItem(IShellItem* item) {
    PWSTR filePath = nullptr;
    HRESULT hr = item->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
    if (FAILED(hr) || !filePath) return {};
    std::string result = WideToUtf8(filePath);
    CoTaskMemFree(filePath);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShowOpenFileDialog
// ─────────────────────────────────────────────────────────────────────────────

FileDialogResult ShowOpenFileDialog(
    const DialogFilter* filters, int filterCount,
    bool multiSelect,
    const char* defaultDir,
    void* parentHwnd)
{
    FileDialogResult result{};

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) {
        UZ_LOG(NativeDialog, Error, "Failed to create IFileOpenDialog: hr=0x{:08X}", static_cast<unsigned>(hr));
        return result;
    }

    // Set options
    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
    if (multiSelect) options |= FOS_ALLOWMULTISELECT;
    dialog->SetOptions(options);

    // Set filters
    WideFilters wf = BuildFilterSpecs(filters, filterCount);
    if (!wf.specs.empty()) {
        dialog->SetFileTypes(static_cast<UINT>(wf.specs.size()), wf.specs.data());
    }

    SetDefaultDirectory(dialog, defaultDir);

    // Show
    hr = dialog->Show(static_cast<HWND>(parentHwnd));
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        dialog->Release();
        return result; // User cancelled — not an error
    }
    if (FAILED(hr)) {
        UZ_LOG(NativeDialog, Error, "IFileOpenDialog::Show failed: hr=0x{:08X}", static_cast<unsigned>(hr));
        dialog->Release();
        return result;
    }

    // Extract results
    IShellItemArray* items = nullptr;
    hr = dialog->GetResults(&items);
    if (SUCCEEDED(hr) && items) {
        DWORD count = 0;
        items->GetCount(&count);
        result.paths.reserve(count);
        for (DWORD i = 0; i < count; ++i) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(items->GetItemAt(i, &item)) && item) {
                std::string path = GetPathFromShellItem(item);
                if (!path.empty()) result.paths.push_back(std::move(path));
                item->Release();
            }
        }
        items->Release();
        result.ok = !result.paths.empty();
    }

    dialog->Release();
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShowSaveFileDialog
// ─────────────────────────────────────────────────────────────────────────────

FileDialogResult ShowSaveFileDialog(
    const DialogFilter* filters, int filterCount,
    const char* defaultName,
    const char* defaultDir,
    void* parentHwnd)
{
    FileDialogResult result{};

    IFileSaveDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) {
        UZ_LOG(NativeDialog, Error, "Failed to create IFileSaveDialog: hr=0x{:08X}", static_cast<unsigned>(hr));
        return result;
    }

    // Set options
    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT;
    dialog->SetOptions(options);

    // Set filters
    WideFilters wf = BuildFilterSpecs(filters, filterCount);
    if (!wf.specs.empty()) {
        dialog->SetFileTypes(static_cast<UINT>(wf.specs.size()), wf.specs.data());
    }

    // Set default file name
    if (defaultName) {
        const std::wstring wideName = Utf8ToWide(defaultName);
        if (!wideName.empty()) dialog->SetFileName(wideName.c_str());
    }

    SetDefaultDirectory(dialog, defaultDir);

    // Show
    hr = dialog->Show(static_cast<HWND>(parentHwnd));
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        dialog->Release();
        return result;
    }
    if (FAILED(hr)) {
        UZ_LOG(NativeDialog, Error, "IFileSaveDialog::Show failed: hr=0x{:08X}", static_cast<unsigned>(hr));
        dialog->Release();
        return result;
    }

    // Extract result
    IShellItem* item = nullptr;
    hr = dialog->GetResult(&item);
    if (SUCCEEDED(hr) && item) {
        std::string path = GetPathFromShellItem(item);
        if (!path.empty()) {
            result.paths.push_back(std::move(path));
            result.ok = true;
        }
        item->Release();
    }

    dialog->Release();
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShowMessageBox
// ─────────────────────────────────────────────────────────────────────────────

MessageBoxResult ShowMessageBox(
    const char* title, const char* message,
    MessageBoxButtons buttons,
    MessageBoxIcon icon,
    void* parentHwnd)
{
    UINT type = 0;

    switch (buttons) {
        case MessageBoxButtons::Ok:          type |= MB_OK; break;
        case MessageBoxButtons::OkCancel:    type |= MB_OKCANCEL; break;
        case MessageBoxButtons::YesNo:       type |= MB_YESNO; break;
        case MessageBoxButtons::YesNoCancel: type |= MB_YESNOCANCEL; break;
    }

    switch (icon) {
        case MessageBoxIcon::Info:     type |= MB_ICONINFORMATION; break;
        case MessageBoxIcon::Warning:  type |= MB_ICONWARNING; break;
        case MessageBoxIcon::Error:    type |= MB_ICONERROR; break;
        case MessageBoxIcon::Question: type |= MB_ICONQUESTION; break;
    }

    const std::wstring wideTitle = Utf8ToWide(title ? title : "");
    const std::wstring wideMsg = Utf8ToWide(message ? message : "");

    const int ret = MessageBoxW(
        static_cast<HWND>(parentHwnd),
        wideMsg.c_str(), wideTitle.c_str(), type);

    switch (ret) {
        case IDOK:     return MessageBoxResult::Ok;
        case IDYES:    return MessageBoxResult::Yes;
        case IDNO:     return MessageBoxResult::No;
        case IDCANCEL: return MessageBoxResult::Cancel;
        default:       return MessageBoxResult::Cancel;
    }
}

} // namespace uz

#endif // _WIN32
