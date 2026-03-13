# Proposal: Clipboard & Drag-Drop (Phase 4.4.0)

## Why
The editor needs clipboard (copy/paste of entities, properties, shader snippets) and drag-drop (drag assets from browser into viewport, drag files from Explorer into engine). Without this, the editor workflow requires manual path entry for every asset. UE5 has full OLE drag-drop; Godot supports `WM_DROPFILES` + custom DnD within editor. This is P2 but required before the editor milestone.

## What Changes

### New Systems
1. **Clipboard API** — `SetClipboardText(string)`, `GetClipboardText()`, `SetClipboardData(format, bytes)`, `GetClipboardData(format)`
2. **OLE IDropTarget** — COM-based drag-drop with `DragEnter`/`DragOver`/`DragLeave`/`Drop` handlers
3. **File drop fallback** — `WM_DROPFILES` for simple file drops (non-OLE apps)
4. **Drop event callbacks** — `OnFilesDropped(paths, position)`, `OnDragEnter/Over/Leave` for visual feedback

### Refactoring
- `CoInitializeEx` call moved to platform init (required for OLE)

## Impact
- Affected specs: `docs/engine/07-platform/01-window-system.md`
- Affected code: `runtime/platform/windows/platform_window.h/.cpp`
- Breaking change: NO — additive
- User benefit: Copy/paste in editor, drag files from Explorer into viewport
- Enables: Asset browser drag-drop, entity copy/paste, editor productivity
