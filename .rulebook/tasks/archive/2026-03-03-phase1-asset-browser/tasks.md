## 1. Asset Registry (`src/engine/editor/asset_registry.h/cpp`)

- [x] 1.1 `AssetRegistry::scan(project_root)` -- walk directory tree, discover assets by extension
- [x] 1.2 Supported source types: `.glb/.gltf` (mesh), `.png/.jpg/.ktx2` (texture), `.wav/.ogg` (audio), `.uzscene`, `.uzmat`, `.hlsl`, `.glsl`
- [x] 1.3 `AssetEntry` struct: `path`, `type`, `import_status` (fresh/stale/error)
- [x] 1.4 Registry refreshes on file watcher events (uses FileWatcher from shader task)

## 2. Import Pipeline

- [ ] D1. 2.1 Create `src/assets/importer.h/cpp` -- `Importer::import(source_path)` (import integrated into AssetRegistry for now)
- [x] 2.2 `.import.json` schema: `{ source, type, hash, imported_at }`
- [x] 2.3 Import is skipped if `.import.json` exists and source file hash is unchanged
- [ ] D2. 2.4 Mesh import (stub): copy `.glb` to `.import/` with settings recorded (full mesh processing in Phase 2)
- [ ] D3. 2.5 Texture import: copy to `.import/`, record width/height/channels from PNG header
- [x] 2.6 On import error: `.import.json` records `{ error: "..." }`, registry marks as error

## 3. IPC Handlers (asset.*)

- [x] 3.1 Register `asset.list` -- `{ type? }` -> array of `{ path, type, status }`
- [x] 3.2 Register `asset.import` -- `{ path }` -> triggers import, returns `{ ok, errors? }`
- [x] 3.3 Register `asset.getInfo` -- `{ path }` -> returns `.import.json` contents
- [x] 3.4 Register `asset.createMaterial` -- `{ name, shader? }` -> creates `.uzmat` from template
- [x] 3.5 Register `asset.createScene` -- `{ name }` -> creates `.uzscene` from template
- [x] 3.6 Register `asset.createShader` -- `{ name, type? }` -> creates `.hlsl` from template

## 4. Asset Templates

- [x] 4.1 Create `assets/templates/material.uzmat` -- PBR material template
- [x] 4.2 Create `assets/templates/scene.uzscene` -- empty scene with camera and directional light
- [x] 4.3 Create `assets/templates/shader_unlit.hlsl` -- minimal unlit vertex+pixel shader
- [ ] D4. 4.4 Create `assets/templates/shader_pbr.hlsl.tpl` -- PBR shader template with PBR structure

## 5. VSCode Asset Browser Panel

(This task delivers the panel from the editor-panels spec, focused on the asset side)

- [ ] D5. 5.1 `AssetBrowserProvider` calls `asset.list` IPC on activation
- [ ] D5. 5.2 Groups assets by type: Scenes / Meshes / Textures / Materials / Shaders / Audio
- [ ] D5. 5.3 Import status indicator: ok / stale / error
- [ ] D5. 5.4 Context menu "Reimport" calls `asset.import` IPC
- [ ] D5. 5.5 Context menu "New Material" calls `asset.createMaterial` IPC, opens file in editor
- [ ] D5. 5.6 Context menu "New Scene" calls `asset.createScene` IPC, opens file in editor
- [ ] D5. 5.7 Context menu "New Shader" calls `asset.createShader` IPC, opens file in editor
- [ ] D5. 5.8 File watcher auto-refreshes tree on new files

## 6. Testing

- [x] 6.1 `asset.list` returns all project assets categorized correctly
- [ ] D5. 6.2 Drop a `.png` into project -- asset browser shows it as "stale" (requires VSCode panel)
- [x] 6.3 "Reimport" -> import runs, status shows Fresh
- [ ] D5. 6.4 "New Material" -> `.uzmat` created, opens in editor (requires VSCode panel)
- [x] 6.5 Asset registry persists across engine restart (reads `.import.json`)
