#pragma once

/// UzEngine Native Dialogs — Win32 file open/save and message box dialogs.
///
/// Uses the IFileOpenDialog / IFileSaveDialog COM interfaces (Vista+ Common
/// Item Dialog) for modern file dialogs, and MessageBoxW for message boxes.
///
/// All string parameters and results are UTF-8. Wide-char conversion is
/// handled internally.
///
/// # Thread Safety
/// Must be called from the main (UI) thread. COM must be initialized on
/// the calling thread (OleInitialize or CoInitializeEx).

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Filter specification for file dialogs.
///
/// Each filter is a (description, pattern) pair. The pattern uses
/// Win32 wildcard syntax with semicolons for multiple extensions.
///
/// # Example
/// ```cpp
/// DialogFilter filters[] = {
///     { "Image Files", "*.png;*.jpg;*.bmp" },
///     { "All Files", "*.*" },
/// };
/// ```
struct DialogFilter {
    const char* description; ///< Human-readable label (e.g. "Image Files").
    const char* extensions;  ///< Semicolon-separated wildcards (e.g. "*.png;*.jpg").
};

/// Result from a file open or save dialog.
struct FileDialogResult {
    bool ok;                        ///< True if the user confirmed a selection.
    std::vector<std::string> paths; ///< Selected file paths (UTF-8). Empty if `ok` is false.
};

/// Button configurations for message boxes.
enum class MessageBoxButtons : uint8_t {
    Ok,          ///< Single "OK" button.
    OkCancel,    ///< "OK" and "Cancel" buttons.
    YesNo,       ///< "Yes" and "No" buttons.
    YesNoCancel, ///< "Yes", "No", and "Cancel" buttons.
};

/// Icon type for message boxes.
enum class MessageBoxIcon : uint8_t {
    Info,     ///< Informational (i) icon.
    Warning,  ///< Warning (!) icon.
    Error,    ///< Error (X) icon.
    Question, ///< Question (?) icon.
};

/// Result indicating which button the user pressed.
enum class MessageBoxResult : uint8_t {
    Ok,     ///< User pressed OK.
    Cancel, ///< User pressed Cancel or closed the dialog.
    Yes,    ///< User pressed Yes.
    No,     ///< User pressed No.
};

/// Opens a native file open dialog.
///
/// Displays the Windows Common Item Dialog for selecting one or more files.
/// If `multiSelect` is true, the user may select multiple files; otherwise
/// only a single selection is allowed.
///
/// # Arguments
/// * `filters` — Array of dialog filters, or nullptr for no filtering.
/// * `filterCount` — Number of entries in `filters`.
/// * `multiSelect` — Allow multiple file selection.
/// * `defaultDir` — UTF-8 path to set as the initial directory, or nullptr.
/// * `parentHwnd` — Owner window handle (HWND cast to void*), or nullptr.
///
/// # Returns
/// A `FileDialogResult` with `ok == true` and populated paths on success,
/// or `ok == false` if the user cancelled or an error occurred.
///
/// # Errors
/// Logs `NativeDialog::Error` on COM failure with the HRESULT.
[[nodiscard]] FileDialogResult ShowOpenFileDialog(
    const DialogFilter* filters, int filterCount,
    bool multiSelect = false,
    const char* defaultDir = nullptr,
    void* parentHwnd = nullptr);

/// Opens a native file save dialog.
///
/// Displays the Windows Common Item Dialog for choosing a save location.
/// The dialog enforces the selected filter extension if the user omits it.
///
/// # Arguments
/// * `filters` — Array of dialog filters, or nullptr for no filtering.
/// * `filterCount` — Number of entries in `filters`.
/// * `defaultName` — Suggested file name (UTF-8), or nullptr.
/// * `defaultDir` — UTF-8 path to set as the initial directory, or nullptr.
/// * `parentHwnd` — Owner window handle (HWND cast to void*), or nullptr.
///
/// # Returns
/// A `FileDialogResult` with `ok == true` and a single path on success,
/// or `ok == false` if the user cancelled or an error occurred.
///
/// # Errors
/// Logs `NativeDialog::Error` on COM failure with the HRESULT.
[[nodiscard]] FileDialogResult ShowSaveFileDialog(
    const DialogFilter* filters, int filterCount,
    const char* defaultName = nullptr,
    const char* defaultDir = nullptr,
    void* parentHwnd = nullptr);

/// Shows a native message box dialog.
///
/// Uses Win32 MessageBoxW for a lightweight modal dialog.
///
/// # Arguments
/// * `title` — UTF-8 window title. Must not be nullptr.
/// * `message` — UTF-8 message body. Must not be nullptr.
/// * `buttons` — Button configuration.
/// * `icon` — Icon style.
/// * `parentHwnd` — Owner window handle (HWND cast to void*), or nullptr.
///
/// # Returns
/// The button the user pressed. Returns `MessageBoxResult::Cancel` if the
/// dialog could not be created.
[[nodiscard]] MessageBoxResult ShowMessageBox(
    const char* title, const char* message,
    MessageBoxButtons buttons = MessageBoxButtons::Ok,
    MessageBoxIcon icon = MessageBoxIcon::Info,
    void* parentHwnd = nullptr);

} // namespace uz
