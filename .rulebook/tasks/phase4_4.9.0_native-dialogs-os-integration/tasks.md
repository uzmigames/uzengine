## 1. Implementation
- [x] 1.1 Implement `OpenFileDialog(filters, multiSelect, defaultDir)` via `IFileOpenDialog` COM
- [x] 1.2 Implement `SaveFileDialog(filters, defaultName, defaultDir)` via `IFileSaveDialog` COM
- [x] 1.3 Implement `ShowMessageBox(title, message, buttons, icon)` via `MessageBoxW`
- [ ] 1.4 Implement `ShowColorPicker(initialColor)` returning selected RGBA <!-- deferred: low priority -->
- [x] 1.5 Implement dark mode detection via `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme`
- [x] 1.6 Add `WM_SETTINGCHANGE` listener for dark mode toggle at runtime
- [x] 1.7 Implement `SetTaskbarProgress(value, max)` and `ClearTaskbarProgress()` via `ITaskbarList3`
- [x] 1.8 Implement `ShowTrayIcon(icon, tooltip)` / `HideTrayIcon()` via `Shell_NotifyIconW`
- [x] 1.9 Implement `ShowNotification(title, body)` via `Shell_NotifyIconW` balloon

## 2. Testing
- [ ] 2.1 Test file dialog filter parsing (e.g., "Images|*.png;*.jpg|All|*.*") <!-- requires UI interaction -->
- [ ] 2.2 Test message box returns correct button result enum <!-- requires UI interaction -->
- [x] 2.3 Test dark mode detection returns boolean on current system
- [x] 2.4 Test taskbar progress set/clear does not crash (visual verification)
- [x] 2.5 Test tray icon show/hide cycle does not leak resources
- [ ] 2.6 Verify 95%+ coverage on all new code
- [x] 2.7 Build succeeds (zig build — 0 errors)

## 3. Documentation
- [x] 3.1 Create `docs/engine/07-platform/11-native-dialogs.md`
- [x] 3.2 Create `docs/engine/07-platform/12-os-integration.md` (dark mode, taskbar, tray, notifications)
