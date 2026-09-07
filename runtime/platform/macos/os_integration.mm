/// UzEngine OS Integration — macOS AppKit implementation.
///
/// Implements dark mode detection, Dock tile progress badges, NSStatusBar
/// tray icons, and `osascript`-based system notifications.
///
/// Compiled as Objective-C++ (.mm) because NSApp, NSDockTile, and
/// NSStatusBar are AppKit classes that require the ObjC runtime.

#if defined(__APPLE__)

#include "os_integration.h"
#include "log.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

UZ_DECLARE_LOG_CATEGORY(OsIntegration);
UZ_DEFINE_LOG_CATEGORY(OsIntegration, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Module-level state — intentionally file-local
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Registered dark mode change callback and its opaque context pointer.
/// Written only from the main thread; read from the same thread via
/// `NotifyDarkModeChange`. No atomic needed — AppKit is single-threaded.
DarkModeCallback s_darkModeCallback = nullptr;
void*            s_darkModeUserData = nullptr;

/// The live status bar item. Retaining it in a __strong static keeps the
/// item alive for the application lifetime after `ShowTrayIcon` is called.
NSStatusItem* __strong s_statusItem = nil;

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Escapes a UTF-8 C string for safe insertion into an AppleScript string
/// literal delimited by double-quotes.
///
/// Replaces `\` with `\\` and `"` with `\"` in-place into `out`.
/// `outSize` must include space for the null terminator.
///
/// # Returns
/// Pointer to `out` on success, nullptr if `src` does not fit in `outSize`.
[[nodiscard]] const char* EscapeForAppleScript(
    const char* src, char* out, size_t outSize) {

    size_t outIdx = 0U;
    for (const char* p = src; *p != '\0'; ++p) {
        const char c = *p;
        if (c == '\\' || c == '"') {
            if (outIdx + 2U >= outSize) {
                return nullptr;
            }
            out[outIdx++] = '\\';
        } else {
            if (outIdx + 1U >= outSize) {
                return nullptr;
            }
        }
        out[outIdx++] = c;
    }
    out[outIdx] = '\0';
    return out;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// IsSystemDarkMode
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] bool IsSystemDarkMode() {
    @autoreleasepool {
        // `AppleInterfaceStyle` is absent in light mode and set to "Dark" in
        // dark mode. This key is available on all macOS versions that UzEngine
        // targets (10.14+). Using NSUserDefaults rather than
        // NSApp.effectiveAppearance so the query works before NSApp is fully
        // initialised (e.g., during early engine startup).
        NSString* style = [[NSUserDefaults standardUserDefaults]
                           stringForKey:@"AppleInterfaceStyle"];
        return [style isEqualToString:@"Dark"];
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SetDarkModeCallback / NotifyDarkModeChange
// ─────────────────────────────────────────────────────────────────────────────

void SetDarkModeCallback(DarkModeCallback callback, void* userData) {
    s_darkModeCallback = callback;
    s_darkModeUserData = userData;
}

void NotifyDarkModeChange() {
    if (!s_darkModeCallback) {
        return;
    }
    const bool isDark = IsSystemDarkMode();
    s_darkModeCallback(isDark, s_darkModeUserData);
}

// ─────────────────────────────────────────────────────────────────────────────
// SetTaskbarProgress / ClearTaskbarProgress — Dock tile badge
// ─────────────────────────────────────────────────────────────────────────────

void SetTaskbarProgress(void* /*hwnd*/, int value, int max) {
    @autoreleasepool {
        // Guard against division by zero; treat a zero max as no progress.
        if (max <= 0) {
            ClearTaskbarProgress(nullptr);
            return;
        }

        // Clamp value into [0, max] so the badge label is never nonsensical.
        const int clamped = (value < 0) ? 0 : (value > max) ? max : value;

        // Express progress as an integer percentage for readability in the
        // Dock badge. A float badge (e.g. "42.7%") is too wide for most icons.
        const int percent = (clamped * 100) / max;

        NSString* label = [NSString stringWithFormat:@"%d%%", percent];
        [[NSApp dockTile] setBadgeLabel:label];

        UZ_LOG(OsIntegration, Info,
               "Dock tile progress: {}% ({}/{})", percent, clamped, max);
    }
}

void ClearTaskbarProgress(void* /*hwnd*/) {
    @autoreleasepool {
        // An empty string removes the badge entirely without hiding the icon.
        [[NSApp dockTile] setBadgeLabel:@""];
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ShowTrayIcon / HideTrayIcon — NSStatusBar item
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] bool ShowTrayIcon(void* /*hwnd*/, const char* tooltip, uint32_t /*iconId*/) {
    @autoreleasepool {
        if (!tooltip) {
            UZ_LOG(OsIntegration, Error, "ShowTrayIcon: tooltip must not be nullptr");
            return false;
        }

        // Remove any existing item before creating a new one so we do not
        // accumulate ghost icons when ShowTrayIcon is called multiple times.
        if (s_statusItem) {
            [[NSStatusBar systemStatusBar] removeStatusItem:s_statusItem];
            s_statusItem = nil;
        }

        s_statusItem = [[NSStatusBar systemStatusBar]
                        statusItemWithLength:NSVariableStatusItemLength];
        if (!s_statusItem) {
            UZ_LOG(OsIntegration, Error,
                   "ShowTrayIcon: statusItemWithLength returned nil");
            return false;
        }

        NSString* tooltipNs = [NSString stringWithUTF8String:tooltip];

        // Use the application name as the visible button label. Prefer a
        // template image when one is available, but fall back to text so the
        // item is always visible even in a minimal build without bundled assets.
        NSStatusBarButton* button = [s_statusItem button];
        if (button) {
            // Attempt to load the application icon from the bundle.
            NSImage* appIcon = [[NSBundle mainBundle]
                                imageForResource:@"AppIcon"];
            if (appIcon) {
                // Resize to the standard menu-bar height so it is not clipped.
                [appIcon setSize:NSMakeSize(18.0F, 18.0F)];
                [appIcon setTemplate:YES]; // Automatic dark/light tinting.
                [button setImage:appIcon];
            } else {
                // No bundled icon — use the process name as plain text.
                NSString* appName =
                    [[NSProcessInfo processInfo] processName];
                [button setTitle:appName ?: @"UzEngine"];
            }
            [button setToolTip:tooltipNs];
        }

        UZ_LOG(OsIntegration, Info,
               "ShowTrayIcon: status bar item created (tooltip='{}')", tooltip);
        return true;
    }
}

void HideTrayIcon(void* /*hwnd*/) {
    @autoreleasepool {
        if (!s_statusItem) {
            return; // Nothing to remove — silent no-op.
        }
        [[NSStatusBar systemStatusBar] removeStatusItem:s_statusItem];
        s_statusItem = nil;
        UZ_LOG(OsIntegration, Info, "HideTrayIcon: status bar item removed");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ShowTrayNotification — osascript subprocess
// ─────────────────────────────────────────────────────────────────────────────

void ShowTrayNotification(void* /*hwnd*/, const char* title, const char* body) {
    if (!title || !body) {
        UZ_LOG(OsIntegration, Error,
               "ShowTrayNotification: title and body must not be nullptr");
        return;
    }

    // Escape both strings before embedding them in the AppleScript literal.
    // 512 characters is generous for notification text; truncation is
    // preferable to a buffer overrun.
    constexpr size_t kEscapedBufSize = 512U;
    char escapedTitle[kEscapedBufSize];
    char escapedBody[kEscapedBufSize];

    if (!EscapeForAppleScript(title, escapedTitle, kEscapedBufSize)) {
        UZ_LOG(OsIntegration, Warning,
               "ShowTrayNotification: title too long or unescapable — truncated");
        std::strncpy(escapedTitle, title, kEscapedBufSize - 1U);
        escapedTitle[kEscapedBufSize - 1U] = '\0';
    }

    if (!EscapeForAppleScript(body, escapedBody, kEscapedBufSize)) {
        UZ_LOG(OsIntegration, Warning,
               "ShowTrayNotification: body too long or unescapable — truncated");
        std::strncpy(escapedBody, body, kEscapedBufSize - 1U);
        escapedBody[kEscapedBufSize - 1U] = '\0';
    }

    // Build: osascript -e 'display notification "<body>" with title "<title>"'
    // The total command buffer is sized to accommodate two escaped 512-char
    // fields plus the fixed AppleScript template (~80 chars).
    constexpr size_t kCmdBufSize = kEscapedBufSize * 2U + 128U;
    char cmd[kCmdBufSize];

    const int written = std::snprintf(
        cmd, kCmdBufSize,
        "osascript -e 'display notification \"%s\" with title \"%s\"'",
        escapedBody, escapedTitle);

    if (written <= 0 || static_cast<size_t>(written) >= kCmdBufSize) {
        UZ_LOG(OsIntegration, Error,
               "ShowTrayNotification: failed to build osascript command");
        return;
    }

    // `system()` blocks until osascript exits, but osascript displays the
    // banner immediately and returns quickly. The call is acceptable here
    // because notifications are infrequent and not on the render-hot-path.
    const int rc = std::system(cmd); // NOLINT(cert-env33-c)
    if (rc != 0) {
        UZ_LOG(OsIntegration, Warning,
               "ShowTrayNotification: osascript exited with code {}", rc);
    }
}

} // namespace uz

#endif // __APPLE__
