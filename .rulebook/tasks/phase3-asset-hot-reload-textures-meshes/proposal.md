# Proposal: phase2-asset-hot-reload-textures-meshes

## Why
Texture and mesh changes currently require a full engine restart to take effect in the scene. An artist iterating on a material must: save the texture in their DCC tool, manually restart the engine, wait for it to initialize, navigate back to the scene, and check the result — a cycle that takes 10 or more seconds per iteration. This overhead multiplies across dozens of material tweaks per asset and makes visual polish work unnecessarily tedious.

## What Changes
- Implement a `FileWatcher` service in the engine that monitors all registered asset source directories using `ReadDirectoryChangesW` (Windows), firing change events when `.png`, `.jpg`, `.dds`, `.gltf`, `.fbx`, or `.uzmat` files are modified
- Add `ResourceManager::hot_reload(path)` which invalidates the in-memory cached resource for the given path, re-runs the import pipeline for that asset type, and returns the new `Handle<T>` — all scene nodes referencing the old handle are updated to point to the new resource via the Handle indirection layer
- Perform GPU resource updates in-place: for textures, upload new pixel data to a staging buffer and issue a `CopyTextureRegion` + resource barrier on the next frame boundary; for meshes, allocate a new vertex/index buffer and swap the scene node's binding before the next draw, then release the old buffer after N frames (fence-safe)
- Add an IPC command `asset.hot_reload` accepting a project-relative asset path, allowing the VSCode extension's Asset Browser to trigger a reload manually via context menu
- Add a VSCode extension setting `uzengine.autoReloadAssets` (default: true) that enables or disables the automatic file-watch reload; when disabled, a "Reload" button appears in the Asset Browser for manual triggering

## Impact
- Affected specs: docs/specs/asset-pipeline.md, docs/specs/renderer.md, docs/specs/memory-system.md
- Affected code: src/assets/ (FileWatcher, ResourceManager hot_reload), src/renderer/ (texture/mesh GPU update path), src/ipc/ (asset.hot_reload handler), vscode-extension/ (Asset Browser context menu, autoReloadAssets setting)
- Breaking change: NO
- User benefit: Texture and mesh changes appear in the running scene within one frame of saving, eliminating the restart cycle and cutting material iteration time from 10+ seconds to under 1 second
