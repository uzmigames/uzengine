## 1. VSCode Asset Browser Panel

- [x] 1.1 `AssetBrowserProvider` calls `asset.list` IPC on activation (`asset-browser-provider.ts:refresh()`)
- [x] 1.2 Groups assets by type: Scenes / Meshes / Textures / Materials / Shaders / Audio
- [x] 1.3 Import status indicator: Fresh(check) / Stale(warning) / Error(error) / NotImported(circle)
- [x] 1.4 Context menu "Reimport" calls `asset.import` IPC (`reimportAsset()`)
- [x] 1.5 Context menu "New Material" calls `asset.createMaterial` IPC, opens file in editor
- [x] 1.6 Context menu "New Scene" calls `asset.createScene` IPC, opens file in editor
- [x] 1.7 Context menu "New Shader" calls `asset.createShader` IPC, opens file in editor
- [x] 1.8 File watcher auto-refreshes tree on `onActiveChanged` (IPC reconnect)

## 2. Testing

- [x] 2.1 `asset.list` integration test from VSCode panel (60 tests in asset-browser-provider.test.ts)
- [x] 2.2 AssetItem shows import status (Fresh/Stale/Error/NotImported) with correct icons
- [x] 2.3 "New Material" -- IPC called, path returned, file opened in editor (tested)
- [x] 2.4 Verify >= 95% coverage (100% stmts, 96.15% branch, 100% funcs, 100% lines)

## 3. Documentation

- [ ] 3.1 Update `docs/specs/editor-panels.md` with asset browser details

## 4. C++ Engine Wiring

- [x] 4.1 Wire `asset_register_handlers()` in `main.cpp` (creates AssetRegistry, scans project root)
- [x] 4.2 All 6 asset.* IPC handlers now active: list, import, getInfo, createMaterial, createScene, createShader

## 5. Drag-and-Drop Integration

- [x] 5.1 `AssetBrowserDragController` serializes `AssetDragPayload` into `MIME_ASSET` DataTransfer
- [x] 5.2 Coordinator `startDrag()` called when dragging from Asset Browser
- [x] 5.3 Registered as `dragAndDropController` on the tree view
