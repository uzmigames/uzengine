# Platform — Native Dialogs & OS Integration

> **Status**: Planned (Phase 4.9.0)
> **Source**: `platform/windows/`
> **Depends on**: COM `IFileDialog`, `TaskDialogIndirect`, `ChooseColor`, Windows Toast API

## Purpose

The editor needs native OS dialogs for file open/save (asset import), message boxes (confirmations), and color picker (material editing). These integrate the editor seamlessly with the OS — users expect Ctrl+S to open a Save dialog that looks familiar, not a custom widget. UzEngine uses Win32 COM interfaces for native appearance and reliability.

## Architecture

```
PlatformWindow Native Dialogs
    │
    ├── File Dialogs
    │   ├── OpenFileDialog()      → IFileDialog
    │   └── SaveFileDialog()
    │
    ├── Message Boxes
    │   ├── ShowMessageBox()      → TaskDialogIndirect
    │   └── With custom buttons/icons
    │
    ├── Color Picker
    │   └── ShowColorPicker()     → ChooseColor
    │
    ├── Dark Mode Detection
    │   └── IsDarkModeEnabled()   → Registry + WM_SETTINGCHANGE
    │
    ├── Taskbar Progress
    │   └── SetTaskbarProgress()  → ITaskbarList3
    │
    ├── System Tray
    │   └── ShowTrayIcon()        → Shell_NotifyIcon
    │
    └── Toast Notifications
        └── ShowNotification()    → Windows Toast API
```

## File Dialogs

```cpp
/// Filter for file dialog (name + patterns).
struct FileFilter {
    std::string name;           // "Image Files"
    std::string patterns;       // "*.png;*.jpg;*.bmp"
};

/// Open file dialog with filters and multi-select.
std::vector<std::string> OpenFileDialog(
    const std::vector<FileFilter>& filters,
    bool multi_select = false);

/// Save file dialog.
std::string SaveFileDialog(
    const std::vector<FileFilter>& filters,
    const std::string& default_name = "");

// Usage
std::vector<FileFilter> filters = {
    {"Image Files", "*.png;*.jpg;*.bmp"},
    {"All Files", "*.*"}
};

auto files = OpenFileDialog(filters, true);
for (const auto& path : files) {
    UZ_LOG(Editor, Info, "Selected: {}", path);
    g_asset_manager->ImportAsset(path);
}
```

## Message Boxes

```cpp
/// Message box buttons and result.
enum class MessageBoxButton {
    Ok, Yes, No, Cancel, Retry, Ignore
};

enum class MessageBoxIcon {
    None, Info, Warning, Error, Question
};

/// Show native message box (blocking).
MessageBoxButton ShowMessageBox(
    const std::string& title,
    const std::string& message,
    const std::vector<MessageBoxButton>& buttons = {MessageBoxButton::Ok},
    MessageBoxIcon icon = MessageBoxIcon::None);

// Usage: Unsaved changes confirmation
auto result = ShowMessageBox(
    "Unsaved Changes",
    "Save changes before closing?",
    {MessageBoxButton::Yes, MessageBoxButton::No, MessageBoxButton::Cancel},
    MessageBoxIcon::Question);

if (result == MessageBoxButton::Yes) {
    g_editor->SaveScene();
} else if (result == MessageBoxButton::Cancel) {
    return;  // Abort close
}
```

## Color Picker

```cpp
/// Choose color from system picker.
/// Returns {r, g, b, a} in 0-255 range.
/// Returns {0, 0, 0, 255} if cancelled.
struct Color {
    uint8_t r, g, b, a;
};

Color ShowColorPicker(Color initial = {255, 255, 255, 255});

// Usage
Color current = g_editor->GetSelectedMaterialColor();
Color picked = ShowColorPicker(current);
if (picked.r != 0 || picked.g != 0 || picked.b != 0) {  // Not cancelled
    g_editor->SetMaterialColor(picked);
}
```

## Dark Mode Detection

```cpp
/// Check if Windows dark mode is enabled globally.
bool IsDarkModeEnabled();

/// Callback when dark mode setting changes.
using DarkModeChangedCallback = std::function<void(bool dark_mode)>;

/// Listen for dark mode changes (WM_SETTINGCHANGE).
void OnDarkModeChanged(DarkModeChangedCallback callback);

// Usage
if (IsDarkModeEnabled()) {
    g_ui->LoadTheme("dark");
} else {
    g_ui->LoadTheme("light");
}

OnDarkModeChanged([](bool dark_mode) {
    UZ_LOG(UI, Info, "Dark mode: {}", dark_mode);
    g_ui->LoadTheme(dark_mode ? "dark" : "light");
});
```

## Taskbar Progress

```cpp
/// Update taskbar progress indicator (progress bar on taskbar icon).
void SetTaskbarProgress(uint32_t current, uint32_t total);

/// Clear taskbar progress.
void ClearTaskbarProgress();

// Usage: Baking lightmaps
uint32_t total_lightmaps = 42;
for (uint32_t i = 0; i < total_lightmaps; ++i) {
    BakeLightmap(i);
    SetTaskbarProgress(i + 1, total_lightmaps);  // Update progress bar
}
ClearTaskbarProgress();
```

## System Tray Icon

```cpp
/// Show system tray icon with tooltip.
void ShowTrayIcon(const char* tooltip, const char* icon_path);

/// Hide system tray icon.
void HideTrayIcon();

/// Set tray icon callback (left-click, right-click).
using TrayIconCallback = std::function<void(int x, int y)>;
void OnTrayIconClicked(TrayIconCallback callback);

// Usage: Minimize to tray
void OnWindowMinimized() {
    PlatformWindow::HideTitleBar();
    ShowTrayIcon("UzEngine Editor", "icons/logo.png");
}

OnTrayIconClicked([](int x, int y) {
    // Restore window on click
    g_window->Show();
    g_window->ShowTitleBar();
    HideTrayIcon();
});
```

## Toast Notifications

```cpp
/// Show Windows Toast notification (lower-right corner).
void ShowNotification(
    const std::string& title,
    const std::string& body,
    const char* icon_path = nullptr,
    uint32_t timeout_ms = 5000);

// Usage: Asset import completion
void OnAssetImportComplete(const std::string& asset_name) {
    ShowNotification(
        "Asset Imported",
        asset_name,
        "icons/check.png",
        3000);
}
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| File dialog | `FDesktopPlatformModule` + Win32 | `FileDialog` node | `OpenFileDialog()`, `SaveFileDialog()` |
| Message box | `FDesktopPlatformModule::MessageBox()` | `AcceptDialog`, `ConfirmationDialog` | `ShowMessageBox()` |
| Color picker | `SColorGradient` widget | `ColorPickerButton` node | `ShowColorPicker()` |
| Dark mode | Manual UI theming | Automatic via `theme_color_dict` | `IsDarkModeEnabled()` + callback |
| Taskbar progress | `FWindowsPlatformMisc::SetTaskbarProgress()` | N/A | `SetTaskbarProgress()` |
| System tray | N/A | N/A | `ShowTrayIcon()` |
| Toast | N/A | N/A | `ShowNotification()` |

## Rules

1. **ALWAYS use native dialogs** — custom dialogs feel unpolished and confuse users
2. **NEVER block the render thread on file dialogs** — show in separate thread or defer
3. **ALWAYS respect dark mode** — load appropriate theme on startup and listen for changes
4. **NEVER hardcode timeout for notifications** — let OS dismiss after duration
5. **ALWAYS handle file dialog cancellation** — user can close without selecting
