# Lua Runtime Demo Infrastructure — Tasks

## Phase A: LuaJIT Build Integration

- [ ] A.1 Add LuaJIT 2.1 to build system — download pre-built Windows `.lib`/`.dll` or build via vcpkg. Add to `runtime/build.zig` link flags.
- [ ] A.2 Verify LuaJIT links and initializes — minimal test: `luaL_newstate()` + `luaL_dostring("return 1+1")` in a unit test.
- [ ] A.3 Deploy `lua51.dll` to output directory alongside other DLLs.

## Phase B: LuaVM Subsystem

- [ ] B.1 Create `runtime/engine/scripting/lua_vm.h/cpp` — `LuaVM : ISubsystem` singleton. Owns `lua_State*`. Init phase: `Init`. Tick group: `Update`.
- [ ] B.2 Implement sandbox — strip `os`, `io`, `debug`, `load`, `loadfile`, `dofile`, `require` from global table. Only `math`, `string`, `table`, `print`, `pairs`, `ipairs`, `type`, `tostring`, `tonumber`, `error`, `pcall`, `xpcall`, `select`, `unpack` remain.
- [ ] B.3 Implement `uz` global table registration — empty table at init, populated by API modules.
- [ ] B.4 Implement script loading — `LuaVM::LoadScript(path)` reads `.lua` file via VFS, compiles with `luaL_loadbuffer`, returns ref.
- [ ] B.5 Implement error handling — `lua_pcall` wrapper that catches errors, logs via `UZ_LOG(Scripting, Error, ...)` with file:line info.
- [ ] B.6 Unit tests for LuaVM init, sandbox stripping, script load, error handling.

## Phase C: Core Lua API

- [ ] C.1 `uz.log(msg)` / `uz.log_warn(msg)` / `uz.log_error(msg)` — maps to `UZ_LOG(Script, Info/Warning/Error, ...)`.
- [ ] C.2 `uz.delta_time` / `uz.total_time` / `uz.frame_index` — read-only globals updated each frame by LuaVM tick.
- [ ] C.3 `uz.quit()` — calls `EngineLoop::RequestExit()`.
- [ ] C.4 Unit tests for core API.

## Phase D: Scene Lua API

- [ ] D.1 `uz.scene.find(name)` — returns entity handle (lightuserdata) by name. Returns nil if not found.
- [ ] D.2 `uz.scene.get_position(entity)` / `uz.scene.set_position(entity, x, y, z)` — read/write CTransform.position.
- [ ] D.3 `uz.scene.get_rotation(entity)` / `uz.scene.set_rotation(entity, pitch, yaw, roll)` — read/write CTransform.rotation (Euler degrees).
- [ ] D.4 `uz.scene.get_scale(entity)` / `uz.scene.set_scale(entity, x, y, z)` — read/write CTransform.scale.
- [ ] D.5 Unit tests for scene API (mock World with test entities).

## Phase E: Camera Lua API

- [ ] E.1 `uz.camera.set_position(x, y, z)` / `uz.camera.get_position()` — read/write active camera position.
- [ ] E.2 `uz.camera.look_at(x, y, z)` — orient camera toward target.
- [ ] E.3 `uz.camera.set_fov(degrees)` / `uz.camera.get_fov()` — field of view.
- [ ] E.4 Unit tests for camera API.

## Phase F: Input Lua API

- [ ] F.1 `uz.input.is_key_down(key)` — returns bool. Key names: "W", "A", "S", "D", "F", "Space", "Escape", "Num0"-"Num9", etc.
- [ ] F.2 `uz.input.is_key_just_pressed(key)` — returns bool (edge-triggered, true only on frame of press).
- [ ] F.3 `uz.input.get_mouse_delta()` — returns dx, dy (two values).
- [ ] F.4 `uz.input.is_mouse_button_down(button)` — "Left", "Right", "Middle".
- [ ] F.5 Unit tests for input API.

## Phase G: LuaScriptComponent

