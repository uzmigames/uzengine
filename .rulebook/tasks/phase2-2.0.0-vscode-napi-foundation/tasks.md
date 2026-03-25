## Phase A: Runtime C API Expansion (`runtime/engine/app/runtime.h/.cpp`)

- [ ] A.1 Add `uz_create_viewport(parent_hwnd, width, height) -> int` — creates DX12 device, swapchain, SceneRenderer, PipelineLibrary, ShaderManager internally. Returns viewport handle (1 on success, 0 on failure). `parent_hwnd` = 0 means create standalone window.
- [ ] A.2 Add `uz_destroy_viewport()` — flush GPU, destroy swapchain + device + all renderer resources. Safe to call if no viewport exists.
- [ ] A.3 Add `uz_resize_viewport(width, height)` — resize swapchain + GBuffer + render targets. Called on window resize events.
- [ ] A.4 Add `uz_set_viewport_position(x, y, width, height)` — `SetWindowPos()` on the native HWND. Extension calls this when WebView panel geometry changes.
- [ ] A.5 Add `uz_render_frame() -> uz_frame_stats` — renders one frame to the viewport. Returns struct with `fps`, `frame_ms`, `draw_calls`, `vertices`, `triangles`. Non-blocking if GPU is busy (skip frame).
- [ ] A.6 Add `uz_load_scene(path) -> int` — loads `.uzscene` via VFS + SceneLoader. Returns 1 success, 0 failure. Async internally (textures loaded on background thread).
- [ ] A.7 Add `uz_save_scene(path) -> int` — serializes current ECS World to `.uzscene` JSON.
- [ ] A.8 Add `uz_get_scene_tree() -> const char*` — returns JSON string of entity hierarchy: `[{id, name, children: [...]}]`. Caller must NOT free the string (internal static buffer, overwritten each call).
- [ ] A.9 Add `uz_create_entity(name) -> uint32_t` — creates entity with NameComponent, returns entity ID.
- [ ] A.10 Add `uz_destroy_entity(entity_id)` — removes entity + all components from World.
- [ ] A.11 Add `uz_get_component_json(entity_id, component_type) -> const char*` — returns JSON of component data. `component_type` is a string like `"transform"`, `"mesh"`, `"light"`, etc.
- [ ] A.12 Add `uz_set_component_property(entity_id, component_type, property_name, value_json) -> int` — sets a single property on a component. Value is JSON-encoded (`"1.5"`, `"[1,0,0]"`, `"\"path/to/tex.png\""`, etc).
- [ ] A.13 Add `uz_on_mouse_move(x, y)` — forward mouse position (viewport-local coordinates) to FPSCameraController / gizmo system.
- [ ] A.14 Add `uz_on_mouse_button(button, pressed)` — forward mouse button events (0=left, 1=right, 2=middle).
- [ ] A.15 Add `uz_on_key(keycode, pressed)` — forward keyboard events for WASD camera + shortcuts.
- [ ] A.16 Add `uz_on_scroll(delta)` — forward scroll wheel for camera speed / zoom.
- [ ] A.17 Add `uz_set_log_callback(void (*fn)(int level, const char* category, const char* message))` — registers a callback invoked on every `UZ_LOG()`. Extension pipes to Output channel. Pass NULL to unregister.
- [ ] A.18 Add `uz_capture_screenshot(uint8_t** out_data, uint32_t* out_size) -> int` — readback current frame as PNG into a caller-provided buffer. Returns 1 success, 0 failure. Caller must call `uz_free_screenshot(data)`.
- [ ] A.19 Add `uz_set_editor_mode(int enabled)` — toggles editor overlays (grid, stats, gizmos) from the extension side.
- [ ] A.20 Wire ALL new functions to the actual SceneRenderer, World, Camera, InputSystem, etc. Currently `uz_runtime_init()` just returns 1 — replace stubs with real implementation that creates the full rendering pipeline.

## Phase B: N-API Bridge (`napi/src/addon.cpp`)

- [ ] B.1 Add N-API exports for all Phase A functions. Sync wrappers for lightweight ops (input, resize, set property). Async/Promise wrappers for heavy ops (load scene, capture screenshot).
- [ ] B.2 Create TypeScript declaration file `extension/src/uzengine.d.ts` with full interface: `UzEngineAddon { createViewport(...), destroyViewport(), renderFrame(), loadScene(path), getSceneTree(), ... }`.
- [ ] B.3 Implement typed array transfer for screenshot buffer — `napi_create_external_buffer` with custom Release callback to avoid copy.
- [ ] B.4 Implement log callback registration — `napi_create_threadsafe_function` so runtime's render thread can invoke JS callback on the main thread safely.
- [ ] B.5 Implement error propagation — runtime failures become `napi_throw_error` / Promise rejections with descriptive messages.
- [ ] B.6 Update `napi/CMakeLists.txt` if additional runtime link dependencies are needed.
- [ ] B.7 Verify Electron ABI compatibility — `--runtime=electron --runtime-version=39.6.0` (VSCode 1.111.0, Node 22.22.0, ABI 140).

