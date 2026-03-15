# Lua Runtime Demo Infrastructure — Tasks

## Phase A: LuaJIT Build Integration

- [x] A.1 Add LuaJIT 2.1 to build system — download pre-built Windows `.lib`/`.dll` or build via vcpkg. Add to `runtime/build.zig` link flags.
- [x] A.2 Verify LuaJIT links and initializes — minimal test: `luaL_newstate()` + `luaL_dostring("return 1+1")` in a unit test.
- [x] A.3 Deploy `lua51.dll` to output directory alongside other DLLs.

## Phase B: LuaVM Subsystem

- [x] B.1 Create `runtime/engine/scripting/lua_vm.h/cpp` — `LuaVM : ISubsystem` singleton. Owns `lua_State*`. Init phase: `Init`. Tick group: `Update`.
- [x] B.2 Implement sandbox — strip `os`, `io`, `debug`, `load`, `loadfile`, `dofile`, `require` from global table. Only `math`, `string`, `table`, `print`, `pairs`, `ipairs`, `type`, `tostring`, `tonumber`, `error`, `pcall`, `xpcall`, `select`, `unpack` remain.
- [x] B.3 Implement `uz` global table registration — empty table at init, populated by API modules.
- [x] B.4 Implement script loading — `LuaVM::LoadScript(bytes, name)` compiles with `luaL_loadbuffer`, returns registry ref. `UnloadScript(ref)` frees it.
- [x] B.5 Implement error handling — `LuaVM::PCall(ref, nargs, nresults, source)` wrapper that catches errors, logs via `UZ_LOG(Scripting, Error, ...)` with file:line info.
- [x] B.6 Unit tests for LuaVM init, sandbox stripping, script load, error handling (28 tests in `tests/engine/scripting/test_lua_vm.cpp`).

## Phase C: Core Lua API

- [x] C.1 `uz.log(msg)` / `uz.log_warn(msg)` / `uz.log_error(msg)` — maps to `UZ_LOG(Scripting, Info/Warning/Error, "[Lua] ...")`.
- [x] C.2 `uz.delta_time` / `uz.total_time` / `uz.frame_index` — updated each frame by `LuaVM::UpdateFrameGlobals()`.
- [x] C.3 `uz.quit()` — sets `uz_quit_requested` flag in Lua registry.
- [x] C.4 Unit tests for core API (included in B.6 test suite — 28 tests total, all passing).

## Phase D: Scene Lua API

- [x] D.1 `uz.scene.find(name)` — returns entity handle (lightuserdata) by name via CName component. Returns nil if not found.
- [x] D.2 `uz.scene.get_position(entity)` / `uz.scene.set_position(entity, x, y, z)` — read/write SceneComponent.position.
- [x] D.3 `uz.scene.get_rotation(entity)` / `uz.scene.set_rotation(entity, pitch, yaw, roll)` — Euler degrees ↔ Quat conversion.
- [x] D.4 `uz.scene.get_scale(entity)` / `uz.scene.set_scale(entity, x, y, z)` — read/write SceneComponent.scale.
- [x] D.5 Unit tests for scene API — 14 tests in `tests/engine/scripting/test_lua_scene_api.cpp` (real World with entities, find/position/rotation/scale/error handling).

## Phase E: Camera Lua API

- [x] E.1 `uz.camera.set_position(x, y, z)` / `uz.camera.get_position()` — finds active CameraComponent.
- [x] E.2 `uz.camera.look_at(x, y, z)` — Shepperd quaternion from orthonormal basis (handles vertical degenerate case).
- [x] E.3 `uz.camera.set_fov(degrees)` / `uz.camera.get_fov()` — clamped [1, 179].
- [x] E.4 Unit tests for camera API. *(29 tests in test_lua_camera_api.cpp, all passing.)*

## Phase F: Input Lua API

