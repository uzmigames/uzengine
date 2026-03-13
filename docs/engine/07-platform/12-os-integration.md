# Platform — OS Integration

> **Status**: Planned (Phase 4.9.0)
> **Source**: `platform/windows/`
> **Depends on**: COM `ITaskbarList3`, `Shell_NotifyIcon`, Registry, Windows Toast API

## Purpose

Beyond dialogs and notifications, the editor integrates deeply with Windows: dark mode UI theming, taskbar progress feedback during long operations, system tray for always-available access, and persistent notifications. These features make the editor feel like a native Windows application, not a web app ported to the desktop.

## Architecture

```
Windows OS Integration
    │
    ├── Dark Mode Detection & Theming
    │   ├── Registry: AppsUseLightTheme
    │   └── WM_SETTINGCHANGE listener
    │
    ├── Taskbar Integration
    │   ├── ITaskbarList3::SetProgressValue()
    │   ├── ITaskbarList3::SetProgressState()
    │   └── Overlay icon for status
    │
    ├── System Tray
    │   ├── Shell_NotifyIcon (NIM_ADD)
    │   └── Context menu on right-click
    │
    └── Toast Notifications
        ├── Windows.UI.Notifications API
        └── XML toast template
```

## Dark Mode Theme System

```cpp
/// System appearance setting.
enum class SystemTheme {
    Light,
    Dark,
    Auto,  // Follow Windows setting
};

/// Get current Windows dark mode setting.
bool IsDarkModeEnabled() {
    // Check registry: HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
    // Value: AppsUseLightTheme (0 = dark, 1 = light)
    return GetRegistryDword("AppsUseLightTheme") == 0;
}

/// Listen for dark mode changes via WM_SETTINGCHANGE.
void OnSystemThemeChanged() {
    if (IsDarkModeEnabled()) {
        g_ui->ApplyTheme("dark");
        g_editor->SetWindowTitlebarColorDark();
    } else {
        g_ui->ApplyTheme("light");
        g_editor->SetWindowTitlebarColorLight();
    }
}

// In window message loop
case WM_SETTINGCHANGE: {
    if (wparam == SPI_SETLOGICALDPISIZE) {
        // DPI changed (handled separately in 03-multi-monitor-dpi.md)
    }
    if (wcscmp(reinterpret_cast<LPCWSTR>(lparam),
        L"AppsUseLightTheme") == 0) {
        OnSystemThemeChanged();
    }
    break;
}
```

## Taskbar Integration (ITaskbarList3)

```cpp
/// Taskbar progress states.
enum class TaskbarProgressState {
    Normal,     // Blue progress bar
    Indeterminate,  // Green animated bar
    Error,      // Red error indicator
    Paused,     // Yellow paused indicator
};

/// Set taskbar progress indicator.
void SetTaskbarProgress(uint32_t value, uint32_t max) {
    // Get ITaskbarList3 COM interface
    ITaskbarList3* taskbar = nullptr;
    CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITaskbarList3, (void**)&taskbar);

    taskbar->SetProgressState(m_hwnd, TBPF_NORMAL);
    taskbar->SetProgressValue(m_hwnd, value, max);
}

/// Set taskbar state (loading, error, etc).
void SetTaskbarState(TaskbarProgressState state) {
    ITaskbarList3* taskbar = nullptr;
    CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITaskbarList3, (void**)&taskbar);

    TBPFLAG flags;
    switch (state) {
        case TaskbarProgressState::Normal:
            flags = TBPF_NORMAL;
            break;
        case TaskbarProgressState::Indeterminate:
            flags = TBPF_INDETERMINATE;
            break;
        case TaskbarProgressState::Error:
            flags = TBPF_ERROR;
            break;
        case TaskbarProgressState::Paused:
            flags = TBPF_PAUSED;
            break;
    }

    taskbar->SetProgressState(m_hwnd, flags);
}

// Usage: Long-running operation
SetTaskbarState(TaskbarProgressState::Indeterminate);
UZ_LOG(Editor, Info, "Baking lights...");

for (uint32_t i = 0; i < 100; ++i) {
    BakeLightmap(i);
    SetTaskbarProgress(i, 100);
}

SetTaskbarState(TaskbarProgressState::Normal);
SetTaskbarProgress(0, 0);  // Clear progress
```

## System Tray Icon

