# Patch Notes — 2026-03-14 — Lua Scripting Phase 1 Complete, SceneRenderer, Build System Changes

## New Features

### Lua Scripting System — Phase 1 Complete
- **LuaVM singleton** with LuaJIT 2.1 embedded in each World
- **Per-node script lifecycle**: `on_start(ctx)`, `on_update(ctx, dt)`, `on_stop(ctx)`
- **Sandboxed Lua state**: `os.execute`, `io.open`, `load`, `require` stripped for security
- **Lua API modules** (all implemented):
  - `uz.log(msg)` — engine logging
  - `uz.scene.get_rotation(name)` / `uz.scene.set_rotation(name, x, y, z)` — entity transform queries
  - `uz.camera.set_position(x,y,z)` / `uz.camera.look_at(x,y,z)` — camera control
  - `uz.input.is_key_down(key)` / `uz.input.is_key_just_pressed(key)` — keyboard input
  - `uz.delta_time`, `uz.total_time` — frame timing
- **ScriptComponent** — attaches `.lua` scripts to entities with hot-reload support
- **Lua API modules**: `lua_camera_api.cpp`, `lua_input_api.cpp`, `lua_scene_api.cpp`, `lua_renderer_api.cpp`

### SceneRenderer + ECS Integration
- **SceneRenderer**: Replaces legacy renderer, ECS-driven draw list generation
- **Scene loading**: New `scene_loader.cpp/h` loads `.uzscene` files into ECS
- **Project loading**: New `project_loader.cpp/h` loads `.uzproject` TOML configuration
- **ECS components added**:
  - `NameComponent` — entity identifier
  - `StaticMeshComponent` — mesh rendering
  - `CameraComponent` — camera entity
  - `ScriptComponent` — Lua script attachment
- **Sample project** updated: `demo.uzscene` + `hello.lua` sample script

## Build System Changes

### Library Naming & Separation
- **Runtime library**: Renamed from `uzengine_runtime` → `runtime.lib`
- **Editor library**: Renamed from `uzengine_editor` → `editor.lib`
- **Umbrella header**: New `runtime/editor/uzeditor.h` includes grid, font, stats, screenshot, compare_grid subsystems
- **Demo auto-linkage**: `build_common.zig` automatically links both `runtime.lib` and `editor.lib` — demos no longer need manual `extra_sources` configuration
- **Build flag**: `-Deditor` enables editor features (grid rendering, stats overlay, screenshot capture)

### Removed Demo-Level Configuration
- Demos no longer maintain individual `extra_sources` in `build.zig`
- All editor code now unified in `editor.lib` — demos get editor features automatically when `-Deditor` is set

## Vulkan Rendering Fixes

### Shadow Map Rendering — Stencil Attachment Bug
- **Issue**: `BeginRenderPass` was unconditionally setting `pStencilAttachment` for all depth formats
- **Fix**: Only set `pStencilAttachment` when format has stencil channel (D24UnormS8Uint, D32FloatS8Uint)
- **Impact**: D32Float and D16Unorm depth-only renders now work correctly; prevents invalid attachment descriptor errors

### Resource Barrier — Layout Tracking
- **Issue**: `ResourceBarrier` computed `oldLayout` from `stateBefore` enum, causing UNDEFINED→DEPTH_STENCIL_READ_ONLY_OPTIMAL mismatch on first frame
- **Fix**: Use `tex->currentLayout` as the actual previous layout, tracking layout across frames
- **Impact**: Vulkan validation layer no longer reports layout transition errors; shadow map chain working correctly

## Breaking Changes

- **Library names** — Code linking `uzengine_runtime`/`uzengine_editor` must update to `runtime`/`editor`
- **Demo build.zig** — Remove all `extra_sources` entries; now automatic via `build_common.zig`

## Build / Infra

- Lua scripts now hot-reload in editor without engine restart
- Demo build system simplified (no per-demo configuration maintenance)
- Vulkan backend more robust for depth-stencil rendering across multiple formats

## Sample Files

```
sample/
├── demo.uzproject          — TOML project configuration
├── content/
│   ├── scenes/
│   │   └── demo.uzscene    — Scene with camera + ScriptComponent
│   └── scripts/
│       └── hello.lua       — Example script with on_start/on_update
```

## Documentation Updated

- `docs/engine/15-scripting/01-overview.md` — Status changed to **Active (Phase 1)**
- `docs/engine/15-scripting/02-luajit-integration.md` — Complete API reference
- `docs/engine/15-scripting/04-script-runner.md` — ScriptComponent and lifecycle details
- `docs/engine/15-scripting/06-lua-api.md` — All Lua bindings documented
- `docs/engine/06-build-toolchain/01-zig-build-system.md` — Updated library names and editor.lib changes
