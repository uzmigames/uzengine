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

### `physics/` (Phase 3) — Jolt Physics
| File | Purpose |
|------|---------|
| `physics_world.h/cpp` | `JoltPhysicsWorld` — thin wrapper over `JPH::PhysicsSystem` |
| `collider_builder.h/cpp` | `CCollider` → Jolt `Shape` creation |
| `ragdoll.h/cpp` | `RagdollSettings` from `.uzphys`, active ragdoll motor torques |

### `audio/` (Phase 3) — miniaudio
| File | Purpose |
|------|---------|
| `audio_engine.h/cpp` | `ma_engine` singleton, spatial audio, `CAudioSource` playback |

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

C++ helpers used by `game.dll` to set up and drive the LuaJIT VM. LuaJIT itself (`libluajit.lib`) links only into `game.dll` — these files expose the binding surface without coupling `uz_engine.lib` to LuaJIT.

| File | Purpose |
|------|---------|
| `lua_binding.h/cpp` | Register metatables: `Vec3`, `Quat`, `NodeRef`, `Scene`, `InputState`. Called by `game.dll` once on VM creation |
| `script_runner.h/cpp` | Per-node dispatch: `on_start`, `on_update`, `on_stop` via `lua_pcall`. Catches errors, reports file+line |
| `lua_sandbox.h/cpp` | Strips dangerous globals (`io.*`, `os.*`, `require`, `load`, etc.) from `lua_State` after `luajit_newstate()` |

---

## Key Invariants

1. **No GPU types** anywhere in `src/engine/`. Never include `<d3d12.h>`.
2. **No window handle** (HWND). Engine is process-agnostic.
3. **No IPC/networking**. Engine knows nothing about WebSocket or JSON.
4. **Assets identified by path**, never by GUID.
5. **Scripts attached by path**, graceful degradation on missing file.
6. **LuaJIT never links into `uz_engine.lib`**. Only `game.dll` links LuaJIT. `src/engine/scripting/` contains binding helpers only.

---

## Related Docs

- [cpp-architecture.md](../../engine/cpp-architecture.md) — API details for Arena, Handle, Result, Scene
- [design-decisions.md](../../engine/design-decisions.md) — why path-based assets, no global script registry, prefab model
- [../renderer/README.md](../renderer/README.md) — how Renderer consumes ECS data from this module
