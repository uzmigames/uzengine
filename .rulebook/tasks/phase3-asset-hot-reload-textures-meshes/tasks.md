## 1. FileWatcher Service

- [ ] 1.1 Implement `FileWatcher` service in `src/engine/core/file_watcher.cpp` using `ReadDirectoryChangesW` (already stubbed in `src/engine/core/file_watcher.h`) to monitor registered directories recursively; fire change events for file extensions: `.png`, `.jpg`, `.dds`, `.gltf`, `.fbx`, `.uzmat`
- [ ] 1.2 Add `FileWatcher::Watch(directory_path)` and `FileWatcher::Unwatch(directory_path)` methods; maintain a list of watched directories and their associated `HANDLE` objects
- [ ] 1.3 Implement `FileWatcher::Poll()` method called once per frame from the engine main loop that drains the `ReadDirectoryChangesW` completion buffer and fires registered `OnFileChanged(path)` callbacks
- [ ] 1.4 Add debounce logic in `FileWatcher`: suppress duplicate events for the same path within a 100ms window using a `std::unordered_map<std::string, std::chrono::steady_clock::time_point>` timestamp map, preventing double-reload from DCC tools that write files in two phases
- [ ] 1.5 Register the engine's asset source directories from the `.uzproj` `[assets.source_dirs]` array with `FileWatcher` at startup

## 2. ResourceManager Hot Reload

- [ ] 2.1 Add `ResourceManager::hot_reload(path)` in `src/engine/assets/resource_manager.cpp` that looks up the cached resource entry for the given path, runs the appropriate import pipeline for the asset type (texture or mesh), and returns a new `Handle<T>` pointing to the updated resource
- [ ] 2.2 Implement Handle indirection: `Handle<T>` stores an index into a `ResourceSlot[]` array; `hot_reload` updates the slot's internal pointer so all existing handles automatically point to the new resource without requiring callers to be notified
- [ ] 2.3 Implement GPU texture upload path in `src/engine/renderer/resource_uploader.cpp`: on texture hot-reload, upload new pixel data to a staging buffer, issue `CopyTextureRegion`, and insert a `D3D12_RESOURCE_BARRIER` transition from `COPY_DEST` to `PIXEL_SHADER_RESOURCE` before the next frame's draw commands
- [ ] 2.4 Implement GPU mesh update path: on mesh hot-reload, allocate new vertex/index buffers, copy the scene node's draw binding to point to the new buffers, then schedule the old buffers for deferred release after `FRAMES_IN_FLIGHT` frames using a fence-tracked deferred deletion queue

## 3. Auto Hot-Reload Pipeline

- [ ] 3.1 Subscribe `ResourceManager::hot_reload` to `FileWatcher::OnFileChanged` in the engine startup code; dispatch the reload call on the render thread (not the file watcher's background thread) to avoid GPU command list thread safety violations
- [ ] 3.2 Log each auto hot-reload with `UZ_LOG_INFO("Hot-reloaded asset: %s (%.1f ms)", path, reload_ms)` measuring the round-trip import + GPU upload time

## 4. IPC Command

- [ ] 4.1 Add `asset.hot_reload` IPC handler in `src/ipc/ipc_handlers.cpp` accepting `{"path": "textures/rock_diffuse.png"}` (project-relative path); call `ResourceManager::hot_reload()` and return `{"ok": true, "reload_ms": 12.4}` or `{"ok": false, "error": "..."}`
- [ ] 4.2 Validate the path in the IPC handler: reject paths containing `..` components, paths outside the project directory, and paths with unsupported extensions; return a descriptive error in each case

## 5. VSCode Extension Integration

- [ ] 5.1 Add a "Reload Asset" context menu item to the Asset Browser tree view in `vscode-uzengine/src/panels/AssetBrowserPanel.ts` that calls `asset.hot_reload` with the selected file's project-relative path
- [ ] 5.2 Add `uzengine.autoReloadAssets` setting (boolean, default `true`) to `vscode-uzengine/package.json`; when `false`, disable `FileWatcher` directory monitoring and show a "Reload" button next to each asset in the Asset Browser
- [ ] 5.3 Show a VSCode status bar notification after a successful `asset.hot_reload` IPC call displaying the asset filename and reload time in milliseconds; auto-hide after 3 seconds

## 6. Testing

- [ ] 6.1 Create `tests/core/test_file_watcher.cpp` (already exists — extend it) with test cases for the debounce logic: write the same path twice within 50ms and verify only one event is fired; write twice 200ms apart and verify two events are fired
- [ ] 6.2 Test `FileWatcher::Watch()` on a non-existent directory logs an error and returns `false`; on a valid directory returns `true` and begins monitoring
- [ ] 6.3 Test `FileWatcher::Unwatch()` stops events from firing for a previously watched directory
- [ ] 6.4 Create `tests/assets/test_resource_manager_hot_reload.cpp` testing that `hot_reload(path)` on an existing cached texture updates the handle's internal pointer to the new resource
- [ ] 6.5 Test that all existing `Handle<Texture>` instances point to the new resource after `hot_reload` without any explicit invalidation by callers
- [ ] 6.6 Test `hot_reload` for a mesh asset allocates new GPU buffers and schedules the old ones for deferred deletion; verify the old buffers are released after `FRAMES_IN_FLIGHT` fence signals
- [ ] 6.7 Test `asset.hot_reload` IPC handler rejects a path with `..` components and returns `ok: false` with a path traversal error
- [ ] 6.8 Test `asset.hot_reload` IPC handler rejects a path with an unsupported extension (e.g. `.exe`) and returns `ok: false`
- [ ] 6.9 Test `asset.hot_reload` IPC handler for a valid path returns `ok: true` and a non-zero `reload_ms` value
- [ ] 6.10 Test `uzengine.autoReloadAssets = false` disables the `FileWatcher` and no auto-reload events are dispatched when a file changes
- [ ] 6.11 Run coverage -- all new files in `src/engine/core/`, `src/engine/assets/`, and `src/engine/renderer/` must reach >= 95% line and branch coverage before marking complete
