# Debug Console Commands

> **Status**: Planning
> **Phase**: 1
> **Priority**: High — essential for debugging during development and live gameplay

---

## Overview

The Debug Console provides developers and game creators with a **real-time command interface** for inspecting and manipulating game state during gameplay. It is primarily a **development tool** (visible in editor and `--debug` builds only), but some commands are safe for shipping games.

**Key Design**:
- **~-key toggle**: Press `~` to open/close the console overlay
- **Auto-complete**: Tab key completes command names and arguments
- **Command history**: Up/Down arrows navigate previous commands
- **Lua-first registration**: Game code registers commands via simple Lua API
- **C++ alternative**: C++ game code can also register commands via `ConsoleCommandRegistry`
- **IPC exposed**: Editor can execute commands via `console.execute` IPC
- **Rich output**: Commands return formatted strings (plain text, structured JSON for complex output)

---

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│ In-Game Console Overlay (Main Thread)                    │
│  - Input capture: ~ key, keyboard, mouse                 │
│  - Layout: ImGui panel (debug builds) or custom canvas   │
│  - Input processing: parse command, dispatch             │
└─────────────────────┬──────────────────────────────────┘
                      │
        ┌─────────────┴─────────────┐
        ▼                           ▼
┌───────────────────────┐   ┌──────────────────────┐
│ ConsoleCommandRegistry│   │ Lua Console Binding  │
│ (C++ Singleton)       │   │ (ctx.console)        │
├───────────────────────┤   ├──────────────────────┤
│ - register_command    │   │ - register()         │
│ - dispatch_command    │   │ - execute()          │
│ - get_all_commands    │   │ - get_history()      │
│ - autocomplete()      │   │ - clear()            │
└───────────────────────┘   └──────────────────────┘
        │                           │
        └─────────────┬─────────────┘
                      ▼
        ┌──────────────────────────┐
        │ Built-in Commands        │
        │ - help, quit             │
        │ - reload_scene           │
        │ - set_timescale          │
        │ - toggle_physics_debug   │
        │ - spawn, kill, set_prop  │
        │ - stats, gc              │
        └──────────────────────────┘

        ┌──────────────────────────┐
        │ Game-Registered Commands │
        │ - god_mode               │
        │ - give_item              │
        │ - teleport_player        │
        │ - ai_attack              │
        │ (game.dll or scene.lua)  │
        └──────────────────────────┘

        │
        ▼ IPC Exposure

┌──────────────────────────────────────────────────────────┐
│ VSCode Editor (console.execute IPC)                      │
│  - Embedded console terminal                             │
│  - History, suggestions                                  │
└──────────────────────────────────────────────────────────┘
```

---

## In-Game Console Overlay

### Activation

- **Key**: `~` (tilde, backtick on some keyboards)
- **Visibility**: Debug builds only (Release/Shipping: console not rendered)
- **Input capture**: When open, console captures all input; game world receives nothing

### Interface

```
═══════════════════════════════════════════════════════════
│ Debug Console                                       [X]   │
├───────────────────────────────────────────────────────────┤
│ > set_timescale 0.5                                       │
│ Timescale set to 0.5x (slow motion)                      │
│                                                           │
│ > reload_shaders                                          │
│ Reloading shaders...                                      │
│ ✓ Loaded 23 shaders in 42ms                              │
│                                                           │
│ > stats                                                   │
│ FPS: 144 | Frame Time: 6.9ms                             │
│ GPU: RTX 3070 (8GB) | Draw Calls: 156                    │
│ Arena Memory: 128 MB / 1 GB                              │
│                                                           │
│ >█ (user typing here)                                    │
├───────────────────────────────────────────────────────────┤
│ help for commands | ↑↓ history | Tab autocomplete        │
═══════════════════════════════════════════════════════════
```

### Interaction

| Key | Action |
|-----|--------|
| `Enter` | Execute command |
| `Tab` | Autocomplete command or argument |
| `↑` / `↓` | Navigate command history |
| `Ctrl+C` | Clear input |
| `Escape` | Close console |
| `~` | Toggle console (open/close) |

---

## Command Format

```
command_name [arg1] [arg2] ...
```

**Command name**: Alphanumeric + underscores (`[a-zA-Z_][a-zA-Z0-9_]*`)

**Arguments**:
- **Unquoted**: Whitespace-delimited (e.g., `kill 42` → args = `["42"]`)
- **Quoted**: Can contain spaces (e.g., `set_prop 10 name "Player Name"` → args = `["10", "name", "Player Name"]`)
- **Numbers**: Parsed as floats or integers depending on context

---

## Built-In Commands

### Core Commands

```
help
  Shows all registered commands with descriptions and argument info.

  Example output:
  ┌─ Debug Console Help ─────────────────────────────┐
  │ help                      — Show this help        │
  │ quit                      — Exit game              │
  │ reload_scene              — Reload scene           │
  │ reload_shaders            — Hot-reload shaders     │
  │ reload_scripts            — Hot-reload Lua scripts │
  │ set_timescale <float>     — Set timescale (0–10)  │
  │ stats                     — Print FPS, memory     │
  │ god_mode                  — Player invincibility   │
  │ [more commands...]                                │
  └───────────────────────────────────────────────────┘