- [x] F.1 `uz.input.is_key_down(key)` — uses platform InputSystem, string→Key enum mapping (A-Z, 0-9, Space, Escape, F1-F12, arrows, modifiers).
- [x] F.2 `uz.input.is_key_just_pressed(key)` — edge-triggered via InputSystem.
- [x] F.3 `uz.input.get_mouse_delta()` — returns dx, dy from InputSystem.
- [x] F.4 `uz.input.is_mouse_button_down(button)` — "Left", "Right", "Middle".
- [x] F.5 Unit tests for input API. *(36 tests in test_lua_input_api.cpp, all passing.)*

## Phase G: LuaScriptComponent

- [x] G.1 Create `CScript` ECS component — holds scriptPath, luaRef, envRef, started flag. Load-failed sentinel prevents retry.
- [x] G.2 Create `ScriptSystem` — static class with Initialize/Update/Shutdown. Per-script Lua environment via setfenv. Loads from VFS, calls on_start(entity)/on_update(entity, dt)/on_stop(entity).
- [x] G.3 Entity passed as lightuserdata arg to callbacks (same encoding as uz.scene API). Scripts use uz.scene.get_position(entity) etc.
- [x] G.4 Register "script" component factory in ComponentFactory registry — scene loader creates `CScript` from `.uzscene` JSON `"script": {"path": "scripts/foo.lua"}`. *(Implemented directly in scene_loader.cpp component dispatch.)*
- [x] G.5 Unit tests for script lifecycle (start/update/stop), error recovery (script error doesn't crash engine). *(27 tests in test_script_system.cpp, all passing.)*

## Phase H: .uzproject Loader

- [x] H.1 Create `runtime/engine/app/project_loader.h/cpp` — ProjectConfig struct + LoadProject(path) via nlohmann/json.
- [x] H.2 Extract window config (title, width, height, fullscreen) from JSON.
- [x] H.3 Extract render config (backend, vsync) from JSON.
- [x] H.4 Extract `default_scene` path from JSON.
- [x] H.5 Extract `content_root` from JSON (defaults to "content").
- [x] H.6 Unit tests for project loader (valid/invalid JSON, missing fields, defaults). *(41 tests in test_project_loader.cpp, all passing.)*

## Phase I: .uzscene Loader

- [x] I.1 Create `runtime/engine/ecs/scene_loader.h/cpp` — `LoadScene(world, bytes, name)` parses JSON → creates entities.
- [x] I.2 Recursive entity tree creation — walks `entities[]` + `children[]` arrays.
- [x] I.3 Component dispatch — `transform`, `script`, `camera`, `light` (directional/point/spot).
- [x] I.4 Built-in component parsers: transform (Euler→Quat), script (CScript), camera (CameraComponent), light (3 types).
- [ ] I.5 `mesh` factory loads `.uzmesh` + textures from `content/` paths, uploads to GPU. *(deferred — needs GPU)*
- [x] I.6 Light factory creates DirectionalLight/PointLight/SpotLight with color/intensity/range from JSON.
- [x] I.7 Camera factory creates CameraComponent with fov/near/far/active from JSON.
- [x] I.8 Unit tests for scene loader (empty scene, single entity, hierarchy, unknown component graceful skip). *(32 tests in test_scene_loader.cpp, all passing.)*

## Phase J: Runtime Standalone Mode

- [x] J.1 Create `runtime/app/main_runtime.cpp` — new `main()` entry point: parse `--project <path>`, init EngineLoop, load project, run. *(Integrated into existing `runtime/tools/uzengine.cpp` instead of new file.)*
- [x] J.2 Add `uzengine_runtime` executable target to `build.zig` — links runtime lib, outputs `uzengine.exe`. *(Existing `uzengine` target updated with lua51.lib linkage.)*
- [x] J.3 Integrate project loader into EngineLoop — if `--project` flag, load `.uzproject` before subsystem init, configure window/renderer from project.
- [x] J.4 Integrate scene loader — after subsystems init, load `default_scene`, spawn entities.
- [x] J.5 Integrate LuaVM — after scene load, initialize scripts on all `CScript` entities.
- [ ] J.6 Screenshot support — `--screenshot <path> --screenshot-frame <N>` works same as demos.
- [x] J.7 Smoke test: `uzengine.exe --project sample/demo.uzproject` loads and renders a triangle scene. *(Verified: 10 entities loaded, Lua on_start/on_update/on_stop all fire correctly, 314 FPS, clean shutdown.)*

## Phase K: SceneRenderer + Renderer Lua API

The goal: `uzengine.exe --project` renders a full PBR scene driven entirely by .uzscene + Lua scripts. No C++ demo code needed.

### K-A: SceneRenderer (C++ — renders World automatically)
- [x] K.A1 Create `engine/renderer/scene_renderer.h/cpp` — owns ShaderManager, PipelineLibrary, CBRingAllocator, GBuffer, RenderWorld, RgResourcePool. Single class that initializes all rendering infrastructure.
- [x] K.A2 `SceneRenderer::Init(device, width, height)` — creates shaders, pipelines, GBuffer, CB ring, shadow atlas, render graph pool.
- [x] K.A3 `SceneRenderer::SyncFromWorld(World&)` — iterates ECS components (CMesh, CMaterial, lights, camera) and syncs to RenderWorld proxies. Creates/updates/destroys proxies as entities change. *(Implemented as SyncWorldToDrawList — direct ECS→DrawList without intermediate RenderWorld, simpler for Phase 0.)*
- [x] K.A4 `SceneRenderer::RenderFrame(cmdList, swapChain, dt)` — builds DrawList from RenderWorld, records render graph (shadow + GBuffer + lighting + overlay + present), executes.
- [x] K.A5 `SceneRenderer::Resize(width, height)` — recreates GBuffer.
- [x] K.A6 `SceneRenderer::Shutdown()` — destroys all GPU resources.
- [x] K.A7 Integrate into `uzengine.cpp RunWindowed()` — replace `RenderClearFrame` with `SceneRenderer::RenderFrame()`. *(Falls back to clear screen if SceneRenderer init fails.)*

### K-B: Mesh/Texture/Material ECS Components
- [x] K.B1 Add `meshPath`/`meshLoaded` fields to existing `StaticMeshComponent` — tracks mesh asset per entity. *(No separate CMeshComponent needed — fields added to StaticMeshComponent.)*
- [x] K.B2 Add `albedoPath`/`normalPath`/`roughnessPath`/`metallicPath`/`aoPath`/`materialLoaded` fields to `StaticMeshComponent` — tracks PBR material per entity. *(Material paths live on same component for simplicity in Phase 0.)*
- [x] K.B3 Scene loader: parse `"mesh": {"asset": "meshes/cube.uzmesh"}` → emplace StaticMeshComponent with meshPath.
- [x] K.B4 Scene loader: parse `"material": {"albedo": "...", "normal": "...", ...}` → set texture paths on existing StaticMeshComponent.
- [x] K.B5 SceneRenderer::SyncWorldToDrawList lazy-loads meshes/textures on first encounter (GPU upload via VFS + LoadMesh/LoadTexture).

### K-C: Lua Renderer API (high-level)
- [x] K.C1 `uz.renderer.set_render_path("deferred"/"forward")` — sets RenderPath on SceneRenderer.
- [x] K.C2 `uz.renderer.set_debug_mode(0-7)` — 0=off, 1-7=GBuffer channels.
- [x] K.C3 `uz.renderer.get_stats()` → {fps, draw_calls} from StatsOverlay.
- [x] K.C4 `uz.mesh.load(path)` → mesh_id via QueueMeshLoad (deferred GPU upload).
- [x] K.C5 `uz.texture.load(path)` → texture_id via QueueTextureLoad (deferred GPU upload).
- [x] K.C6 `uz.scene.set_mesh(entity, mesh_id)` — assigns mesh to entity (creates StaticMeshComponent if needed).
- [x] K.C7 `uz.scene.set_material(entity, {albedo=tex_id, normal=tex_id, ...})` — assigns PBR material textures.
- [x] K.C8 Register all Lua APIs — `LuaRegisterRendererAPI(L)` in lua_vm.cpp Init, `LuaSetSceneRenderer` in uzengine.cpp.

### K-D: Integration + Smoke Test
- [x] K.D1 Build and run: `uzengine.exe --project sample/demo.uzproject` — builds clean, loads project/scene, initializes Lua. *(GPU device creation requires desktop, not CI-testable.)*
- [ ] K.D2 Lua script can toggle render path with F key, debug mode with number keys. *(pbr_controller.lua created, needs visual test on desktop.)*
- [ ] K.D3 Visual verification — screenshot matches PBR demo output.

## Phase L: PBR Demo Conversion (Lua-only)

- [x] L.1 Create `sample/content/scripts/pbr_controller.lua` — FPS camera (WASD+mouse), F=toggle render path, 0-7=debug modes.
- [x] L.2 Update `sample/content/scenes/demo.uzscene` — mesh components with uzmesh paths, material components with PBR texture paths (albedo/normal/roughness/metallic/ao).
- [ ] L.3 Run `uzengine.exe --project sample/demo.uzproject` — full PBR scene from Lua, no C++ demo.
- [ ] L.4 **Visual verification** — screenshot, compare with C++ PBR demo.
- [ ] L.5 Document in `docs/guides/lua-project-guide.md`.

## Phase N: Lua API Parity — Fill Gaps

Goal: Every C++ engine capability accessible from Lua. Ordered by gameplay impact.

### N-A: ECS Iteration & Component Access
- [x] N.A1 `uz.scene.get_entities()` → array of entity handles (all alive entities).
- [x] N.A2 `uz.scene.get_entities_with(component_name)` → filtered array. Supported names: "transform", "camera", "light", "mesh", "script".
- [x] N.A3 `uz.scene.has_component(entity, component_name)` → bool. Supports: transform, camera, light, mesh, script, name.
- [x] N.A4 `uz.scene.remove_component(entity, component_name)` — removes named component. Same name mapping.
- [x] N.A5 `uz.scene.clear()` — destroys all entities (World::Clear).
- [x] N.A6 Unit tests for ECS iteration/component access.

### N-B: Transform Hierarchy
- [x] N.B1 `uz.scene.get_world_position(entity)` → x,y,z (computed from hierarchy).
- [x] N.B2 `uz.scene.get_world_matrix(entity)` → 16 numbers (row-major flat).
- [x] N.B3 `uz.scene.attach_to(child, parent)` — parent-child transform hierarchy.
- [x] N.B4 `uz.scene.detach(entity)` — unparent.
- [x] N.B5 `uz.scene.get_parent(entity)` → entity or nil.
- [x] N.B6 `uz.scene.get_children(entity)` → array of entity handles.
- [x] N.B7 Unit tests for hierarchy API.

### N-C: Camera Extended
- [x] N.C1 `uz.camera.get_near()` / `uz.camera.set_near(n)` — near plane.
- [x] N.C2 `uz.camera.get_far()` / `uz.camera.set_far(f)` — far plane.
- [x] N.C3 `uz.camera.set_orthographic(enabled, size)` / `uz.camera.is_orthographic()`.
- [x] N.C4 `uz.camera.set_active(entity)` — switch active camera by entity.
- [x] N.C5 `uz.camera.get_view_matrix()` → 16 numbers.
- [x] N.C6 `uz.camera.get_projection_matrix()` → 16 numbers.
- [x] N.C7 Unit tests for camera extended API.

### N-D: Input Extended
- [x] N.D1 `uz.input.is_key_just_released(key)` → bool.
- [x] N.D2 `uz.input.get_mouse_position()` → x, y (absolute screen coords).
- [x] N.D3 `uz.input.get_scroll_delta()` → dx, dy.
- [x] N.D4 `uz.input.set_mouse_capture(enabled)` / `uz.input.is_mouse_captured()`. *(set_mouse_capture is no-op from Lua — requires HWND, logged as warning)*
- [x] N.D5 `uz.input.set_cursor_visible(enabled)` / `uz.input.is_cursor_visible()`.
- [x] N.D6 `uz.input.is_gamepad_connected(index)` → bool.
- [x] N.D7 `uz.input.get_gamepad_axis(index, axis_name)` → number (-1..1).
- [x] N.D8 `uz.input.is_gamepad_button_down(index, button_name)` → bool.
- [x] N.D9 Unit tests for input extended API.

### N-E: Lights Extended
- [x] N.E1 `uz.scene.get_light_range(entity)` → number (point/spot only).
- [x] N.E2 `uz.scene.get_spot_angles(entity)` → inner_deg, outer_deg.
- [x] N.E3 `uz.scene.set_spot_angles(entity, inner_deg, outer_deg)`.
- [x] N.E4 `uz.scene.get_light_direction(entity)` → x,y,z (directional only).
- [x] N.E5 `uz.scene.set_cast_shadows(entity, enabled)` — toggle shadow casting.
- [x] N.E6 `uz.scene.remove_light(entity)` — removes any light component.
- [x] N.E7 Unit tests for lights extended API.

### N-F: Mesh/Material Extended
- [x] N.F1 `uz.scene.get_mesh(entity)` → mesh_id or nil.
- [x] N.F2 `uz.scene.get_material(entity)` → table {albedo=id, normal=id, ...} or nil.
- [x] N.F3 `uz.scene.set_visible(entity, enabled)` — toggle StaticMeshComponent.visible.
- [x] N.F4 `uz.scene.is_visible(entity)` → bool.
- [x] N.F5 `uz.scene.set_cast_shadows(entity, enabled)` — toggle castShadows (shared with N.E5 for lights).
- [x] N.F6 Unit tests for mesh/material extended API.

### N-G: Renderer Extended
- [x] N.G1 `uz.renderer.set_debug_view(mode_name)` — DebugViewMode by string name (case-insensitive).
- [x] N.G2 `uz.renderer.get_debug_view()` → string (current mode name).
- [x] N.G3 `uz.renderer.screenshot(path)` — queues screenshot request via Lua registry.
- [x] N.G4 Unit tests for renderer extended API.

### N-H: Math Types (Vec3/Quat userdata with metatables)
- [x] N.H1 `uz.math.vec3(x,y,z)` → Vec3 userdata with __add, __sub, __mul, __div, __unm, __len, __tostring, __eq, __index (x,y,z fields).
- [x] N.H2 Vec3 methods: `v:dot(v2)`, `v:cross(v2)`, `v:normalize()`, `v:length()`, `v:distance(v2)`, `v:lerp(v2,t)`.
- [x] N.H3 Vec3 constants: `uz.math.VEC3_ZERO`, `uz.math.VEC3_ONE`, `uz.math.VEC3_UP`, `uz.math.VEC3_RIGHT`, `uz.math.VEC3_FORWARD`.
- [x] N.H4 `uz.math.quat(x,y,z,w)` → Quat userdata with __mul (compose + rotate vec3), __tostring, __eq.
- [x] N.H5 Quat constructors: `uz.math.quat_from_euler(pitch,yaw,roll)`, `uz.math.quat_from_axis_angle(vec3, angle)`, `uz.math.quat_identity()`.
- [x] N.H6 Quat methods: `q:slerp(q2,t)`, `q:inverse()`, `q:normalize()`, `q:to_euler()` → pitch,yaw,roll degrees.
- [x] N.H7 Unit tests for Vec3 and Quat userdata (arithmetic, methods, constants).

### N-I: Scene Management
- [x] N.I1 `uz.scene.load_scene(scene_path)` — load .uzscene file via VFS, replacing current world.
- [x] N.I2 `uz.scene.save_scene(scene_path)` — stub (logs warning, returns false — serialization not yet needed).
- [x] N.I3 Unit tests for scene load/save from Lua.

## Phase M: Cleanup & Polish

- [ ] M.1 Hot-reload scaffolding — FileWatcher on `.lua` files, re-execute `on_stop` → reload → `on_start` (basic implementation, no full production polish needed).
- [ ] M.2 Lua error overlay — script errors displayed on screen via stats overlay (file, line, message).
- [ ] M.3 Update `ROADMAP.md` with new Lua scripting milestone status.
- [ ] M.4 Archive this task after all visual verification passes.
