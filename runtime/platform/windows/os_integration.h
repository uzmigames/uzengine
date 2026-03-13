#pragma once

/// UzEngine OS Integration — Windows system features.
///
/// Provides access to system dark mode, taskbar progress, system tray icons,
/// and tray balloon notifications. All functions are Windows-only and must
/// be called from the main (UI) thread.
///
/// # COM Requirement
/// `SetTaskbarProgress` and `ClearTaskbarProgress` require COM initialization
/// on the calling thread (OleInitialize or CoInitializeEx).

#include <cstdint>

namespace uz {

/// Queries whether the Windows system is in dark mode (Apps theme).
///
/// Reads the registry key:
/// `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme`
///
/// # Returns
/// - `true` if the system is in dark (app) mode.
/// - `false` if in light mode or if the registry key cannot be read.
[[nodiscard]] bool IsSystemDarkMode();

/// Callback signature for dark mode change notifications.
///
/// # Arguments
/// * `isDark` — True if the system switched to dark mode.
/// * `userData` — Opaque pointer passed to `SetDarkModeCallback`.
using DarkModeCallback = void(*)(bool isDark, void* userData);

/// Registers a callback for dark mode changes.
///
/// The callback is invoked when external code (e.g., window proc) detects
/// a `WM_SETTINGCHANGE` with "ImmersiveColorSet". This function only stores
/// the callback — the caller's WndProc must call `NotifyDarkModeChange()`
/// to trigger it.
///
/// # Arguments
/// * `callback` — Function to call on dark mode change, or nullptr to clear.
/// * `userData` — Opaque pointer forwarded to the callback.
void SetDarkModeCallback(DarkModeCallback callback, void* userData);

/// Fires the registered dark mode callback with the current system state.
///
/// Intended to be called from a WndProc handler on `WM_SETTINGCHANGE`
/// when `lParam` contains "ImmersiveColorSet". No-op if no callback is set.
void NotifyDarkModeChange();

/// Sets the taskbar progress bar value on a window's taskbar button.
///
/// Uses `ITaskbarList3::SetProgressValue` to display a green progress
/// overlay on the taskbar icon. Calling with value >= max shows full.
///
/// # Arguments
/// * `hwnd` — Window handle (HWND cast to void*). Must not be nullptr.
/// * `value` — Current progress value (0 to `max`).
/// * `max` — Maximum progress value. Defaults to 100.
///
/// # Errors
/// Logs `OSIntegration::Warning` if the ITaskbarList3 interface is unavailable.
void SetTaskbarProgress(void* hwnd, int value, int max = 100);

/// Clears the taskbar progress bar from a window's taskbar button.
///
/// # Arguments
/// * `hwnd` — Window handle (HWND cast to void*). Must not be nullptr.
void ClearTaskbarProgress(void* hwnd);

/// Shows a system tray (notification area) icon for the specified window.
///
/// Creates or updates a tray icon using `Shell_NotifyIconW`. The icon
/// defaults to the application's main icon (loaded via `LoadIcon`).
///
/// # Arguments
/// * `hwnd` — Window handle (HWND cast to void*) for tray message routing.
/// * `tooltip` — UTF-8 tooltip text (max 127 chars). Must not be nullptr.
/// * `iconId` — Unique icon identifier for this window (default 0).
///
/// # Returns
/// - `true` if the tray icon was added successfully.
/// - `false` on failure (logs `OSIntegration::Error`).
[[nodiscard]] bool ShowTrayIcon(void* hwnd, const char* tooltip, uint32_t iconId = 0);

/// Removes the system tray icon for the specified window.
///
/// Safe to call if no tray icon was added (no-op).
///
/// # Arguments
/// * `hwnd` — Window handle (HWND cast to void*).
void HideTrayIcon(void* hwnd);

/// Shows a balloon notification from the system tray icon.
///
/// Requires that `ShowTrayIcon` was called first for this window.
/// Uses `Shell_NotifyIconW` with `NIF_INFO` to display a toast-style
/// balloon from the tray icon.
///
/// # Arguments
/// * `hwnd` — Window handle (HWND cast to void*).
/// * `title` — UTF-8 notification title (max 63 chars). Must not be nullptr.
/// * `body` — UTF-8 notification body text (max 255 chars). Must not be nullptr.
void ShowTrayNotification(void* hwnd, const char* title, const char* body);

} // namespace uz
