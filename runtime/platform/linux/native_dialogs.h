#pragma once

/// UzEngine Native Dialogs — Linux file open/save and message box dialogs.
///
/// Uses `zenity` (GNOME) as the primary backend and `kdialog` (KDE) as a
/// fallback, invoked as subprocesses via fork/execvp. No GTK or Qt headers
/// are required — the tool handles the display connection.
///
/// All string parameters and results are UTF-8.
///
/// # Thread Safety
/// Must be called from the main (UI) thread. Each call forks a subprocess and
/// blocks until the user dismisses the dialog.
///
/// # Tool Requirements
/// At least one of `zenity` or `kdialog` must be installed and on PATH.
/// On Ubuntu/Debian: `sudo apt install zenity`
/// On KDE systems: `kdialog` is typically pre-installed.

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Filter specification for file dialogs.
///
/// Each filter is a (description, pattern) pair. The pattern uses
/// semicolon-separated wildcard extensions compatible with both
/// zenity (`--file-filter`) and kdialog filter syntax.
///
/// # Example
/// ```cpp
/// DialogFilter filters[] = {
///     { "Image Files", "*.png;*.jpg;*.bmp" },
///     { "All Files",   "*.*" },
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
/// Spawns `zenity --file-selection` (or `kdialog --getopenfilename`) as a
/// subprocess and blocks until the user dismisses the dialog.
/// If `multiSelect` is true, the user may select multiple files (zenity only;
/// kdialog fallback silently allows single selection).
///
/// # Arguments
/// * `filters` — Array of dialog filters, or nullptr for no filtering.
/// * `filterCount` — Number of entries in `filters`.
/// * `multiSelect` — Allow multiple file selection.
/// * `defaultDir` — UTF-8 path to set as the initial directory, or nullptr.
/// * `parentHwnd` — Ignored on Linux (present for API parity with Windows).
///
/// # Returns
/// A `FileDialogResult` with `ok == true` and populated paths on success,
/// or `ok == false` if the user cancelled or no dialog tool was found.
///
/// # Errors
/// Logs `NativeDialog::Error` if no tool is available or the subprocess fails.
[[nodiscard]] FileDialogResult ShowOpenFileDialog(
    const DialogFilter* filters, int filterCount,
    bool multiSelect = false,
    const char* defaultDir = nullptr,
    void* parentHwnd = nullptr);

/// Opens a native file save dialog.
///
/// Spawns `zenity --file-selection --save --confirm-overwrite` (or
/// `kdialog --getsavefilename`) as a subprocess and blocks until the user
/// dismisses the dialog.
///
/// # Arguments
/// * `filters` — Array of dialog filters, or nullptr for no filtering.
/// * `filterCount` — Number of entries in `filters`.
/// * `defaultName` — Suggested file name (UTF-8), or nullptr.
/// * `defaultDir` — UTF-8 path to set as the initial directory, or nullptr.
/// * `parentHwnd` — Ignored on Linux (present for API parity with Windows).
///
/// # Returns
/// A `FileDialogResult` with `ok == true` and a single path on success,
/// or `ok == false` if the user cancelled or no dialog tool was found.
///
/// # Errors
/// Logs `NativeDialog::Error` if no tool is available or the subprocess fails.
[[nodiscard]] FileDialogResult ShowSaveFileDialog(
    const DialogFilter* filters, int filterCount,
    const char* defaultName = nullptr,
    const char* defaultDir = nullptr,
    void* parentHwnd = nullptr);

/// Shows a native message box dialog.
///
/// Spawns `zenity --info/--warning/--error/--question` (or the corresponding
/// `kdialog` variant) as a subprocess and blocks until the user dismisses it.
///
/// # Arguments
/// * `title` — UTF-8 window title. Must not be nullptr.
/// * `message` — UTF-8 message body. Must not be nullptr.
/// * `buttons` — Button configuration.
/// * `icon` — Icon style.
/// * `parentHwnd` — Ignored on Linux (present for API parity with Windows).
///
/// # Returns
/// The button the user pressed. Returns `MessageBoxResult::Cancel` if the
/// dialog could not be created or no tool was found.
///
/// # Errors
/// Logs `NativeDialog::Error` if no tool is available or the subprocess fails.
[[nodiscard]] MessageBoxResult ShowMessageBox(
    const char* title, const char* message,
    MessageBoxButtons buttons = MessageBoxButtons::Ok,
    MessageBoxIcon icon = MessageBoxIcon::Info,
    void* parentHwnd = nullptr);

} // namespace uz