quit
  Exit the game (close render.exe).

  Usage: quit

  Example:
    > quit
    Shutting down...
```

### Scene Commands

```
reload_scene
  Unload current scene and reload from disk. All unsaved state is lost.

  Usage: reload_scene

  Example:
    > reload_scene
    Reloading scene: res://scenes/level1.uzscene...
    ✓ Scene loaded (2.1s)

print_scene
  Dump the scene node tree to console output and engine log.

  Usage: print_scene [filter]

  Arguments:
    filter (optional) — Show only nodes matching a name pattern

  Example:
    > print_scene
    Scene: res://scenes/level1.uzscene
    ├─ World (id=0, Transform)
    │  ├─ Terrain (id=1, Terrain)
    │  ├─ Player (id=10, Character)
    │  │  └─ Camera (id=11, Camera)
    │  └─ Enemy_001 (id=20, Character)
    └─ Lighting (id=100, Empty)
       ├─ Sun (id=101, Light, dir)
       └─ Ambient (id=102, Light, ambient)

    > print_scene Player
    ├─ Player (id=10, Character)
    │  ├─ model = "res://models/player.gltf"
    │  ├─ health = 100
    │  └─ Camera (id=11, Camera)
```

### Shader & Asset Commands

```
reload_shaders
  Hot-reload all HLSL shader files from disk. Shader variants are recompiled.

  Usage: reload_shaders

  Example:
    > reload_shaders
    Reloading shaders...
    ✓ Compiled 42 shaders in 128ms
    ! Warning: shader_lib/lighting.hlsl has 2 warnings

reload_scripts
  Hot-reload all Lua scripts from disk. Scene script remains in memory;
  only reloaded after next scene load.

  Usage: reload_scripts

  Example:
    > reload_scripts
    ✓ Reloaded 18 Lua scripts
    ! Script error in scripts/enemy_ai.lua line 42: division by zero
```

### Playback Control

```
set_timescale <float>
  Adjust game simulation speed (0.0 = paused, 1.0 = normal, 2.0 = 2x speed).

  Usage: set_timescale <float>

  Arguments:
    float — Timescale multiplier (0.0–10.0)

  Example:
    > set_timescale 0.5
    ✓ Timescale set to 0.5x (slow motion)

    > set_timescale 1.0
    ✓ Timescale set to 1.0x (normal speed)

    > set_timescale 10
    ✓ Timescale set to 10.0x (fast-forward)

    > set_timescale 0
    ✓ Timescale set to 0.0 (paused)
```

### Debug Visualization

```
toggle_physics_debug
  Show/hide physics collision wireframes in the viewport.

  Usage: toggle_physics_debug

  Example:
    > toggle_physics_debug
    Physics debug visualization: ON
    (Shows all rigidbody colliders, kinematic bodies, triggers as wireframes)

