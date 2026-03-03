## 1. VSCode Asset Browser Panel

- [ ] 1.1 `AssetBrowserProvider` calls `asset.list` IPC on activation
- [ ] 1.2 Groups assets by type: Scenes / Meshes / Textures / Materials / Shaders / Audio
- [ ] 1.3 Import status indicator: ok / stale / error
- [ ] 1.4 Context menu "Reimport" calls `asset.import` IPC
- [ ] 1.5 Context menu "New Material" calls `asset.createMaterial` IPC, opens file in editor
- [ ] 1.6 Context menu "New Scene" calls `asset.createScene` IPC, opens file in editor
- [ ] 1.7 Context menu "New Shader" calls `asset.createShader` IPC, opens file in editor
- [ ] 1.8 File watcher auto-refreshes tree on new files

## 2. Testing

- [ ] 2.1 `asset.list` integration test from VSCode panel
- [ ] 2.2 Drop a `.png` into project -- asset browser shows it as "stale"
- [ ] 2.3 "New Material" -- `.uzmat` created, opens in editor
- [ ] 2.4 Verify >= 95% coverage

## 3. Documentation

- [ ] 3.1 Update `docs/specs/editor-panels.md` with asset browser details
