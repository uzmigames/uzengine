## 1. Implementation
- [x] 1.1 Implement `ClipboardSetText(string)` and `ClipboardGetText()` via Win32 clipboard API <!-- clipboard.h/.cpp -->
- [x] 1.2 Implement `ClipboardSetData(format, bytes)` and `ClipboardGetData(format)` for binary data
- [x] 1.3 Implement OLE `IDropTarget` COM class for the engine window <!-- drag_drop.h/.cpp, EngineDropTarget -->
- [x] 1.4 Register the window as a drop target via `RegisterDragDrop(hwnd, dropTarget)` <!-- EnableDragDrop() -->
- [x] 1.5 Implement `DragEnter`/`DragOver`/`DragLeave`/`Drop` handlers extracting file paths from `IDataObject`
- [x] 1.6 Add `WM_DROPFILES` fallback handler for non-OLE file drops <!-- platform_window.cpp -->
- [x] 1.7 Add `SetFileDropCallback(paths, mousePosition)` on PlatformWindow <!-- platform_window.h -->
- [x] 1.8 Add `OnDragEnter`/`OnDragOver`/`OnDragLeave` callbacks via SetDragHoverCallbacks <!-- drag_drop.h -->
- [x] 1.9 OLE initialized via `OleInitialize` in EnableDragDrop <!-- drag_drop.cpp -->

## 2. Testing
- [x] 2.1 Test clipboard text round-trip (set → get → compare)
- [x] 2.2 Test clipboard binary data round-trip with custom format
- [x] 2.3 Test `ClipboardGetText()` returns empty string when clipboard has no text
- [x] 2.4 Test OLE drop target lifecycle (enable/disable)
- [x] 2.5 Test `WM_DROPFILES` fallback path extraction
- [ ] 2.6 Verify 95%+ coverage on all new code
- [x] 2.7 Build succeeds (zig build — 0 errors)

## 3. Documentation
- [x] 3.1 Create `docs/engine/07-platform/05-clipboard-drag-drop.md`
