## Phase A: Runtime C API Expansion (`runtime/engine/app/runtime.h/.cpp`)

- [x] A.1 Add `uz_create_viewport(parent_hwnd, width, height) -> int` — Full DX12 pipeline: device, swapchain, cmdlist, fences, SceneRenderer, World, VFS, Lua. Parent HWND support via SetParent reparenting.
- [x] A.2 Add `uz_destroy_viewport()` — Reverse-order teardown: Lua, SceneRenderer, fences, cmdlist, swapchain, device. Safe no-op if no viewport.
- [x] A.3 Add `uz_resize_viewport(width, height)` — GPU idle wait, swapchain resize, SceneRenderer resize, screenshot capture reinit.
- [x] A.4 Add `uz_set_viewport_position(x, y, width, height)` — SetWindowPos on HWND + implicit resize if dimensions changed.
- [x] A.5 Add `uz_render_frame() -> uz_frame_stats` — Full frame loop: message pump, fence wait, timing, Lua tick, SceneRenderer::RenderFrame, Present, fence signal. Returns fps/frame_ms/frame_index.
- [x] A.6 Add `uz_load_scene(path) -> int` — VFS read + LoadScene into World + Lua re-bind.
- [x] A.7 Add `uz_save_scene(path) -> int` — SaveScene serialization + fwrite to disk.
- [x] A.8 Add `uz_get_scene_tree() -> const char*` — Iterates CName components, builds JSON with id/name/children. Static buffer valid until next call.
- [x] A.9 Add `uz_create_entity(name) -> uint32_t` — World::CreateEntity + AddComponent<CName>.
- [x] A.10 Add `uz_destroy_entity(entity_id)` — EntityFromEntt reconstruction + World::DestroyEntity.
- [x] A.11 Add `uz_get_component_json(entity_id, component_type) -> const char*` — Stub returning "{}". Full implementation deferred to Phase B (requires property reflection system).
- [x] A.12 Add `uz_set_component_property(entity_id, component_type, property_name, value_json) -> int` — Stub returning 0. Full implementation deferred to Phase B (requires property reflection system).
- [x] A.13 Add `uz_on_mouse_move(x, y)` — Forwards to InputSystem::OnMouseMove via SceneRenderer::GetInput().
- [x] A.14 Add `uz_on_mouse_button(button, pressed)` — Maps 0/1/2 to MouseButton enum, forwards to InputSystem::OnMouseButton.
- [x] A.15 Add `uz_on_key(keycode, pressed)` — Forwards VK codes to InputSystem::OnKeyDown/OnKeyUp.
- [x] A.16 Add `uz_on_scroll(delta)` — Forwards to InputSystem::OnMouseScroll.
- [x] A.17 Add `uz_set_log_callback(fn)` — Custom spdlog CallbackSink that maps spdlog levels to 0-5 and forwards category+message.
- [x] A.18 Add `uz_capture_screenshot(uint8_t** out_data, uint32_t* out_size) -> int` — CaptureFromPresent + WaitIdle + ToBase64PNG + base64 decode to raw PNG bytes. Caller frees via uz_free_buffer().
- [x] A.19 Add `uz_set_editor_mode(int enabled)` — Delegates to SceneRenderer::SetEditorMode.
- [x] A.20 Wire ALL new functions to actual engine objects. RuntimeState struct owns device/swapchain/cmdList/fences/World/VFS/SceneRenderer/LuaVM. Init sequence mirrors RunWindowed() from uzengine.cpp. Also added uz_set_content_root() and uz_free_buffer().

## Phase B: N-API Bridge (`napi/src/`)

- [x] B.1 N-API exports for all Phase A functions. Split into 6 files: `addon.cpp` (entry), `napi_lifecycle.cpp`, `napi_viewport.cpp`, `napi_scene.cpp`, `napi_input.cpp`, `napi_diagnostics.cpp`. Common header: `napi_common.h`.
- [x] B.2 TypeScript declaration file `extension/src/uzengine.d.ts`: `FrameStats` interface, `LogLevel` enum, `UzEngineAddon` interface with all 25 exports fully typed.
- [x] B.3 Screenshot buffer via `Napi::Buffer<uint8_t>::Copy` — zero-copy transfer to Node, C buffer freed immediately via `uz_free_buffer`.
- [x] B.4 Log callback via `Napi::ThreadSafeFunction` — runtime threads safely marshal log messages to JS event loop. String capture by value to avoid dangling C buffers.
- [x] B.5 Error propagation via `Napi::Error::New().ThrowAsJavaScriptException()` + `Napi::TypeError` for argument validation. No C++ exceptions (`NAPI_DISABLE_CPP_EXCEPTIONS`).
- [x] B.6 CMakeLists.txt updated: links `uzengine_runtime.lib` (DLL import lib), defines `UZ_USING_DLL`, copies runtime DLLs + D3D12 Agility SDK post-build.
- [x] B.7 Electron ABI verified: `--runtime=electron --runtime-version=39.6.0`. Build succeeds with MSVC cl.exe + MSVC link.exe. Output: `uzengine.node` (162KB).

