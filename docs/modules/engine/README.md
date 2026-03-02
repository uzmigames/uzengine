# Module: Engine

> **Location**: `src/engine/`
> **Type**: Static library (`uz_engine.lib`)
> **Language**: C++23
> **GPU required**: No — fully headless, unit-testable on any machine

---

## Responsibility

The Engine module is the **foundation framework** for everything else. It represents and simulates a game world: scene hierarchy, entity data, asset loading, physics, audio, animation, input. It produces no pixels and touches no GPU.

Every other C++ module depends on Engine. Engine depends on nothing except standard library and platform OS headers.

---

## Subsystems

### `core/` — Primitives
Low-level building blocks with zero engine dependencies.

| File | Purpose |
|------|---------|
| `arena.h/cpp` | Linear arena allocator. Three lifetimes: `frame_arena` (reset/frame), `level_arena` (reset/scene load), `scratch` (thread-local temp) |
| `handle.h` | `Handle<T>` — 24-bit index + 8-bit generation. Stale handle → `nullptr`, never UB |
| `pool.h` | `Pool<T>` — slot array backing `Handle<T>`. Free list, generation bump on remove |
| `result.h` | `Result<T, E>` — explicit error propagation without exceptions |
| `string_id.h` | `StringId` — FNV-1a 64-bit hash, compile-time literal `"foo"_sid` |
| `assert.h` | `UZ_ASSERT(cond, msg)` — `__debugbreak()` in debug, no-op in shipping |
| `log.h` | `log_info/warn/error` — sink-agnostic; editor writes to OutputChannel, shipping writes to file |
| `math/` | `Vec2/3/4`, `Mat3/4`, `Quat`, `Transform3D` (GLM wrappers with UzEngine conventions) |
| `containers/` | Arena-backed `Array<T>`, `String`, `HashMap<K,V>` (Robin Hood open-address) |

### `scene/` — Scene Graph
Node hierarchy that represents the game world structure.

| File | Purpose |
|------|---------|
| `node.h/cpp` | `NodeBase`, `NodeType` enum, linked-list hierarchy (parent/child/sibling) |
| `node_types.h` | Per-type data structs: `Node3DData`, `StaticMeshData`, `CameraData`, etc. (all POD) |
| `scene.h/cpp` | `Scene`: `Pool<NodeBase>` + parallel component arrays, add/remove/traverse |
| `scene_loader.h/cpp` | `.uzscene` INI-like parser → `Scene` |
| `scene_serializer.h/cpp` | `Scene` → `.uzscene` text |
| `scene_manager.h/cpp` | Active scene lifecycle, reload-on-file-change, prefab expansion |

**Key rule**: Scripts are attached by file path. Missing script file → warning + node stays alive. No crash.

### `ecs/` — Entity Component System
Runtime data layer consumed by the Renderer and Game Logic thread. Uses `entt`.

| File | Purpose |
|------|---------|
| `components.h` | All component structs (POD only, no methods, no virtual): `CTransform`, `CMeshRenderer`, `CDirLight`, `CCamera`, `CRigidBody`, `CScriptRef`, etc. |
| `systems.h/cpp` | All system free functions grouped by phase: `update_transforms`, `cull_frustum`, `step_physics`, `update_animations` |
| `world.h/cpp` | `entt::registry` wrapper; scene graph → ECS entity mapping on scene load |

**Key rule**: Components are POD. Systems are free functions. No OOP inheritance in ECS.

### `assets/` — Resource Manager
Loads, caches, and evicts game assets. Identity = `res://`-relative path, never GUID.

| File | Purpose |
|------|---------|
| `resource_manager.h/cpp` | `load<T>(path)` → `ResourceHandle<T>`. LRU eviction. Thread-safe cache lookup |
| `resource_handle.h` | `ResourceHandle<T>` — typed index into resource cache |
| `formats/uz_mesh.h/cpp` | `.uzmesh` binary read/write |
| `formats/uz_mat.h/cpp` | `.uzmat` INI-like read/write |
| `import/mesh_import.h/cpp` | GLTF/FBX → `.uzmesh` (Phase 2: fastgltf + ufbx) |
| `import/texture_import.h/cpp` | PNG/HDR → BC7 DDS (Phase 2: DirectXTex) |

**Import sidecar rule**: `.import/` is gitignored and regenerated on demand. Deleting it is safe.

### `platform/` — OS Abstraction
Win32 specifics isolated here. Future: Linux stubs.

| File | Purpose |
|------|---------|
| `file_watcher.h/cpp` | `ReadDirectoryChangesW` async overlapped I/O. Posts `FileChange` events to a queue polled by the Main thread |
| `win32.h/cpp` | Timer (`QueryPerformanceCounter`), process helpers, path utilities |