toggle_wireframe
  Enable/disable wireframe rendering mode.

  Usage: toggle_wireframe

  Example:
    > toggle_wireframe
    Wireframe mode: ON
    (All geometry rendered as wireframe; useful for topology inspection)

toggle_normals
  Show/hide vertex normal vectors as small lines from each vertex.

  Usage: toggle_normals

  Example:
    > toggle_normals
    Normal visualization: ON
```

### Scene Manipulation

```
spawn <prefab_path> [x] [y] [z]
  Spawn a prefab instance at a world position.

  Usage: spawn <prefab_path> [x] [y] [z]

  Arguments:
    prefab_path — Path to prefab (e.g., "res://prefabs/item_potion.uzprefab")
    x, y, z     — World position (optional; defaults to origin)

  Example:
    > spawn res://prefabs/item_health.uzprefab 10 5 20
    ✓ Spawned prefab at (10, 5, 20) → Node id=1024

kill <node_id>
  Remove a node from the scene (and destroy its children).

  Usage: kill <node_id>

  Arguments:
    node_id — Node ID to remove (see print_scene)

  Example:
    > kill 42
    ✓ Removed node 42 and 3 children

set_prop <node_id> <property> <value>
  Set a property on a node.

  Usage: set_prop <node_id> <property> <value>

  Arguments:
    node_id  — Node ID (from print_scene)
    property — Property name (e.g., "health", "enabled", "velocity")
    value    — New value (parsed as number, bool, or string)

  Example:
    > set_prop 10 health 50
    ✓ Player.health = 50

    > set_prop 10 enabled false
    ✓ Player.enabled = false

    > set_prop 100 name "NewName"
    ✓ Node[100].name = "NewName"
```

### Memory & Performance

```
stats
  Print current frame statistics: FPS, frame time, memory usage, draw calls.

  Usage: stats

  Example:
    > stats
    ═══════════════════════════════════════════════════════
    Performance:
      FPS: 144 (frame time: 6.9ms)
      GPU: RTX 3070 (8GB VRAM)

    Rendering:
      Draw calls: 156
      Triangles: 2.4M
      Vertices: 8.7M
      Textures loaded: 234

    Memory:
      Total arena: 512 MB / 2 GB
      Frame arena: 42 MB (8%)
      Level arena: 256 MB (50%)
      Permanent arena: 214 MB (42%)
      Lua heap: 8 MB
    ═══════════════════════════════════════════════════════

gc
  Force a Lua garbage collection cycle.

  Usage: gc

  Example:
    > gc
    Running Lua GC...
    ✓ Collected 8.2 MB (freed 156 objects)
```

---

## Lua API

Game scripts register commands via the `ctx.console` binding:

```lua
-- Register a simple command (no arguments)
ctx.console:register("god_mode", function(args)
    player.invincible = not player.invincible
    return "God mode: " .. tostring(player.invincible)
end)

-- Register a command with arguments
ctx.console:register("give_item <item_id> [count]", function(args)
    -- args[1] = item_id, args[2] = count (or nil if not provided)
    local item_id = args[1]
    local count = tonumber(args[2]) or 1

    if not inventory:can_add(item_id, count) then
        return "Inventory full!"
    end

    inventory:add(item_id, count)
    return "Added " .. count .. "x " .. item_id
end)

-- Register a command that returns rich output
ctx.console:register("list_inventory", function(args)
    local items = inventory:get_items()
    if #items == 0 then
        return "Inventory empty"
    end

    local output = "Inventory:\n"
    for i, item in ipairs(items) do
        output = output .. "  " .. i .. ". " .. item.name .. " (x" .. item.count .. ")\n"
    end
    return output
end)

-- Unregister a command
ctx.console:unregister("god_mode")

-- Get list of all registered commands
local commands = ctx.console:get_commands()
for name, info in pairs(commands) do
    print(name .. ": " .. info.description)
end
```

### Command Signature Format

When registering, the command name can include argument specification:

```lua
-- No arguments
ctx.console:register("god_mode", callback)

