#pragma once

/// UzEngine Native Dialogs — macOS AppKit file open/save and message box dialogs.
///
/// Uses `NSOpenPanel` / `NSSavePanel` for file dialogs and `NSAlert` for message
/// boxes. All dialogs are modal and block the calling thread until dismissed.
///
/// All string parameters and results are UTF-8. Conversion to/from NSString is
/// handled internally.
///
/// # Thread Safety
/// Must be called from the main thread. AppKit panels are not thread-safe and
/// will assert or produce undefined behaviour if invoked from a background thread.

#if defined(__APPLE__)

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Filter specification for file dialogs.
///
/// Each filter is a (description, pattern) pair. The pattern uses semicolons to
/// separate multiple extensions, each written as a wildcard (e.g. `*.png`).
/// The leading `*.` prefix is stripped when building the AppKit allowed-types list.
///
/// # Example
/// ```cpp
/// DialogFilter filters[] = {
///     { "Image Files", "*.png;*.jpg;*.bmp" },
///     { "All Files",   "*.*"               },
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
    Info,     ///< Informational icon (NSAlertStyleInformational).
    Warning,  ///< Warning icon (NSAlertStyleWarning).
    Error,    ///< Critical/error icon (NSAlertStyleCritical).
    Question, ///< Informational icon — AppKit has no dedicated question icon.
};

/// Result indicating which button the user pressed.
enum class MessageBoxResult : uint8_t {
    Ok,     ///< User pressed OK.
    Cancel, ///< User pressed Cancel or closed the dialog.
    Yes,    ///< User pressed Yes.
    No,     ///< User pressed No.
};

/// Opens a native file open dialog using NSOpenPanel.
///
/// Displays a modal sheet (or app-modal panel if `parentWindow` is nullptr)
/// for selecting one or more files. If `multiSelect` is true the user may
/// select multiple items; otherwise only a single selection is allowed.
///
/// Extension filtering is applied via `NSOpenPanel.allowedContentTypes`
/// (macOS 12+) or `NSOpenPanel.allowedFileTypes` on older systems.
/// Passing `nullptr` for `filters` / `filterCount == 0` disables filtering.
///
/// # Arguments
/// * `filters`     — Array of dialog filters, or nullptr for no filtering.
/// * `filterCount` — Number of entries in `filters`.
/// * `multiSelect` — Allow multiple file selection.
/// * `defaultDir`  — UTF-8 path to set as the initial directory, or nullptr.
/// * `parentWindow` — Owning NSWindow pointer cast to void*, or nullptr for
///                    app-modal behaviour.
///
/// # Returns
/// A `FileDialogResult` with `ok == true` and populated paths on success,
/// or `ok == false` if the user cancelled.
///
/// # Errors
/// Logs `NativeDialog::Error` if NSOpenPanel cannot be created or if the
/// defaultDir path cannot be converted to an NSURL.
[[nodiscard]] FileDialogResult ShowOpenFileDialog(
    const DialogFilter* filters, int filterCount,
    bool multiSelect = false,
    const char* defaultDir = nullptr,
    void* parentWindow = nullptr);

/// Opens a native file save dialog using NSSavePanel.
///
/// Displays a modal save panel pre-populated with `defaultName` in the name
/// field and `defaultDir` as the initial directory.
///
/// Extension filtering follows the same rules as `ShowOpenFileDialog`.
/// On macOS the user can freely type any name; the filter is advisory only.
///
/// # Arguments
/// * `filters`      — Array of dialog filters, or nullptr for no filtering.
/// * `filterCount`  — Number of entries in `filters`.
/// * `defaultName`  — Suggested file name (UTF-8), or nullptr.
/// * `defaultDir`   — UTF-8 path to set as the initial directory, or nullptr.
/// * `parentWindow` — Owning NSWindow pointer cast to void*, or nullptr.
///
/// # Returns
/// A `FileDialogResult` with `ok == true` and a single path on success,
/// or `ok == false` if the user cancelled.
///
/// # Errors
/// Logs `NativeDialog::Error` if NSSavePanel cannot be created.
[[nodiscard]] FileDialogResult ShowSaveFileDialog(
    const DialogFilter* filters, int filterCount,
    const char* defaultName = nullptr,
    const char* defaultDir = nullptr,
    void* parentWindow = nullptr);

/// Shows a native message box using NSAlert.
///
/// Displays a modal alert with the specified title, message, buttons, and icon
/// style. The call blocks until the user dismisses the dialog.
///
/// `MessageBoxIcon::Question` maps to `NSAlertStyleInformational` because AppKit
/// does not have a distinct question-mark icon style.
///
/// # Arguments
/// * `title`        — UTF-8 window title. Must not be nullptr.
/// * `message`      — UTF-8 informative text. Must not be nullptr.
/// * `buttons`      — Button configuration determining which buttons appear.
/// * `icon`         — Alert style controlling the icon shown.
/// * `parentWindow` — Owning NSWindow pointer cast to void*, or nullptr for
///                    app-modal behaviour (ignored; NSAlert always runs app-modal).
///
/// # Returns
/// The button the user pressed. Returns `MessageBoxResult::Cancel` if the alert
/// could not be created.
[[nodiscard]] MessageBoxResult ShowMessageBox(
    const char* title, const char* message,
    MessageBoxButtons buttons = MessageBoxButtons::Ok,
    MessageBoxIcon icon = MessageBoxIcon::Info,
    void* parentWindow = nullptr);

} // namespace uz

#endif // __APPLE__
