/// UzEngine OS Integration Implementation (Linux)
///
/// Dark mode detection via gsettings/gtk-theme-name subprocess probes.
/// Taskbar progress and system tray icon are no-ops (no cross-desktop standard).
/// Desktop notifications via notify-send subprocess (fire-and-forget).
///
/// All subprocess work uses fork/execvp directly — no shell metacharacter risk,
/// no external library dependencies beyond the standard POSIX headers.

#include "os_integration.h"

#ifdef __linux__

#include "log.h"

#include <cerrno>
#include <cctype>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

UZ_DEFINE_LOG_CATEGORY(OsIntegration, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal subprocess helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Maximum bytes read from a gsettings probe stdout.
constexpr size_t kProbeReadMax = 512;

/// Forks a child process, runs `argv` via execvp, and returns the first
/// `kProbeReadMax` bytes written to the child's stdout.
///
/// Stderr is redirected to /dev/null. Returns an empty string if fork fails,
/// execvp fails (exit code 127), or the child writes nothing.
std::string RunReadProbe(const char* const* argv) {
    int pipeFds[2] = { -1, -1 };
    if (::pipe(pipeFds) != 0) {
        return {};
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return {};
    }

    if (pid == 0) {
        // Child: wire stdout → pipe write end, silence stderr.
        ::close(pipeFds[0]);
        ::dup2(pipeFds[1], STDOUT_FILENO);
        ::close(pipeFds[1]);

        const int devNull = ::open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            ::dup2(devNull, STDERR_FILENO);
            ::close(devNull);
        }

        ::execvp(argv[0], const_cast<char* const*>(argv));
        ::_exit(127); // execvp failed — tool not on PATH
    }

    // Parent: close write end, read up to kProbeReadMax bytes.
    ::close(pipeFds[1]);

    char buf[kProbeReadMax + 1];
    ssize_t totalRead = 0;
    ssize_t n = 0;
    while (totalRead < static_cast<ssize_t>(kProbeReadMax)) {
        n = ::read(pipeFds[0], buf + totalRead,
                   kProbeReadMax - static_cast<size_t>(totalRead));
        if (n <= 0) break;
        totalRead += n;
    }
    buf[totalRead] = '\0';
    ::close(pipeFds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    // Exit 127 → execvp failed (tool absent); treat as empty output.
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        return {};
    }

    return std::string(buf, static_cast<size_t>(totalRead));
}

/// Spawns `argv` as a fire-and-forget child (stdout/stderr → /dev/null).
/// Does NOT wait for the child to exit — suitable for notifications.
void RunFireAndForget(const char* const* argv) {
    const pid_t pid = ::fork();
    if (pid < 0) return;

    if (pid == 0) {
        // Child: silence all output so nothing leaks to the engine console.
        const int devNull = ::open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            ::dup2(devNull, STDOUT_FILENO);
            ::dup2(devNull, STDERR_FILENO);
            ::close(devNull);
        }

        ::execvp(argv[0], const_cast<char* const*>(argv));
        ::_exit(127);
    }

    // Parent: detach the child. We use SIGCHLD=SIG_DFL; the caller is
    // responsible for reaping or accepting zombie cleanup by the OS.
    // For a short-lived notification helper this is acceptable.
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Dark Mode Detection
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Probes `gsettings get org.gnome.desktop.interface color-scheme` and returns
/// true if the output contains `'prefer-dark'`.
bool ProbeGnomeColorScheme() {
    const char* argv[] = {
        "gsettings", "get", "org.gnome.desktop.interface", "color-scheme", nullptr
    };
    const std::string output = RunReadProbe(argv);
    // gsettings prints the GVariant value with surrounding quotes, e.g.:
    //   'prefer-dark'
    //   'default'
    return output.find("prefer-dark") != std::string::npos;
}

