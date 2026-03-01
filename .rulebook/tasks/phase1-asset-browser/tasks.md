## 1. Asset Registry (`src/assets/asset_registry.h/cpp`)

- [ ] 1.1 `AssetRegistry::scan(project_root)` — walk directory tree, discover assets by extension
- [ ] 1.2 Supported source types: `.glb/.gltf` (mesh), `.png/.jpg/.ktx2` (texture), `.wav/.ogg` (audio), `.uzscene`, `.uzmat`, `.hlsl`, `.glsl`
- [ ] 1.3 `AssetEntry` struct: `path`, `type`, `import_status` (fresh/stale/error)
- [ ] 1.4 Registry refreshes on file watcher events (uses FileWatcher from shader task)

## 2. Import Pipeline

- [ ] 2.1 Create `src/assets/importer.h/cpp` — `Importer::import(source_path)` → generates `.import.json`
- [ ] 2.2 `.import.json` schema: `{ source, type, settings, output, hash, imported_at }`
- [ ] 2.3 Import is skipped if `.import.json` exists and source file hash is unchanged
- [ ] 2.4 Mesh import (stub): copy `.glb` to `.import/` with settings recorded (full mesh processing in Phase 2)
- [ ] 2.5 Texture import: copy to `.import/`, record width/height/channels from PNG header
- [ ] 2.6 On import error: `.import.json` records `{ error: "..." }`, registry marks as error

## 3. IPC Handlers (asset.*)

- [ ] 3.1 Register `asset.list` — `{ type? }` → array of `{ path, type, status }`
- [ ] 3.2 Register `asset.import` — `{ path }` → triggers import, returns `{ ok, errors? }`
- [ ] 3.3 Register `asset.getInfo` — `{ path }` → returns `.import.json` contents
- [ ] 3.4 Register `asset.createMaterial` — `{ name, shader? }` → creates `.uzmat` from template
- [ ] 3.5 Register `asset.createScene` — `{ name }` → creates `.uzscene` from template
- [ ] 3.6 Register `asset.createShader` — `{ name, type? }` → creates `.hlsl` from template

## 4. Asset Templates

- [ ] 4.1 Create `assets/templates/material.uzmat.tpl` — PBR material template
- [ ] 4.2 Create `assets/templates/scene.uzscene.tpl` — empty scene with camera and directional light
- [ ] 4.3 Create `assets/templates/shader_unlit.hlsl.tpl` — minimal unlit vertex+pixel shader
- [ ] 4.4 Create `assets/templates/shader_pbr.hlsl.tpl` — PBR shader template with PBR structure

## 5. VSCode Asset Browser Panel

(This task delivers the panel from the editor-panels spec, focused on the asset side)

- [ ] 5.1 `AssetBrowserProvider` calls `asset.list` IPC on activation
- [ ] 5.2 Groups assets by type: Scenes / Meshes / Textures / Materials / Shaders / Audio
- [ ] 5.3 Import status indicator: ✓ (ok) / ⚠ (stale) / ✗ (error)
- [ ] 5.4 Context menu "Reimport" calls `asset.import` IPC
- [ ] 5.5 Context menu "New Material" calls `asset.createMaterial` IPC, opens file in editor
- [ ] 5.6 Context menu "New Scene" calls `asset.createScene` IPC, opens file in editor
- [ ] 5.7 Context menu "New Shader" calls `asset.createShader` IPC, opens file in editor
- [ ] 5.8 File watcher auto-refreshes tree on new files

## 6. Testing

- [ ] 6.1 `asset.list` returns all project assets categorized correctly
- [ ] 6.2 Drop a `.png` into project — asset browser shows it as "stale"
- [ ] 6.3 "Reimport" → import runs, status shows ✓
- [ ] 6.4 "New Material" → `.uzmat` created, opens in editor
- [ ] 6.5 Asset registry persists across engine restart (reads `.import.json`)
