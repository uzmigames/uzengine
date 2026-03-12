# Scripting — Hot-Reload

> **Status**: Planned (Phase 3)
> **Source**: `engine/scripting/lua_hot_reload.h`, `script_hot_reload.cpp`
> **Depends on**: Script runner (§15.4), FileWatcher (§7), world
> **References**: Godot GDScript hot reload, Roblox Studio script reload

## Purpose

Hot-reload allows changing Lua scripts without stopping the game. As a developer edits a script file on disk, the engine detects the change, reloads and recompiles the script, and restarts affected entities. This dramatically speeds up iteration.

## Hot-Reload Architecture

```
FileWatcher detects .lua change
        │
        ▼
ScriptHotReloadManager
        │
        ├─ 1. Find all entities using this script
        │
        ├─ 2. Call on_stop(ctx) on each entity
        │     (script saves state via ctx.world)
        │
        ├─ 3. Reload script bytecode from disk
        │
        ├─ 4. Call on_start(ctx) on each entity
        │     (script restores state if needed)
        │
        └─ ✓ Done (in-world state preserved)
```

## State Preservation Problem

When a script reloads, the entity should ideally maintain its state. There are three approaches:

```
Approach 1: Stateless (simplest)
  ├─ on_stop() clears all state
  └─ on_start() reinitializes from scratch
  Problem: Loss of runtime modifications (player position, health, etc.)

Approach 2: Component-based (recommended)
  ├─ on_stop() saves state to entity components
  ├─ Script reloads
  └─ on_start() reads state from components
  Best for: Game state that needs persistence anyway

Approach 3: Lua table persistence (risky)
  ├─ on_stop() saves table to ctx.world
  ├─ Script reloads
  └─ on_start() restores from ctx.world
  Problem: Lua tables may hold invalid C++ pointers after reload
```

**Recommended**: Use Component-based approach (Approach 2).

## Stateless Hot-Reload (Simple)

For simple scripts, hot-reload just resets the script:

```lua
-- scripts/rotating_mesh.lua

local rotation_speed = 90  -- degrees/sec

function on_start(ctx)
    -- No state to save
    ctx.log("Rotating mesh started")
end

function on_update(ctx, dt)
    local entity = ctx.entity
    local rot = entity:get_rotation()
    rot.z = rot.z + rotation_speed * dt
    entity:set_rotation(rot)
end

function on_stop(ctx)
    -- No cleanup needed
end
```

When the script reloads, `on_stop` is called (no-op), then `on_start` reinitializes, and `on_update` continues. No state is lost because there is no state.

## Component-Based State Preservation

For complex scripts, save state in components:

```lua
-- scripts/enemy_ai.lua

function on_start(ctx)
    local entity = ctx.entity

    -- Try to restore saved state
    local ai_component = entity:get_component("AIComponent")
    if ai_component then
        ctx.current_health = ai_component.health
        ctx.current_target = ai_component.target_id
    else
        -- First time or reloaded, use defaults
        ctx.current_health = 100
        ctx.current_target = nil
    end

    ctx.log("Enemy AI restarted, health=" .. ctx.current_health)
end

function on_update(ctx, dt)
    local entity = ctx.entity

    -- AI logic
    if ctx.current_health <= 0 then
        entity:destroy()
        return
    end

    -- ...move, attack, etc...

    -- Save state to component (will survive reload)
    local ai = entity:get_component("AIComponent")
    if ai then
        ai.health = ctx.current_health
        ai.target_id = ctx.current_target
    end
end

function on_stop(ctx)
    -- State is already saved in components
    ctx.log("Enemy AI stopped")
end
```

## Hot-Reload Manager

```cpp
class ScriptHotReloadManager {
public:
    /// Start watching for script changes
    void EnableHotReload(World* world) {
        world_ref = world;

        FileWatcher* watcher = GetSubsystem<FileWatcher>();
        watcher->Watch("res://scripts/", [this](const std::string& path) {
            if (StringUtils::EndsWith(path, ".lua")) {
                OnScriptChanged(path);
            }
        });

        UZ_LOG(Scripting, Info, "Hot-reload enabled for Lua scripts");
    }

    /// Called when a script file changes on disk
    void OnScriptChanged(const std::string& script_path) {
        UZ_LOG(Scripting, Info, "Script changed: {}", script_path);

        // Find all entities using this script
        std::vector<Entity> affected = FindEntitiesUsingScript(script_path);
        if (affected.empty()) {
            UZ_LOG(Scripting, Debug, "No entities using this script");
            return;
        }

        // Reload script for each entity
        for (Entity entity : affected) {
            ReloadScriptForEntity(entity, script_path);
        }
    }

private:
    World* world_ref = nullptr;

    /// Find all entities with ScriptComponent using this script
    std::vector<Entity> FindEntitiesUsingScript(const std::string& script_path) {
        std::vector<Entity> result;

        // Iterate all entities
        world_ref->ForEachEntity([&](Entity entity) {
            if (auto script = entity.GetComponent<ScriptComponent>()) {
                if (script->GetScriptPath() == script_path) {
                    result.push_back(entity);
                }
            }
        });

        return result;
    }

    /// Reload a script on a specific entity
    void ReloadScriptForEntity(Entity entity, const std::string& script_path) {
        auto* script_comp = entity.GetComponent<ScriptComponent>();
        if (!script_comp) return;

        UZ_LOG(Scripting, Info, "Hot-reloading script on entity {}: {}",
               entity.GetName(), script_path);

        // 1. Call on_stop (allow cleanup)
        ScriptRunner runner;
        if (!runner.StopScript(entity)) {
            UZ_LOG(Scripting, Warning, "on_stop failed during hot-reload");
        }

        // 2. Reload script bytecode from disk
        if (!runner.AttachScript(entity, script_path)) {
            UZ_LOG(Scripting, Error, "Failed to reload script: {}", script_path);
            script_comp->SetError("Reload failed");
            return;
        }

        // 3. Call on_start (script restores state from components)
        if (!runner.StartScript(entity)) {
            UZ_LOG(Scripting, Error, "on_start failed during hot-reload");
            script_comp->SetError("on_start failed");
            return;
        }

        UZ_LOG(Scripting, Info, "Hot-reload successful for entity {}", entity.GetName());
    }
};
```

