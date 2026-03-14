# Scripting — Script Runner

> **Status**: Active (Phase 1 Complete)
> **Source**: `engine/scripting/script_component.h/cpp`, `script_runner.cpp`
> **Depends on**: LuaVM (§15.2), sandbox (§15.3), component system (§10), ECS World system
> **References**: Godot `Node._process/_ready/_exit_tree`, Roblox `Script.Parent`, UE5 `Actor.BeginPlay/EndPlay`

## Purpose

The script runner manages the lifecycle of individual Lua scripts attached to entities. Each entity can have multiple `ScriptComponent` instances, each running a Lua script. The runner handles binding scripts to entities, calling lifecycle hooks (`on_start`, `on_update`, `on_stop`), and exposing engine APIs through the `ctx` parameter.

## Architecture

```
Entity
├── Components
│   ├── TransformComponent
│   ├── MeshComponent
│   └── ScriptComponent (script: "scripts/behavior.lua")
│       └── ScriptRunner
│           ├── Lua VM reference
│           ├── Script table (_G.entity_X)
│           ├── Loaded script bytecode
│           └── Lifecycle state (started, running, stopped)
│
└── Per-frame tick
    └── ScriptRunner::OnTick()
        ├── Call on_update(ctx, dt) if running
        └── Handle errors, timeouts
```

## ScriptComponent

```cpp
/// Lua script attached to an entity.
class ScriptComponent : public Component {
public:
    /// Path to Lua script relative to res:// VFS
    std::string script_path;

    /// If true, skip calling on_start on load (for blueprint reuse)
    bool autoplay = true;

    /// Maximum time per tick (seconds)
    /// Scripts exceeding this timeout are halted
    float tick_timeout = 0.1f;

    /// Called by framework to run per-frame update
    void OnTick(float dt);

    /// Manually start the script (if autoplay=false)
    bool Start();

    /// Manually stop the script
    void Stop();

    /// Get script state (for debugging)
    enum class State {
        NotStarted,
        Running,
        Stopped,
        Error,
    };
    State GetState() const { return state; }

    /// Last error message (if state == Error)
    std::string GetLastError() const { return last_error; }

private:
    State state = State::NotStarted;
    std::string last_error;
    lua_State* script_L = nullptr;
    Entity owner_entity;
};
```

## Script Lifecycle

```
Script Attach
    │
    ├─ Check script file exists (res://path)
    │
    └─ Parse and validate Lua syntax

Autoplay == true?
    │
    ├─ YES → Call on_start(ctx)
    │        │
    │        └─ Script initializes state
    │
    └─ NO  → Wait for Start() call

Running State
    │
    ├─ Each frame: Call on_update(ctx, dt)
    │
    └─ Until entity destroyed or Stop() called

Script Stop
    │
    ├─ Call on_stop(ctx)
    │
    └─ Release resources
```

## Script Execution