/// Probes `gsettings get org.gnome.desktop.interface gtk-theme-name` and returns
/// true if the theme name contains the case-insensitive substring "dark".
bool ProbeGtkThemeName() {
    const char* argv[] = {
        "gsettings", "get", "org.gnome.desktop.interface", "gtk-theme-name", nullptr
    };
    const std::string output = RunReadProbe(argv);
    if (output.empty()) return false;

    // Case-insensitive search for "dark" in the theme name string.
    std::string lower = output;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower.find("dark") != std::string::npos;
}

} // anonymous namespace

bool IsSystemDarkMode() {
    // Primary check: GNOME/Mutter color-scheme preference (GNOME 42+, elementary OS).
    if (ProbeGnomeColorScheme()) {
        return true;
    }

    // Fallback: GTK theme name heuristic for older GNOME, KDE (uses GTK theme
    // compat key), XFCE, and other GTK-based desktops that lack color-scheme.
    return ProbeGtkThemeName();
}

// ─────────────────────────────────────────────────────────────────────────────
// Dark Mode Callback
// ─────────────────────────────────────────────────────────────────────────────

/// Stored dark mode callback and its associated user data pointer.
static DarkModeCallback g_darkModeCallback = nullptr;
static void*            g_darkModeUserData = nullptr;

void SetDarkModeCallback(DarkModeCallback callback, void* userData) {
    g_darkModeCallback = callback;
    g_darkModeUserData = userData;
}

void NotifyDarkModeChange() {
    if (g_darkModeCallback) {
        g_darkModeCallback(IsSystemDarkMode(), g_darkModeUserData);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Taskbar Progress — no-op on Linux
// ─────────────────────────────────────────────────────────────────────────────

void SetTaskbarProgress(void* /*hwnd*/, int /*value*/, int /*max*/) {
    // No cross-desktop standard for taskbar progress without D-Bus extensions.
    // Warn once per process lifetime to avoid log spam.
    static bool s_warned = false;
    if (!s_warned) {
        s_warned = true;
        UZ_LOG(OsIntegration, Warning,
            "SetTaskbarProgress: not supported on Linux "
            "(no cross-desktop taskbar progress API without D-Bus extensions)");
    }
}

void ClearTaskbarProgress(void* /*hwnd*/) {
    // Intentional no-op — see SetTaskbarProgress.
}

// ─────────────────────────────────────────────────────────────────────────────
// System Tray Icon — no-op on Linux
// ─────────────────────────────────────────────────────────────────────────────

bool ShowTrayIcon(void* /*hwnd*/, const char* /*tooltip*/, uint32_t /*iconId*/) {
    // Tray icons require X11 XEmbed or D-Bus org.kde.StatusNotifierItem.
    // Neither can be driven via subprocess without pulling in libX11/libdbus.
    static bool s_warned = false;
    if (!s_warned) {
        s_warned = true;
        UZ_LOG(OsIntegration, Warning,
            "ShowTrayIcon: not supported on Linux "
            "(requires X11 XEmbed or D-Bus StatusNotifierItem — "
            "not available in subprocess-only integration mode)");
    }
    return false;
}

void HideTrayIcon(void* /*hwnd*/) {
    // Intentional no-op — see ShowTrayIcon.
}

// ─────────────────────────────────────────────────────────────────────────────
// Desktop Notifications via notify-send
// ─────────────────────────────────────────────────────────────────────────────

void ShowTrayNotification(void* /*hwnd*/, const char* title, const char* body) {
    if (!title || !body) return;

    // notify-send is provided by libnotify-bin on Debian/Ubuntu, libnotify on
    // Fedora/Arch. Works with any libnotify-compatible notification daemon
    // (GNOME Shell, KDE Plasma, XFCE notifyd, dunst, mako, etc.).
    //
    // We pass title and body as separate positional arguments to avoid any
    // shell-injection risk — execvp bypasses the shell entirely.
    const char* argv[] = {
        "notify-send",
        title,
        body,
        nullptr
    };

    RunFireAndForget(argv);

    UZ_LOG(OsIntegration, Trace,
        "ShowTrayNotification: dispatched notify-send: title='{}'", title);
}

} // namespace uz

#endif // __linux__
