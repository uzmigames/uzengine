# Scripting — C++ & Lua

> **Status**: Active (Phase 1 Complete — Lua scripting, Phase 2+ C++ behaviors planned)
> **Source**: `engine/scripting/`, `lua_vm.h`, `script_component.h`, `lua_*_api.cpp`
> **References**: LuaJIT 2.1 integration, UE5 Blueprints + C++ dual model (reference), Godot GDScript + C++ pattern (reference)

## Purpose

Game logic can be written in C++ (primary, full performance) or Lua (secondary, rapid prototyping and mod support). Both languages are well-represented in LLM training data. JSON scene files reference scripts by path — LLMs can generate and modify game logic directly.

## C++ (Primary)

- Game logic written in C++ compiled against engine headers
- Shipped as DLL/SO for hot-reload in editor, statically linked in shipping builds
- Same language as engine internals — zero bridging cost
- Full access to engine API, components, subsystems, and physics

### Game Code Model

Game code uses the `Behavior` system (§10). Behaviors are registered via `UZ_REGISTER_BEHAVIOR` and instantiated from `.uzscene` files. No `main()` needed.

```cpp
class EnemyAI : public uz::Behavior {
    UZ_BEHAVIOR(EnemyAI);

public:
    void OnBeginPlay() override {
        // Initialize AI state
    }

    void OnTick(float dt) override {
        // Per-frame AI logic
    }
};

UZ_REGISTER_BEHAVIOR(EnemyAI);
```

### Hot-Reload (Editor)

In editor mode, game code compiles as a shared library (`game.dll`). When the source changes:

1. FileWatcher detects `.cpp`/`.h` changes
2. Engine pauses, saves world state
3. Unloads `game.dll`
4. Recompiles via Zig build
5. Loads new `game.dll`
6. Restores world state, calls `OnBeginPlay()` on new instances

In shipping builds, game code is statically linked — no DLL overhead.

## Lua (Secondary)

Embedded LuaJIT VM for rapid prototyping and mod support. Lua scripts are hot-reloadable without recompilation.

### Lua VM Lifecycle

```
World Creation
    │
    ├── Create LuaVM (sandbox stripped: os.execute, io.*, etc.)
    ├── Register engine bindings (Vec3, Entity, World, Input, Audio, Physics)
    ├── Load script files referenced by components
    │
    ├── Per-Frame: call on_update(dt) on active scripts
    ├── On Event: dispatch registered callbacks
    │
    └── World Destruction: close LuaVM, release all references
```

### Script Runner

Lua scripts attach to entities via a `ScriptComponent`:

```json
{
    "id": "collectible",
    "components": {
        "SceneComponent": { "position": [5, 0, 3] },
        "StaticMeshComponent": { "parent": "SceneComponent", "mesh": "content/meshes/coin.uzmesh" },
        "ScriptComponent": { "script": "scripts/collectible.lua" }
    }
}
```

### Lua API

```lua
-- scripts/collectible.lua

local spin_speed = 90  -- degrees/sec

function on_start(ctx)
    -- ctx.entity: this entity
    -- ctx.world: the World
    ctx.world:get(ctx.entity, "TriggerVolume").on_overlap_begin:bind(function(event)
        if ctx.world:has(event.other_entity, "PlayerTag") then
            ctx.world:destroy(ctx.entity)
            ctx.audio:play_2d("content/audio/coin.wav")
        end
    end)
end

function on_update(ctx, dt)
    local scene = ctx.world:get(ctx.entity, "SceneComponent")
    local rot = scene.rotation
    rot.z = rot.z + spin_speed * dt
    scene.rotation = rot
end
```

### Lua Bindings

Engine types exposed to Lua via metatables:

| Lua Type | C++ Type | Operations |
|----------|----------|------------|
| `Vec3` | `uz::Vec3` | `+`, `-`, `*`, `.x/.y/.z`, `length()`, `normalize()`, `dot()`, `cross()` |
| `Quat` | `uz::Quat` | `*`, `slerp()`, `from_axis_angle()` |
| `Entity` | `uz::Entity` | `is_valid()`, comparison |
| `World` | `uz::World*` | `get()`, `has()`, `create()`, `destroy()`, `find()` |

### Sandbox

The Lua VM is sandboxed — dangerous globals are stripped:

```
Stripped: os.execute, os.remove, io.*, loadfile, dofile, require (replaced with safe loader)
Kept: math.*, string.*, table.*, coroutine.*, print (redirected to UZ_LOG)
```

### Hot-Reload

When a `.lua` file changes on disk:

1. FileWatcher detects change
2. Call `on_stop(ctx)` on the old script instance
3. Re-load and re-parse the Lua file
4. Call `on_start(ctx)` on the new script instance
5. Lua state (global variables) is NOT preserved across hot-reload — design scripts to be stateless or re-initialize in `on_start`

## Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| Primary | C++ | GDScript/C++ | C# | **C++** |
| Secondary | Blueprint (visual) | C#/GDExtension | None (Bolt visual) | **Lua (text)** |
| Hot-reload | C++ live coding | GDScript instant | C# domain reload | **C++ DLL + Lua instant** |
| Mod support | Lua/Blueprint | GDExtension | None built-in | **Lua scripts** |
| LLM-friendly | C++ (good), Blueprint (poor) | GDScript (medium) | C# (good) | **C++ (good), Lua (good)** |
| Scene reference | Blueprint path | Script path | MonoBehaviour | **Script path (`.lua`)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 1** | ✅ **DONE** — LuaJIT 2.1 VM, sandboxed state, `ScriptComponent`, per-node lifecycle (`on_start`/`on_update`/`on_stop`), Lua APIs (camera, input, scene, renderer), hot-reload |
| **Phase 2** | C++ Behavior system, `UZ_REGISTER_BEHAVIOR`, hot-reload via game.dll (ECS integration) |
| **Phase 3** | Enhanced Lua bindings (physics, audio, events), Lua debug integration (breakpoints via VSCode DAP), Lua profiler |
| **Phase 4** | Lua coroutine scheduler, async/await patterns, Lua module system with imports |
