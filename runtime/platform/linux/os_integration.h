#pragma once

/// UzEngine OS Integration — Linux system features.
///
/// Provides the same public API as the Windows `os_integration.h` counterpart,
/// with Linux-appropriate implementations or documented no-ops where a portable
/// equivalent does not exist without pulling in large X11/D-Bus dependencies.
///
/// # Platform Notes
/// - **Dark mode**: Detected via `gsettings` subprocess (GNOME color-scheme) with
///   fallback to the `gtk-theme-name` key for non-GNOME desktops.
/// - **Taskbar progress**: No standard Linux API exists. Functions are no-ops.
/// - **System tray icon**: Requires X11 XEmbed or D-Bus StatusNotifierItem — too
///   complex without external libraries. Functions are no-ops.
/// - **Tray notification**: Delegated to the `notify-send` subprocess, which works
///   on GNOME, KDE, XFCE, and any desktop with a libnotify-compatible daemon.
///
/// # Thread Safety
/// All functions must be called from the main thread. The dark mode callback
/// state is not protected by a mutex and must not be set from multiple threads.

#ifdef __linux__

#include <cstdint>

namespace uz {

/// Queries whether the desktop environment is in dark mode.
///
/// First queries `gsettings get org.gnome.desktop.interface color-scheme` and
/// checks for the value `'prefer-dark'`. If that key is absent or `gsettings`
/// is not available, falls back to reading `gtk-theme-name` and checking for
/// the case-insensitive substring `"dark"`.
///
/// Both probes run as short-lived child processes via `fork`/`execvp`.
///
/// # Returns
/// - `true` if a dark color scheme or dark theme name is detected.
/// - `false` if in light mode, no DE settings tool is available, or both
///   subprocess probes fail.
[[nodiscard]] bool IsSystemDarkMode();

/// Callback signature for dark mode change notifications.
///
/// # Arguments
/// * `isDark`    — `true` if the desktop has switched to dark mode.
/// * `userData`  — Opaque pointer passed verbatim from `SetDarkModeCallback`.
using DarkModeCallback = void(*)(bool isDark, void* userData);

/// Registers a callback to be invoked on dark mode changes.
///
/// On Linux there is no kernel-level notification for theme changes; the
/// callback is fired only when the application explicitly calls
/// `NotifyDarkModeChange()` (e.g., after polling `IsSystemDarkMode()` on a
/// timer or in response to a D-Bus signal the caller monitors externally).
///
/// # Arguments
/// * `callback`  — Function to call on dark mode change, or `nullptr` to clear.
/// * `userData`  — Opaque pointer forwarded to every callback invocation.
void SetDarkModeCallback(DarkModeCallback callback, void* userData);

/// Fires the registered dark mode callback with the current system state.
///
/// Calls `IsSystemDarkMode()` and passes the result to the stored callback.
/// No-op if no callback has been registered via `SetDarkModeCallback`.
void NotifyDarkModeChange();

/// Sets the taskbar progress bar value.
///
/// @note **No-op on Linux.** No cross-desktop standard for taskbar progress
/// exists without a D-Bus StatusNotifierItem + Unity/KDE extension. This
/// function logs a one-time warning and returns immediately.
///
/// # Arguments
/// * `hwnd`  — Ignored on Linux.
/// * `value` — Ignored on Linux.
/// * `max`   — Ignored on Linux.
void SetTaskbarProgress(void* hwnd, int value, int max = 100);

/// Clears the taskbar progress bar.
///
/// @note **No-op on Linux.** See `SetTaskbarProgress` for rationale.
///
/// # Arguments
/// * `hwnd` — Ignored on Linux.
void ClearTaskbarProgress(void* hwnd);

/// Shows a system tray icon.
///
/// @note **No-op on Linux.** Adding a tray icon requires X11 XEmbed protocol
/// or the D-Bus `org.kde.StatusNotifierItem` interface, neither of which can
/// be driven without external library headers (libX11 or libdbus). This
/// function logs a one-time warning and returns `false`.
///
/// # Arguments
/// * `hwnd`    — Ignored on Linux.
/// * `tooltip` — Ignored on Linux.
/// * `iconId`  — Ignored on Linux.
///
/// # Returns
/// Always `false` on Linux.
[[nodiscard]] bool ShowTrayIcon(void* hwnd, const char* tooltip, uint32_t iconId = 0);

/// Removes a system tray icon.
///
/// @note **No-op on Linux.** See `ShowTrayIcon` for rationale.
///
/// # Arguments
/// * `hwnd` — Ignored on Linux.
void HideTrayIcon(void* hwnd);

/// Shows a desktop notification via `notify-send`.
///
/// Spawns `notify-send "<title>" "<body>"` as a fire-and-forget child process
/// using `fork`/`execvp`. This works on any desktop with a libnotify-compatible
/// notification daemon (GNOME, KDE Plasma, XFCE, etc.).
///
/// The child process is not waited on — the notification is best-effort. If
/// `notify-send` is absent from PATH, the function logs a warning and returns.
///
/// @note Unlike the Windows implementation, this function does NOT require
/// `ShowTrayIcon` to be called first.
///
/// # Arguments
/// * `hwnd`  — Ignored on Linux (notifications are DE-level, not window-level).
/// * `title` — UTF-8 notification title. Must not be `nullptr`.
/// * `body`  — UTF-8 notification body text. Must not be `nullptr`.
void ShowTrayNotification(void* hwnd, const char* title, const char* body);

} // namespace uz

#endif // __linux__
