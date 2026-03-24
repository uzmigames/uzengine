# Proposal: Native Dialogs & OS Integration (Phase 4.9.0)

## Why
The editor needs file open/save dialogs for asset import, message boxes for confirmations, taskbar progress for long operations (baking lightmaps, cooking assets), and dark mode detection for UI theming. Godot implements all of these (IFileDialog, ITaskbarList3, dark mode via registry); UE5 has file dialogs and message boxes. This is P3 but significantly improves the editor UX.

## What Changes

### New Systems
1. **File dialog** — `OpenFileDialog(filters, multiSelect)`, `SaveFileDialog(filters, defaultName)` via COM `IFileDialog`
2. **Message box** — `ShowMessageBox(title, message, buttons, icon)` via `TaskDialogIndirect` (modern look)
3. **Color picker** — `ShowColorPicker(initialColor)` via `ChooseColor`
4. **Dark mode detection** — registry check + `WM_SETTINGCHANGE` listener
5. **Taskbar progress** — `SetTaskbarProgress(value, max)` via `ITaskbarList3`
6. **System tray icon** — `ShowTrayIcon(icon, tooltip)` / `HideTrayIcon()` via `Shell_NotifyIcon`
7. **Toast notifications** — `ShowNotification(title, body)` via Windows Toast API

## Impact
- Affected specs: `docs/engine/07-platform/`
- Affected code: `runtime/platform/windows/`
- Breaking change: NO — purely additive
- User benefit: Native OS integration for editor workflows
- Enables: Editor asset import, progress feedback, dark mode theming