- [ ] G.1 Create `CScript` ECS component — holds script path (string), lua ref (int), loaded flag.
- [ ] G.2 Create `ScriptSystem` — queries all `CScript` components. On first tick: loads script, calls `on_start(ctx)`. Every tick: calls `on_update(ctx, dt)`. On entity destroy: calls `on_stop(ctx)`.
- [ ] G.3 `ctx` table passed to callbacks contains: entity handle, convenience accessors (`ctx.entity`, `ctx.get_position()`, `ctx.set_position(x,y,z)`).
- [ ] G.4 Register "script" component factory in ComponentFactory registry — scene loader creates `CScript` from `.uzscene` JSON `"script": {"path": "scripts/foo.lua"}`.
- [ ] G.5 Unit tests for script lifecycle (start/update/stop), error recovery (script error doesn't crash engine).

## Phase H: .uzproject Loader

- [ ] H.1 Create `runtime/engine/app/project_loader.h/cpp` — parse `.uzproject` JSON via nlohmann/json (already in deps).
- [ ] H.2 Extract window config (title, width, height, fullscreen) → apply to window creation.
- [ ] H.3 Extract render config (backend, vsync, resolution) → apply to RHI init.
- [ ] H.4 Extract `default_scene` path → load `.uzscene` file.
- [ ] H.5 Extract `content_root` → set VFS mount point.
- [ ] H.6 Unit tests for project loader (valid/invalid JSON, missing fields, defaults).

## Phase I: .uzscene Loader

- [ ] I.1 Create `runtime/engine/ecs/scene_loader.h/cpp` — parse `.uzscene` JSON → create entities with components.
- [ ] I.2 Implement entity tree creation — walk `entities[]` array recursively, create entities, set parent/child hierarchy.
- [ ] I.3 Implement component dispatch — for each key in `components {}`, look up `ComponentFactory`, call `Create(entity, jsonData)`.
- [ ] I.4 Register built-in component factories: `transform`, `mesh`, `light`, `camera`, `script`.
- [ ] I.5 `mesh` factory loads `.uzmesh` + textures from `content/` paths, uploads to GPU.
- [ ] I.6 `light` factory creates `CLight` with type/color/intensity/shadows from JSON.
- [ ] I.7 `camera` factory creates `CCamera` with fov/near/far from JSON.
- [ ] I.8 Unit tests for scene loader (empty scene, single entity, hierarchy, unknown component graceful skip).

## Phase J: Runtime Standalone Mode

- [ ] J.1 Create `runtime/app/main_runtime.cpp` — new `main()` entry point: parse `--project <path>`, init EngineLoop, load project, run.
- [ ] J.2 Add `uzengine_runtime` executable target to `build.zig` — links runtime lib, outputs `uzengine.exe`.
- [ ] J.3 Integrate project loader into EngineLoop — if `--project` flag, load `.uzproject` before subsystem init, configure window/renderer from project.
- [ ] J.4 Integrate scene loader — after subsystems init, load `default_scene`, spawn entities.
- [ ] J.5 Integrate LuaVM — after scene load, initialize scripts on all `CScript` entities.
- [ ] J.6 Screenshot support — `--screenshot <path> --screenshot-frame <N>` works same as demos.
- [ ] J.7 Smoke test: `uzengine.exe --project sample/demo.uzproject` loads and renders a triangle scene.

## Phase K: Renderer Lua API (PBR Demo Needs)

- [ ] K.1 `uz.renderer.set_render_path(path)` — "deferred" or "forward". Maps to RenderPath enum.
- [ ] K.2 `uz.renderer.set_debug_mode(mode)` — 0=off, 1-7=GBuffer channels.
- [ ] K.3 `uz.renderer.get_stats()` — returns table {fps, frame_time, draw_calls, triangles}.
- [ ] K.4 Unit tests for renderer API.

## Phase L: PBR Demo Conversion

- [ ] L.1 Create `demos/pbr_test/demo.uzproject`:
  ```json
  {
    "name": "PBR Test",
    "version": "1.0.0",
    "engine_version": "0.3.0",
    "default_scene": "scenes/main.uzscene",
    "content_root": "content/",
    "window": { "title": "UzEngine — PBR Test", "width": 1280, "height": 720 },
    "render": { "backend": "dx12", "vsync": false }
  }
  ```
- [ ] L.2 Create `demos/pbr_test/scenes/main.uzscene` — 5 cube entities with mesh+material components, ground plane, 4 point lights, 1 directional light, camera entity.
- [ ] L.3 Create `demos/pbr_test/scripts/pbr_controller.lua`:
  - `on_start`: log demo info, set initial camera position
  - `on_update`: handle F key (toggle render path), number keys (debug mode), FPS camera via mouse+WASD
- [ ] L.4 Create `demos/pbr_test/content/` — symlink or copy mesh/texture assets.
- [ ] L.5 Run: `uzengine.exe --project demos/pbr_test/demo.uzproject --screenshot test.png --screenshot-frame 5`
- [ ] L.6 **Visual verification** — view screenshot, compare with existing C++ demo output. Must match: 5 cubes, correct PBR lighting, shadows, ground plane.
- [ ] L.7 Test F-key toggle and debug modes interactively.
- [ ] L.8 Document conversion process in `docs/guides/demo-conversion-guide.md`.

## Phase M: Cleanup & Polish

- [ ] M.1 Hot-reload scaffolding — FileWatcher on `.lua` files, re-execute `on_stop` → reload → `on_start` (basic implementation, no full production polish needed).
- [ ] M.2 Lua error overlay — script errors displayed on screen via stats overlay (file, line, message).
- [ ] M.3 Update `ROADMAP.md` with new Lua scripting milestone status.
- [ ] M.4 Archive this task after all visual verification passes.