## Global Script Reloads

Some scripts are singletons (e.g., game controller, UI manager). Hot-reload them globally:

```cpp
void GlobalScriptHotReload(const std::string& script_path) {
    // Used for:
    // - Utility functions (math helpers, string parsing)
    // - Global event handlers (input, UI)
    // - Configuration scripts

    World* world = GameInstance::Get()->GetWorld();
    LuaVM* vm = world->GetLuaVM();

    UZ_LOG(Scripting, Info, "Global hot-reload: {}", script_path);

    // Re-execute script in global scope
    if (!vm->ExecuteScript(script_path)) {
        UZ_LOG(Scripting, Error, "Global reload failed");
        return;
    }

    // Call any global on_reload() hook
    std::string error;
    if (!vm->CallFunction("on_reload")) {
        // on_reload is optional
    }
}
```

## Dependency Tracking

If script A depends on script B, reloading B should reload A:

```cpp
struct ScriptDependency {
    std::string script_path;
    std::vector<std::string> dependencies;  // Other scripts this requires
};

class ScriptDependencyTracker {
private:
    std::map<std::string, ScriptDependency> graph;

public:
    /// Register that script_a requires script_b
    void RegisterDependency(const std::string& a, const std::string& b) {
        graph[a].script_path = a;
        graph[a].dependencies.push_back(b);
    }

    /// Get all scripts that (directly or transitively) depend on script_path
    std::vector<std::string> GetDependents(const std::string& script_path) {
        std::vector<std::string> result;
        for (auto& [key, dep] : graph) {
            if (StringUtils::Contains(dep.dependencies, script_path)) {
                result.push_back(key);
                // Recursively get dependents of dependents
                auto sub_deps = GetDependents(key);
                result.insert(result.end(), sub_deps.begin(), sub_deps.end());
            }
        }
        return result;
    }
};

void OnScriptChanged(const std::string& script_path) {
    // Reload the script itself
    ScriptHotReloadManager().OnScriptChanged(script_path);

    // Reload all dependent scripts
    ScriptDependencyTracker tracker;
    auto dependents = tracker.GetDependents(script_path);
    for (const auto& dep : dependents) {
        ScriptHotReloadManager().OnScriptChanged(dep);
    }
}
```

## Debugging Hot-Reload

When hot-reload fails, preserve error context:

```lua
-- scripts/buggy.lua

function on_start(ctx)
    -- This will error during hot-reload
    local undefined_var = undefined_var_name  -- BUG!
end

function on_stop(ctx)
    -- This is still called even if on_start will fail
end
```

When the script reloads with this bug:

```
ScriptComponent state: Error
LastError: "Lua error: scripts/buggy.lua:5: attempt to index a nil value"
```

The entity's script component tracks the error state and displays it in the editor inspector.

## Hot-Reload Best Practices

### 1. Separate State from Logic

Bad (state in global):
```lua
local health = 100

function on_update(ctx, dt)
    health = health - 10 * dt  -- Lost on reload!
end
```

Good (state in component):
```lua
function on_update(ctx, dt)
    local entity = ctx.entity
    local ai = entity:get_component("AIComponent")
    ai.health = ai.health - 10 * dt  -- Persisted in component
end
```

### 2. Idempotent on_start

```lua
-- BAD: on_start has side effects that accumulate
function on_start(ctx)
    ctx.world:spawn_entity("Projectile")  -- Spawns new one every reload!
end

-- GOOD: on_start is idempotent
function on_start(ctx)
    -- Restore from saved state only
    local entity = ctx.entity
    local ai = entity:get_component("AIComponent")
    if not ai.spawned_projectile then
        ctx.world:spawn_entity("Projectile")
        ai.spawned_projectile = true
    end
end
```

### 3. No Circular Dependencies

```lua
-- AVOID: A requires B, B requires A
-- scripts/a.lua: require("scripts/b")
-- scripts/b.lua: require("scripts/a")

-- This causes infinite reload loops if either changes
```

## Engine Comparison

| Feature | Godot | Roblox Studio | **UzEngine** |
|---------|-------|--------------|------------|
| Script hot-reload | GDScript instant | Lua/Script hot-reload | **Lua hot-reload** |
| State preservation | Automatic (scene saved) | Manual via datastore | **Component-based** |
| Error handling | Stop script | Log and continue | **Log, mark error, continue** |
| Dependency tracking | GD script dependency graph | Manual requires() | **Planned for Phase 3.5** |

## Rules

1. **ALWAYS use component state, not Lua globals** — globals are lost on reload
2. **NEVER spawn entities in on_start** — use flags or component state to track
3. **ALWAYS make on_start idempotent** — reloading should not duplicate effects
4. **NEVER have circular dependencies** — A→B→A causes infinite reloads
5. **ALWAYS test hot-reload with modified scripts** — verify state survives