### `physics/` (Phase 3) — Jolt Physics Engine
| File | Purpose |
|------|---------|
| `physics_world.h/cpp` | Jolt `PhysicsSystem` wrapper, world update, layer collision matrix |
| `physics_body_sync.h/cpp` | Mirror Jolt `JPH::Body` state to ECS `CTransform`, `CRigidBody` each frame |
| `physics_queries.h/cpp` | Raycast, linecast, overlap queries. Accessible from Lua via `ctx.world:raycast()`, etc. |
| `contact_listener.h/cpp` | Jolt contact/trigger event callbacks → `EventBus<CollisionEvent>`, `EventBus<TriggerEvent>` dispatch |
| `character_controller.h/cpp` | Jolt character controller integration for grounded-movement AI. Accessible via Lua `self:move(vel, dt)`, `self:jump()` |
| `physics_debug_draw.h/cpp` | On-demand wireframe rendering via `scene.setDebugPhysics(true)` IPC. DX12 editor pass (sort_key=900) |

Collision layers: 16-layer system, configurable in `.uzproj` with collision matrix.

### `audio/` (Phase 2) — XAudio2 Audio Engine
| File | Purpose |
|------|---------|
| `audio_engine.h/cpp` | XAudio2 initialization, 4 submix buses (Master, SFX, Music, Voice), voice pool (64 voices), lifecycle |
| `audio_clip.h/cpp` | Decoded PCM audio data, metadata (duration, channels, sample rate) |
| `audio_source.h/cpp` | ECS component definition; playback control |
| `audio_listener.h/cpp` | ECS component definition; 3D listener position for spatial audio (Phase 3) |
| `spatial_audio.h/cpp` | X3DAudio integration, 3D HRTF + attenuation curves (Phase 3) |
| `audio_format.h/cpp` | WAV/MP3/OGG decoding (stb_vorbis, libmpg123) at import time |

Note: Audio engine runs on the Game Logic thread, driven by `game.dll` via `GameContext` audio callbacks in `uz_game_update`. Hot-reload safe: voice buffers replaced in place.

### `animation/` (Phase 3) — Animation System
| File | Purpose |
|------|---------|
| `clip_sampler.h/cpp` | Sample `.uzanim` at time t (SLERP rotations, LERP pos/scale) |
| `state_machine.h/cpp` | `.uzanim_sm` state machine: transitions, conditions, blend |
| `ik_solver.h/cpp` | Two-bone analytical (limbs) + FABRIK iterative (spine/chains) |

### `input/` (Phase 3) — Input System
| File | Purpose |
|------|---------|
| `input_action.h` | `InputAction` struct, trigger types (Pressed/Released/Hold/Tap) |
| `mapping_context.h/cpp` | `.uzinput` parser, device-to-action mapping |
| `input_system.h/cpp` | Win32 raw input + XInput poll → `InputState` |

### `scripting/` (Phase 1) — Lua Binding Helpers

C++ helpers used by `game.dll` to set up and drive the LuaJIT VM. LuaJIT itself (`libluajit.lib`) links only into `game.dll` — these files expose the binding surface without coupling `uz_engine.lib` to LuaJIT. The VM is hosted in `game.dll` and managed by `src/game/lua_vm.h/cpp`.

| File | Purpose |
|------|---------|
| `lua_binding.h/cpp` | Register metatables: `Vec3`, `Quat`, `NodeRef`, `Scene`, `InputState`, `Timer` (via `ctx.*` globals). Called by `game.dll` once on VM creation |
| `script_runner.h/cpp` | Per-node dispatch: `on_start`, `on_update`, `on_stop` via `lua_pcall`. Catches errors, reports file+line. Called from `uz_game_update` after `drain_timers` |
| `lua_sandbox.h/cpp` | Strips dangerous globals (`io.*`, `os.*`, `require`, `load`, etc.) from `lua_State` after `luajit_newstate()` |

Note: VM lifecycle (creation, shutdown, hot-reload coordination) is owned by `src/game/lua_vm.h/cpp` (`game.dll`), not Engine.

### `timer/` (Phase 1) — Lua Timer System

Timers are a game.dll-owned facility accessed via Lua scripts. Engine provides binding helpers to expose timer API.

| File | Purpose |
|------|---------|
| (No files in `src/engine/timer/`) | Timer management (`LuaTimer` struct, `drain_timers()` method) lives in `src/game/lua_vm.h/cpp` (`game.dll`). Engine's `scripting/lua_binding.cpp` only registers the Lua API: `ctx:schedule_after(delay, callback)`, `ctx:schedule_every(interval, callback)`, `ctx:cancel_timer(id)`, `ctx:schedule_next_frame(callback)` |

Timer drain order per frame: next-frame callbacks fire first, then timed callbacks in registration order, then per-node `on_update` runs. Missing timers on hot-reload are gracefully discarded.

### `events/` (Phase 3) — Event Bus System

Typed publish-subscribe mechanism for decoupled communication between game systems. Includes predefined engine events (`SceneLoaded`, `SceneUnloaded`, `NodeAdded`, `NodeRemoved`).

| File | Purpose |
|------|---------|
| `event_bus.h/cpp` | `EventBus<T>` template, `AutoSub<T>` RAII subscriber wrapper, `SubId` type. Single-threaded (Game Logic thread only), no locks |
| (Lua API in `scripting/lua_binding.cpp`) | Registers `ctx.events` userdata: `emit(event_type, data)`, `subscribe(event_type, callback)`, `unsubscribe(sub_id)`. Wires C++ engine events → Lua dispatch |