```cpp
class ScriptRunner {
public:
    /// Attach script to entity, load file, prepare VM
    bool AttachScript(Entity entity, const std::string& script_path) {
        // 1. Get entity and world
        World* world = entity.GetWorld();
        LuaVM* lua_vm = world->GetLuaVM();

        // 2. Validate script file exists
        std::string resolved_path = PathSystem::ResolvePath(script_path);
        if (!FileSystem::Exists(resolved_path)) {
            UZ_LOG(Scripting, Error, "Script not found: {}", script_path);
            return false;
        }

        // 3. Load script bytecode (or cache if already loaded)
        std::string script_code = FileSystem::Read(resolved_path);
        if (script_code.empty()) {
            UZ_LOG(Scripting, Error, "Failed to read script: {}", script_path);
            return false;
        }

        // 4. Parse and check syntax
        if (!CheckLuaSyntax(lua_vm, script_code)) {
            UZ_LOG(Scripting, Error, "Syntax error in {}", script_path);
            return false;
        }

        UZ_LOG(Scripting, Debug, "Script loaded: {}", script_path);
        return true;
    }

    /// Call on_start() if script defines it
    bool StartScript(Entity entity) {
        LuaVM* lua_vm = entity.GetWorld()->GetLuaVM();
        lua_State* L = lua_vm->GetState();

        // Push ctx table (see ctx API below)
        PushContextTable(L, entity);

        // Get on_start function
        lua_getglobal(L, "on_start");
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 2);  // Pop ctx and nil
            return true;    // on_start is optional
        }

        // Call on_start(ctx)
        lua_pushvalue(L, -2);  // Push ctx again as argument
        if (lua_pcall(L, 1, 0, 0) != 0) {
            const char* error = lua_tostring(L, -1);
            UZ_LOG(Scripting, Error, "on_start error: {}", error);
            lua_pop(L, 2);  // Pop error and ctx
            return false;
        }

        lua_pop(L, 1);  // Pop ctx
        return true;
    }

    /// Call on_update(ctx, dt) each frame
    bool TickScript(Entity entity, float dt) {
        LuaVM* lua_vm = entity.GetWorld()->GetLuaVM();
        lua_State* L = lua_vm->GetState();

        // Push ctx table
        PushContextTable(L, entity);

        // Get on_update function
        lua_getglobal(L, "on_update");
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 2);  // Pop ctx and nil
            return true;    // on_update is optional
        }

        // Call on_update(ctx, dt)
        lua_pushvalue(L, -2);      // Push ctx
        lua_pushnumber(L, dt);     // Push dt
        if (lua_pcall(L, 2, 0, 0) != 0) {
            const char* error = lua_tostring(L, -1);
            UZ_LOG(Scripting, Error, "on_update error: {}", error);
            lua_pop(L, 2);
            return false;
        }

        lua_pop(L, 1);  // Pop ctx
        return true;
    }

    /// Call on_stop() on shutdown
    bool StopScript(Entity entity) {
        LuaVM* lua_vm = entity.GetWorld()->GetLuaVM();
        lua_State* L = lua_vm->GetState();

        PushContextTable(L, entity);

        lua_getglobal(L, "on_stop");
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 2);
            return true;  // on_stop is optional
        }

        lua_pushvalue(L, -2);
        if (lua_pcall(L, 1, 0, 0) != 0) {
            const char* error = lua_tostring(L, -1);
            UZ_LOG(Scripting, Error, "on_stop error: {}", error);
            lua_pop(L, 2);
            return false;
        }

        lua_pop(L, 1);
        return true;
    }

private:
    /// Build context object (ctx parameter passed to Lua)
    void PushContextTable(lua_State* L, Entity entity) {
        lua_newtable(L);  // Create ctx = {}

        // ctx.entity = entity
        PushEntity(L, entity);
        lua_setfield(L, -2, "entity");

        // ctx.world = world
        World* world = entity.GetWorld();
        PushWorld(L, world);
        lua_setfield(L, -2, "world");

        // ctx.input = input system
        InputSystem* input = GetSubsystem<InputSystem>();
        PushInputAPI(L, input);
        lua_setfield(L, -2, "input");

        // ... ctx.audio, ctx.physics, ctx.camera, etc.
    }
};
```

## Context API (ctx Parameter)

The `ctx` parameter passed to lifecycle functions contains engine APIs:

```lua
function on_start(ctx)
    -- ctx.entity: this entity
    ctx.entity:get_name()
    ctx.entity:set_position({100, 50, 200})

    -- ctx.world: the world
    local other = ctx.world:find_entity_by_name("NPC")
    ctx.world:spawn_entity("Projectile")

    -- ctx.input: input system
    if ctx.input:is_key_pressed("space") then
        -- Jump
    end

    -- ctx.audio: audio system
    ctx.audio:play_sound("content/audio/jump.wav")

    -- ctx.physics: physics queries
    local hit = ctx.physics:ray_cast(from_pos, to_pos)

    -- ctx.camera: camera system
    ctx.camera:play_shake({duration = 0.5})

    -- ctx.ui: UI system (Phase 3+)
    ctx.ui:show_text("Hello!")

    -- ctx.log: logging
    ctx.log("Script started", "Scripting")
end

function on_update(ctx, dt)
    -- Same apis as on_start, called each frame
    -- dt: delta time in seconds
end

function on_stop(ctx)
    -- Called on entity destruction or script unload
    -- Clean up subscriptions, timers, etc.
end
```