-- Required argument
ctx.console:register("give_item <item_id>", callback)

-- Multiple required arguments
ctx.console:register("teleport <x> <y> <z>", callback)

-- Optional argument (in square brackets)
ctx.console:register("spawn_enemy <prefab> [count]", callback)

-- Mix of required and optional
ctx.console:register("set_weather <weather_type> [intensity]", callback)
```

The argument specification is used for help text and auto-complete suggestions.

---

## C++ API

Game code can also register commands in C++ (less common, but available):

```cpp
// src/game/console_commands.h

#include "core/console.h"

namespace console {

// Register a command (returns void, outputs via return value)
void register_command(std::string_view name,
                      std::string_view signature,  // "give_item <item_id> [count]"
                      std::function<std::string(std::span<std::string_view>)> handler);

// Helper macro for brevity
#define CONSOLE_CMD(name, sig, body) \
    console::register_command(name, sig, [](std::span<std::string_view> args) -> std::string { body })

}  // namespace console
```

**Example**:
```cpp
// In game.dll initialization
CONSOLE_CMD("god_mode", "god_mode", {
    g_player.invincible = !g_player.invincible;
    return "God mode: " + std::string(g_player.invincible ? "ON" : "OFF");
});

CONSOLE_CMD("give_item", "give_item <item_id> [count]", {
    if (args.empty()) return "Usage: give_item <item_id> [count]";

    std::string item_id(args[0]);
    int count = args.size() > 1 ? std::stoi(std::string(args[1])) : 1;

    if (!g_inventory.can_add(item_id, count)) {
        return "Inventory full!";
    }
    g_inventory.add(item_id, count);
    return "Added " + std::to_string(count) + "x " + item_id;
});
```

---

## IPC Interface

The editor can execute console commands remotely via IPC:

### `console.execute`

Execute a command in the running game engine.

```json
{
    "fn": "console.execute",
    "args": {
        "command": "set_timescale 0.5"
    }
}
```

**Response**:
```json
{
    "ok": true,
    "output": "Timescale set to 0.5x (slow motion)",
    "command": "set_timescale 0.5",
    "timestamp": "2025-03-01T14:30:00Z"
}
```

**Error response**:
```json
{
    "ok": false,
    "error": "Unknown command: invalid_cmd",
    "suggestion": "Did you mean: reload_scene?"
}
```

### `console.get_commands`

List all registered commands (for editor autocomplete).

```json
{
    "fn": "console.get_commands",
    "args": {}
}
```

**Response**:
```json
{
    "ok": true,
    "commands": [
        {
            "name": "help",
            "signature": "help",
            "description": "Show all registered commands"
        },
        {
            "name": "quit",
            "signature": "quit",
            "description": "Exit the game"
        },
        {
            "name": "set_timescale",
            "signature": "set_timescale <float>",
            "description": "Set game timescale (0–10)"
        },
        ...
    ]
}
```

### `console.get_history`

Retrieve the last N executed commands and their outputs.

```json
{
    "fn": "console.get_history",
    "args": {
        "limit": 50
    }
}
```

**Response**:
```json
{
    "ok": true,
    "history": [
        {
            "command": "set_timescale 0.5",
            "output": "Timescale set to 0.5x",
            "timestamp": "2025-03-01T14:30:00Z",
            "success": true
        },
        {
            "command": "stats",
            "output": "FPS: 144...",
            "timestamp": "2025-03-01T14:30:05Z",
            "success": true
        },
        ...
    ]
}
```

---

## Auto-Complete

When the user presses `Tab` in the console, the engine provides suggestions:

1. **Command name completion**: `set_t[TAB]` → `set_timescale`
2. **Argument suggestions**: `spawn [TAB]` → shows list of prefab paths from the asset registry

The autocomplete system uses the registered command signatures to suggest argument types.

---

## Output Formatting

Commands return plain-text strings, which are displayed in the console overlay:

- **Simple success**: `"Timescale set to 0.5x"`
- **Multiline output**: Use `\n` for line breaks
- **Error messages**: Commands can return descriptive errors (no exceptions)
- **Rich output**: For complex data, JSON strings are pretty-printed

**Example**:
```lua
return "Inventory:\n  1. Health Potion (x5)\n  2. Mana Potion (x3)"
```

Renders as:
```
> list_inventory
Inventory:
  1. Health Potion (x5)
  2. Mana Potion (x3)