```cpp
/// Tray icon context menu item.
struct TrayMenuEntry {
    std::string label;
    std::function<void()> callback;
    bool is_separator = false;
};

/// Show icon in system tray.
void ShowTrayIcon(
    const char* icon_path,
    const char* tooltip,
    const std::vector<TrayMenuEntry>& menu);

/// Hide tray icon.
void HideTrayIcon();

/// Callback when tray icon clicked or hovered.
using TrayIconCallback = std::function<void(int x, int y)>;
void OnTrayLeftClick(TrayIconCallback callback);
void OnTrayRightClick(TrayIconCallback callback);

// Usage: Background mode with tray menu
std::vector<TrayMenuEntry> tray_menu = {
    {"Show Window", []() { g_window->Show(); g_window->SetFocus(); }},
    {"", nullptr},  // Separator
    {"Exit", []() { g_engine->Shutdown(); }},
};

ShowTrayIcon("icons/uz-logo.png", "UzEngine", tray_menu);

OnTrayLeftClick([](int x, int y) {
    if (g_window->IsVisible()) {
        g_window->Hide();
    } else {
        g_window->Show();
        g_window->SetFocus();
    }
});
```

## Toast Notifications

```cpp
/// Toast notification content.
struct ToastContent {
    std::string title;
    std::string body;
    std::string icon_path;  // Optional
    std::string action_label;
    std::function<void()> action_callback;
};

/// Show Windows Toast notification (lower-right corner, persistent).
void ShowToast(const ToastContent& content, uint32_t timeout_ms = 5000);

// Usage: Success/error notifications
ToastContent success{};
success.title = "Assets Imported";
success.body = "25 new textures imported successfully";
success.icon_path = "icons/check.png";
ShowToast(success, 3000);

ToastContent error{};
error.title = "Import Failed";
error.body = "Some assets could not be imported. See log for details.";
error.icon_path = "icons/error.png";
error.action_label = "View Log";
error.action_callback = []() { g_editor->ShowLogWindow(); };
ShowToast(error, 10000);
```

## Window Integration

```cpp
/// Set custom window title bar color (immersive dark mode).
void SetTitlebarColor(uint32_t rgb) {
    // Use DWMAPI DwmSetWindowAttribute with DWMWA_CAPTION_COLOR
    DwmSetWindowAttribute(m_hwnd,
        DWMWA_CAPTION_COLOR,
        &rgb, sizeof(rgb));
}

/// Set title bar background (matches theme).
void ApplySystemTheme() {
    if (IsDarkModeEnabled()) {
        // Dark mode: dark background, light text
        SetTitlebarColor(0x1E1E1E);  // Dark gray
        g_ui->SetWindowTitleColor(0xFFFFFF);  // White text
    } else {
        // Light mode: light background, dark text
        SetTitlebarColor(0xF3F3F3);  // Light gray
        g_ui->SetWindowTitleColor(0x000000);  // Black text
    }
}
```

## Power & Performance Integration

```cpp
/// Get current power mode from Windows.
enum class PowerMode {
    Normal,
    BatterySaver,
    HighPerformance,
};

PowerMode GetPowerMode() {
    // Query Windows power settings via API or registry
}

/// Adjust engine behavior based on power mode.
void OnPowerModeChanged(PowerMode mode) {
    switch (mode) {
        case PowerMode::BatterySaver:
            g_renderer->SetFrameRateLimit(30);  // Reduce battery drain
            break;
        case PowerMode::Normal:
            g_renderer->SetFrameRateLimit(60);
            break;
        case PowerMode::HighPerformance:
            g_renderer->SetFrameRateLimit(144);
            break;
    }
}
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Dark mode | Manual via UMG theme | Engine default | Auto-detect + `OnDarkModeChanged()` |
| Taskbar progress | `FWindowsPlatformMisc::SetTaskbarProgress()` | N/A | `SetTaskbarProgress()`, `SetTaskbarState()` |
| System tray | N/A (editor only) | N/A | `ShowTrayIcon()` + context menu |
| Toast notifications | Discord/Slack integration | N/A | Windows Toast API |
| Power mode | N/A | `OS.get_power_state()` | `GetPowerMode()` |
| Window integration | Immersive dark mode (UE5.4+) | N/A | `SetTitlebarColor()` + theme |

## Rules

1. **ALWAYS initialize dark mode at startup** — don't assume light theme
2. **ALWAYS listen for `WM_SETTINGCHANGE`** — dark mode can change mid-session
3. **NEVER use hardcoded colors** — respect user's system theme
4. **ALWAYS clear taskbar progress when operation completes** — leave it visible and user thinks it's still running
5. **ALWAYS handle toast action callbacks on main thread** — may be called from system thread