## Error Handling

Scripts that error are logged and the entity continues running (graceful degradation):

```cpp
bool ScriptComponent::OnTick(float dt) {
    if (state != State::Running) return true;

    // Call on_update
    ScriptRunner runner;
    Entity owner = GetOwnerEntity();

    if (!runner.TickScript(owner, dt)) {
        state = State::Error;
        last_error = runner.GetLastError();

        // Log error once
        UZ_LOG(Scripting, Error, "Script error on entity {}: {}",
               owner.GetName(), last_error);

        return false;
    }

    return true;
}
```

## Script Initialization Example

```lua
-- scripts/player.lua

local move_speed = 500  -- units/second
local jump_force = 1000
local grounded = false

function on_start(ctx)
    -- Cache entity reference
    local entity = ctx.entity
    ctx.log("Player script initialized", "Scripting")

    -- Subscribe to input events
    ctx.input:on_key_press("space", function()
        if grounded then
            Jump(ctx)
        end
    end)
end

function on_update(ctx, dt)
    local entity = ctx.entity
    local input = ctx.input

    -- Movement
    local move_dir = {x = 0, y = 0, z = 0}
    if input:is_key_down("w") then move_dir.z = move_dir.z + 1 end
    if input:is_key_down("a") then move_dir.x = move_dir.x - 1 end
    if input:is_key_down("s") then move_dir.z = move_dir.z - 1 end
    if input:is_key_down("d") then move_dir.x = move_dir.x + 1 end

    if uz.Vec3(move_dir):length() > 0 then
        move_dir = uz.Vec3(move_dir):normalize()
        local pos = entity:get_position()
        pos = pos + uz.Vec3(move_dir) * move_speed * dt
        entity:set_position(pos)
    end

    -- Check grounded
    local pos = entity:get_position()
    local below = ctx.physics:ray_cast(pos, pos + uz.Vec3(0, -100, 0))
    grounded = below:is_hit()
end

function on_stop(ctx)
    ctx.log("Player script stopped", "Scripting")
    -- Clean up subscriptions
end

function Jump(ctx)
    local entity = ctx.entity
    local vel = entity:get_velocity()
    entity:set_velocity(uz.Vec3(vel.x, jump_force, vel.z))
end
```

## Multi-Script Entities

An entity can have multiple scripts (each independent):

```json
{
    "id": "npc",
    "components": {
        "ScriptComponent": [
            {"script": "scripts/npc_ai.lua"},
            {"script": "scripts/npc_dialog.lua"},
            {"script": "scripts/npc_animation.lua"}
        ]
    }
}
```

Each script runs independently. They communicate through the World or component data.

## Engine Comparison

| Aspect | UE5 | Godot | Unity | **UzEngine** |
|--------|-----|-------|-------|------------|
| Script attachment | Blueprint property | Node scene | Monobehaviour | **ScriptComponent** |
| Lifecycle | BeginPlay/EndPlay | `_ready/_exit_tree` | `OnEnable/OnDisable` | **on_start/on_update/on_stop** |
| Context | `this` (Pawn) | `self` (Node) | `gameObject`, `transform` | **ctx table** |
| Per-frame tick | `Tick(dt)` | `_process(dt)` | `Update()` | **on_update(ctx, dt)** |
| Error handling | Crash or log | Pause/stop | Exception | **Log + continue** |

## Rules

1. **ALWAYS check `on_start` succeeded** before entity is playable
2. **NEVER store references across scenes** — scripts reload on LoadScene()
3. **ALWAYS call `on_stop` on entity destroy** — allow cleanup
4. **NEVER use global variables** — use entity components or ctx.world instead
5. **ALWAYS handle timeouts gracefully** — long-running scripts will be halted