## Phase C: Extension TypeScript (`extension/src/`)

- [ ] C.1 Refactor `extension.ts` — replace hardcoded `require('./uzengine.node')` with typed import from `uzengine.d.ts`. Register all new commands.
- [ ] C.2 Create `panels/viewport-panel.ts` — WebViewPanel that: (a) creates HTML with toolbar + #viewport-container div, (b) calls `addon.createViewport()` with panel geometry, (c) starts render loop via `setInterval` calling `addon.renderFrame()`, (d) forwards input events via `onDidReceiveMessage` → `addon.onMouseMove/onKey/etc`.
- [ ] C.3 Create `panels/scene-tree-panel.ts` — TreeView provider that calls `addon.getSceneTree()` and renders entity hierarchy. Click entity → select in viewport.
- [ ] C.4 Create `panels/console-panel.ts` — OutputChannel that receives log messages from `addon.setLogCallback()` and formats them with level/category coloring.
- [ ] C.5 Create `engine-mode.ts` — status bar toggle button (`$(game) Code Mode` / `Engine Mode`). `Ctrl+Shift+E` switches layout: Engine Mode shows viewport center + hierarchy left + inspector right + console bottom.
- [ ] C.6 Register file association: `.uzscene` files open with `uzengine.openScene` command → loads scene + opens viewport panel.
- [ ] C.7 Register `uzengine.openViewport`, `uzengine.openScene`, `uzengine.toggleEngineMode` commands in `package.json` `contributes.commands`.
- [ ] C.8 Add activation events: `onCommand:uzengine.*`, `onLanguage:uzscene`.
- [ ] C.9 Implement viewport geometry tracking — `onDidChangeViewState` to detect panel move/resize → call `addon.setViewportPosition()`.

## Phase D: Build Pipeline

- [ ] D.1 Verify root `npm run build` works end-to-end: `zig build` (runtime .lib) → `cmake-js` (napi .node with MSVC) → `cp .node extension/native/` → `tsc` (extension) → VSIX package.
- [ ] D.2 Add `preBuild` task in `.vscode/tasks.json` that calls `uz_pool_shutdown()` (or kills runtime processes) before recompile.
- [ ] D.3 Test full cycle: edit C++ → build → reload extension → viewport still works.

## Testing

- [ ] T.1 C API unit test: `uz_create_viewport(0, 800, 600)` succeeds, `uz_render_frame()` returns non-zero fps, `uz_destroy_viewport()` doesn't crash.
- [ ] T.2 C API unit test: `uz_load_scene("scenes/pbr_showcase.uzscene")` loads entities, `uz_get_scene_tree()` returns valid JSON with entity names.
- [ ] T.3 C API unit test: `uz_create_entity("TestCube")` returns valid ID, `uz_get_component_json(id, "transform")` returns JSON with position/rotation/scale.
- [ ] T.4 C API unit test: `uz_set_component_property(id, "transform", "position", "[1,2,3]")` succeeds, `uz_get_component_json` reflects the change.
- [ ] T.5 C API unit test: `uz_set_log_callback(fn)` receives at least one log message during `uz_create_viewport`.
- [ ] T.6 C API unit test: `uz_capture_screenshot()` returns non-null PNG data with valid header (89 50 4E 47).
- [ ] T.7 N-API test: `require('uzengine.node')` in Node.js (not Electron) — verify all exports exist and have correct types.
- [ ] T.8 Extension test: activate extension → run `uzengine.openViewport` → verify viewport panel opens without errors.
- [ ] T.9 Extension test: load `.uzscene` → verify scene tree populates with entity names.
- [ ] T.10 Stress test: create/destroy viewport 100 times → no memory leaks (D3D12MA assertion clean).
- [ ] T.11 Input test: forward 1000 mouse move events → no crash, camera position changes.

## Documentation

- [ ] D.1 Update `docs/specs/2.0-napi-foundation.md` deliverables table — mark expanded C API, N-API bridge, extension scaffold as complete.
- [ ] D.2 Update `docs/engine/28-ipc-protocol/02-napi-bridge.md` with actual function signatures and usage examples.
- [ ] D.3 Create `docs/guides/extension-development.md` — how to build/test/debug the VSCode extension locally.
- [ ] D.4 Update `extension/README.md` with setup instructions, commands, and architecture overview.
- [ ] D.5 Update root `README.md` with VSCode extension build instructions.