## Phase C: Extension TypeScript (`extension/src/`)

- [x] C.1 Refactored `extension.ts` — typed import via `uzengine.d.ts`, registers 5 commands (start, openViewport, openScene, toggleEngineMode, refreshSceneTree). Manages viewport/sceneTree/console/engineMode lifecycle.
- [x] C.2 Created `panels/viewport-panel.ts` — WebViewPanel with toolbar (W/E/R gizmo, Local/World), input forwarding (mouse move/button/key/scroll → postMessage → addon), 60fps render loop via setInterval, stats display.
- [x] C.3 Created `panels/scene-tree-panel.ts` — TreeDataProvider consuming `addon.getSceneTree()` JSON. Entity hierarchy with id/name/children. Refresh on scene load.
- [x] C.4 Created `panels/console-panel.ts` — OutputChannel with `setLogCallback` streaming. Formats: `[timestamp] [category] [level] message`.
- [x] C.5 Created `engine-mode.ts` — StatusBarItem toggle ($(game) Engine Mode / $(code) Code Mode). Ctrl+Shift+E keybinding.
- [x] C.6 `.uzscene` file association via `uzengine.openScene` command + `onLanguage:uzscene` activation.
- [x] C.7 All 5 commands + `uzengine.sceneTree` view + keybinding registered in `package.json`.
- [x] C.8 Activation events: `onLanguage:uzscene`, `onLanguage:uzproject` (onCommand auto-synthesized by VSCode).
- [x] C.9 Viewport geometry tracking: `onDidChangeViewState` detects panel visibility → start/stop render loop. Position tracking deferred to Phase 2.1 (HWND embedding).

## Phase D: Build Pipeline

- [x] D.0 DLL target in `build.zig` (`-Ddll=true`): recompiles `runtime.cpp` with `UZ_BUILDING_DLL`, links runtime.lib+editor.lib+system libs. Output: `uzengine_runtime.dll` (41MB) + import lib (6KB).
- [x] D.1 End-to-end verified: `zig build -Ddll=true` → `cmake-js compile` (MSVC) → `cp .node+DLLs extension/native/` → `npx tsc` → 5 JS output files. `uzengine.node` = 162KB.
- [x] D.2 Updated `scripts/build.ps1` — added `-Ddll=true` to zig build step, DLL+Agility SDK copy to `extension/native/`.
- [x] D.3 Updated `.vscode/tasks.json` — 5 tasks: build:all (full pipeline), build:runtime, build:napi, build:extension, build:runtime:debug.

## Testing

- [ ] T.1 C API unit test: `uz_create_viewport(0, 800, 600)` succeeds, `uz_render_frame()` returns non-zero fps, `uz_destroy_viewport()` doesn't crash.
- [ ] T.2 C API unit test: `uz_load_scene("scenes/pbr_showcase.uzscene")` loads entities, `uz_get_scene_tree()` returns valid JSON with entity names.
- [ ] T.3 C API unit test: `uz_create_entity("TestCube")` returns valid ID.
- [ ] T.4 C API unit test: `uz_set_log_callback(fn)` receives at least one log message during `uz_create_viewport`.
- [ ] T.5 C API unit test: `uz_capture_screenshot()` returns non-null PNG data with valid header (89 50 4E 47).
- [ ] T.6 N-API smoke test: load `uzengine.node` in Node.js, verify all 25 exports exist.
- [ ] T.7 Extension test: activate → `uzengine.openViewport` → viewport panel opens.
- [ ] T.8 Extension test: load `.uzscene` → scene tree populates.
- [ ] T.9 Stress test: create/destroy viewport 100 times → no D3D12MA leak assertion.

## Documentation

- [x] Doc.1 Updated `docs/specs/2.0-napi-foundation.md` — 10 deliverables marked DONE.
- [x] Doc.2 Updated `docs/engine/28-ipc-protocol/02-napi-bridge.md` — DLL architecture, 6-file split.
- [x] Doc.3 Created `docs/guides/build-and-deployment.md` — full build guide (385 lines).
- [x] Doc.4 Updated `extension/README.md` — architecture, commands, directory structure.
- [x] Doc.5 Updated root `README.md` — DLL build pipeline, manual steps.
- [x] Doc.6 Updated `napi/README.md` — DLL-based bridge, exported API table.
- [x] Doc.7 Updated `docs/engine/06-build-toolchain/04-napi-bridge.md` — DLL rationale, version matrix.