```

---

## Safety

### Shipping Builds

In **Release** and **Shipping** builds:
- Console is **not rendered** to the viewport
- IPC `console.*` handlers are **disabled** or return errors
- Only a whitelist of "safe" built-in commands are enabled (if at all)

### Debug Builds

In **Debug** builds:
- Full console with all commands
- All IPC handlers enabled
- Can execute arbitrary commands

### Game-Defined Commands

Game developers decide which commands are safe for shipping:

```lua
-- In game.dll, only register certain commands in shipping
if SHIPPING_BUILD then
    -- Only console commands safe for players
    ctx.console:register("stats", callback_safe_stats)
else
    -- Full debug suite
    ctx.console:register("god_mode", callback_god_mode)
    ctx.console:register("spawn", callback_spawn)
    -- ...
end
```

---

## Implementation Details

### `ConsoleCommandRegistry` (C++ Singleton)

```cpp
// src/runtime/console.h

class ConsoleCommandRegistry {
public:
    static ConsoleCommandRegistry& get();

    void register_command(std::string_view name,
                          std::string_view signature,
                          CommandHandler handler);

    std::string execute(std::string_view command_line);

    void clear_history();
    std::vector<CommandEntry> get_history(int limit = 100);

    std::vector<std::string> autocomplete(std::string_view partial);
    std::vector<CommandInfo> get_all_commands() const;

private:
    std::unordered_map<std::string, CommandInfo> m_commands;
    std::vector<CommandEntry> m_history;

    CommandInfo* find_command(std::string_view name);
    std::vector<std::string_view> parse_args(std::string_view line);
};
```

### Lua Binding

```cpp
// src/runtime/lua_console_api.h

void register_console_api(lua::State& lua) {
    auto console_table = lua.createTable();

    // console.register(name_with_sig, callback)
    console_table["register"] = [](lua::State& lua) {
        std::string name_with_sig = lua.getArgument<std::string>(1);
        lua::Function callback = lua.getArgument<lua::Function>(2);

        ConsoleCommandRegistry::get().register_command(
            name_with_sig,
            name_with_sig,
            [callback](std::span<std::string_view> args) -> std::string {
                // Call Lua callback, convert result to string
                auto result = callback(args);
                return lua::to_string(result);
            }
        );
        return 0;  // no return value
    };

    // ... other methods

    lua["console"] = console_table;
}
```

---

## Command Registration Lifecycle

1. **Engine startup**: Built-in commands registered in `ConsoleCommandRegistry::init()`
2. **Scene load**: Game script `on_scene_load()` calls `ctx.console:register()` for game-specific commands
3. **Runtime**: User can execute commands via console overlay or IPC
4. **Scene unload**: Game-registered commands remain; only removed explicitly or on game shutdown

---

## Performance

Executing a console command is an **off-frame operation**:
- Command dispatch happens on the Main thread
- Results are buffered and displayed next frame
- Heavy commands (spawn, reload_scene) may cause a frame drop, but don't block the main loop

---

## Diagnostics & Debugging

The console is invaluable for:
- **Live state inspection**: `print_scene`, `stats`
- **Quick experiments**: `set_timescale`, `spawn`, `kill`
- **Hot-reload feedback**: `reload_shaders`, `reload_scripts`
- **AI-assisted debugging**: Claude can run commands via IPC to inspect/modify game state

---

## Related Documentation

- [../../guides/ipc-mcp.md](../../guides/ipc-mcp.md) — IPC protocol, console.execute handler
- [../lua-scripting.md](../lua-scripting.md) — Lua API patterns, script lifecycle
- [../debug-profiling.md](../debug-profiling.md) — Assert system, profiling overlays