### `tags/` (Phase 3) — Tag Registry

Per-node tagging system for grouping and querying nodes by semantic labels (e.g., "enemy", "collectible", "checkpoint").

| File | Purpose |
|------|---------|
| `tag_registry.h/cpp` | `TagRegistry`: manages `CTags` component (bitset<64>). Supports tag name ↔ bit index lookup. `query_tagged(tag_id)` → `Vec<NodeRef>` |

### `save/` (Phase 3) — Save/Load System

Binary serialization framework for game state snapshots. Engine provides format and manager; game.dll owns save data schema.

| File | Purpose |
|------|---------|
| `save_manager.h/cpp` | `SaveManager`: manages save slots (0–9 + autosave), file I/O to `%APPDATA%/UzEngine/<project>/saves/` |
| `save_writer.h/cpp` | `SaveWriter`: msgpack-based writer; lambda-based data serialization for flexibility |
| `save_reader.h/cpp` | `SaveReader`: msgpack parser; graceful fallback (missing keys use defaults, no crash) |

Saves include: node transforms, Lua persistent tables (`save.data`), thumbnail JPEG (320×180).

### `locale/` (Phase 3) — Localization System

Runtime language switching with fallback chains. Supports per-locale font overrides (critical for CJK).

| File | Purpose |
|------|---------|
| `locale_manager.h/cpp` | `LocaleManager`: loads `.csv` translation files (glob pattern in `.uzproj`). Supports runtime language switching. 3-tier fallback: current locale → English → key itself (no crash on missing translation) |
| (CSV parser in locale_manager.cpp) | Hand-written UTF-8 CSV parser with escape sequence handling and BOM detection |

Example `.csv` format: `ui.menu.play,Play,Jugar,Jogar` (key, en, es, pt_br).

### `console/` (Phase 1) — Console Command Registry

Developer console system for in-game debugging and cheats.

| File | Purpose |
|------|---------|
| `console_command_registry.h/cpp` | `ConsoleCommandRegistry`: command registration, execution, auto-complete. Bound to Lua via `ctx.console:execute(cmd_string)` and C++ via `GameContext` callbacks |

---

## Key Invariants

1. **No GPU types** anywhere in `src/engine/`. Never include `<d3d12.h>` in engine files.
2. **No window handle** (HWND). Engine is process-agnostic.
3. **No IPC/networking** directly in Engine. IPC handlers live in `src/runtime/ipc/handlers/` and call back into Engine via stable C APIs.
4. **Assets identified by path**, never by GUID.
5. **Scripts attached by path**, graceful degradation on missing file.
6. **LuaJIT never links into `uz_engine.lib`**. Only `game.dll` links LuaJIT. `src/engine/scripting/` contains binding helpers only.
7. **Audio subsystem** runs on the Game Logic thread, not a separate thread. Driven by `game.dll` callbacks in `uz_game_update`.
8. **All timer, event, and Lua VM operations are Game Logic thread-only**. No cross-thread synchronization needed.

---

## Related Docs

- [cpp-architecture.md](../../engine/cpp-architecture.md) — API details for Arena, Handle, Result, Scene
- [design-decisions.md](../../engine/design-decisions.md) — why path-based assets, no global script registry, prefab model
- [../renderer/README.md](../renderer/README.md) — how Renderer consumes ECS data from this module
- [../../specs/lua-scripting.md](../../specs/lua-scripting.md) — Lua VM lifecycle, binding patterns, hot-reload
- [../../specs/timer-system.md](../../specs/timer-system.md) — Timer API, drain order, hot-reload behavior
- [../../specs/event-bus.md](../../specs/event-bus.md) — EventBus<T>, predefined engine events, Lua API
- [../../specs/physics-system.md](../../specs/physics-system.md) — Jolt integration, collision layers, raycast queries
- [../../specs/audio-system.md](../../specs/audio-system.md) — XAudio2, bus system, spatial audio, Lua bindings
- [../../specs/localization.md](../../specs/localization.md) — CSV format, locale switching, font fallback
- [../../specs/save-system.md](../../specs/save-system.md) — Binary format, save slots, hot-reload safety

## Rulebook Tasks

Phase 1 scripting/timer/console: [phase1-lua-scripting](../../.rulebook/tasks/phase1-lua-scripting/), [phase1-timer-system](../../.rulebook/tasks/phase1-timer-system/), [phase1-console-commands](../../.rulebook/tasks/phase1-console-commands/)

Phase 2 audio/physics/rendering: [phase2-audio-system](../../.rulebook/tasks/phase2-audio-system/), [phase3-physics-system](../../.rulebook/tasks/phase3-physics-system/)

Phase 3 events/tags/save/locale: [phase3-event-bus](../../.rulebook/tasks/phase3-event-bus/), [phase3-tags-groups](../../.rulebook/tasks/phase3-tags-groups/), [phase3-save-system](../../.rulebook/tasks/phase3-save-system/), [phase3-localization](../../.rulebook/tasks/phase3-localization/)
