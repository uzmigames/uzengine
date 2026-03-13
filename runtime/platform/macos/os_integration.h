#pragma once

/// UzEngine OS Integration — macOS system features.
///
/// Provides access to system dark mode, Dock tile progress, status bar (tray)
/// icons, and system notifications. All functions are macOS-only and must be
/// called from the main thread unless noted otherwise.
///
/// # AppKit Requirement
/// The implementation file is compiled as Objective-C++ (.mm) and links
/// against AppKit. The calling target must link `-framework AppKit`.
///
/// # Notification Entitlement
/// `ShowTrayNotification` uses `osascript` under the hood so it works without
/// the `com.apple.security.app-sandbox` entitlement and without requesting
/// `UNUserNotificationCenter` authorization.

#if defined(__APPLE__)

#include <cstdint>

namespace uz {

/// Queries whether the macOS system is in dark mode.
///
/// Reads the `AppleInterfaceStyle` key from `NSUserDefaults` standard
/// defaults. A value of `"Dark"` means dark mode is active; any other
/// value (including absence of the key) means light mode.
///
/// # Returns
/// - `true`  if the system appearance is Dark Aqua.
/// - `false` if in Aqua (light) mode or if the preference cannot be read.
[[nodiscard]] bool IsSystemDarkMode();

/// Callback signature for dark mode change notifications.
///
/// # Arguments
/// * `isDark`    — True if the system switched to dark mode.
/// * `userData`  — Opaque pointer passed to `SetDarkModeCallback`.
using DarkModeCallback = void(*)(bool isDark, void* userData);

/// Registers a callback for dark mode changes.
///
/// The callback is invoked when external code detects a
/// `NSAppearanceDidChangeNotification` or when the caller explicitly calls
/// `NotifyDarkModeChange()`. Only one callback is active at a time.
///
/// # Arguments
/// * `callback`  — Function to call on dark mode change, or nullptr to clear.
/// * `userData`  — Opaque pointer forwarded to the callback unchanged.
void SetDarkModeCallback(DarkModeCallback callback, void* userData);

/// Fires the registered dark mode callback with the current system state.
///
/// Intended to be called when the application detects an appearance change
/// (e.g., from an `NSDistributedNotificationCenter` observer on
/// `"AppleInterfaceThemeChangedNotification"`). No-op if no callback is set.
void NotifyDarkModeChange();

/// Sets the Dock tile progress badge for the running application.
///
/// Uses `[NSApp dockTile]` to display a percentage badge label on the
/// application's Dock icon (e.g., `"42%"`). This is the macOS equivalent
/// of the Windows taskbar progress indicator.
///
/// The `hwnd` parameter exists solely for API parity with the Windows
/// version; it is ignored on macOS (the Dock tile is per-application, not
/// per-window).
///
/// # Arguments
/// * `hwnd`  — Ignored. Pass nullptr for clarity.
/// * `value` — Current progress value (0 to `max`).
/// * `max`   — Maximum progress value. Defaults to 100.
///
/// # Thread Safety
/// Must be called from the main thread; AppKit is not thread-safe.
void SetTaskbarProgress(void* hwnd, int value, int max = 100);

/// Clears the Dock tile progress badge from the application's Dock icon.
///
/// Sets the badge label to an empty string, removing the percentage overlay.
///
/// # Arguments
/// * `hwnd` — Ignored. Pass nullptr for clarity.
void ClearTaskbarProgress(void* hwnd);

/// Adds a status bar icon (macOS equivalent of a system tray icon).
///
/// Creates an `NSStatusItem` in `NSStatusBar.systemStatusBar` with the
/// application name as its button title. If a status item already exists
/// it is replaced. The `hwnd` and `iconId` parameters are accepted for
/// API parity but are unused on macOS (there is no per-window tray routing).
///
/// # Arguments
/// * `hwnd`    — Ignored. Pass nullptr for clarity.
/// * `tooltip` — UTF-8 tooltip text shown when the user hovers the item.
///               Must not be nullptr.
/// * `iconId`  — Ignored. Pass 0 for clarity.
///
/// # Returns
/// - `true`  if the status item was created successfully.
/// - `false` on allocation failure (logs `OsIntegration::Error`).
[[nodiscard]] bool ShowTrayIcon(void* hwnd, const char* tooltip, uint32_t iconId = 0);

/// Removes the status bar icon created by `ShowTrayIcon`.
///
/// Releases the `NSStatusItem` from `NSStatusBar.systemStatusBar`. Safe to
/// call if no status item was created (no-op).
///
/// # Arguments
/// * `hwnd` — Ignored. Pass nullptr for clarity.
void HideTrayIcon(void* hwnd);

/// Shows a macOS system notification via `osascript`.
///
/// Executes `osascript -e 'display notification "<body>" with title "<title>"'`
/// in a detached subprocess. This approach works without App Sandbox
/// entitlements or `UNUserNotificationCenter` authorization dialogs.
///
/// Special characters in `title` and `body` are escaped before being
/// inserted into the AppleScript string to prevent injection.
///
/// The `hwnd` parameter is accepted for API parity and is ignored.
///
/// # Arguments
/// * `hwnd`  — Ignored. Pass nullptr for clarity.
/// * `title` — UTF-8 notification title. Must not be nullptr.
/// * `body`  — UTF-8 notification body text. Must not be nullptr.
///
/// # Side Effects
/// Spawns a short-lived `osascript` subprocess.
/// Logs `OsIntegration::Warning` if `osascript` cannot be found or fails.
void ShowTrayNotification(void* hwnd, const char* title, const char* body);

} // namespace uz

#endif // __APPLE__
