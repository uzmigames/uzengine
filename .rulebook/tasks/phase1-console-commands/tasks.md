# Phase 1 — Debug Console Commands: Implementation Tasks

## Deliverables

- `ConsoleCommandRegistry` — C++ singleton for command registration and dispatch
- Built-in commands — `help`, `quit`, `reload_scene`, `reload_shaders`, `reload_scripts`, `set_timescale`, `stats`, `spawn`, `kill`, `set_prop`, `print_scene`, `gc`, etc.
- In-game console overlay — ~-key toggle, input handling, output display
- Lua console bindings — `ctx.console` API (register, execute, get_history, get_commands)
- IPC handlers — `console.execute`, `console.get_commands`, `console.get_history`
- Auto-complete — Tab key completes command names and arguments
- Hot-reload support — Console remains open across script reloads
- Test coverage: 85%+

---

## Task Breakdown

### 1. Create `ConsoleCommandRegistry` (C++ Singleton)

**Responsibility**: Register commands, parse/execute command lines, maintain history

**API**:
```cpp
class ConsoleCommandRegistry {
public:
    static ConsoleCommandRegistry& get();

    void register_command(std::string_view name,
                          std::string_view signature,  // "give_item <item_id> [count]"
                          std::function<std::string(std::span<std::string_view>)> handler);

    std::string execute(std::string_view command_line);

    std::vector<CommandInfo> get_all_commands() const;
    std::vector<CommandEntry> get_history(int limit = 100);
    std::vector<std::string> autocomplete(std::string_view partial);

    void clear_history();
};
```

**Subtasks**:
1. Command registration
   - Store commands in `std::unordered_map<name, CommandInfo>`
   - Parse signature to extract argument specs
   - Generate help text from signature

2. Command execution
   - Parse command line: `command_name arg1 arg2 ...`
   - Handle quoted arguments (can contain spaces)
   - Parse numbers vs. strings
   - Look up command handler
   - Invoke handler with args
   - Catch and format exceptions

3. Command history
   - Ring buffer of last 100 commands + outputs
   - Trim old entries
   - Query for get_history IPC

4. Auto-complete
   - Command name prefix matching
   - Suggest matching command names
   - For certain commands (spawn, set_prop), suggest asset/node names

5. Error handling
   - Unknown command → suggest similar commands (Levenshtein distance)
   - Missing required arguments → show usage
   - Argument parse errors → report clearly

**Files**:
- `src/runtime/console.h/cpp` — Registry implementation
- `src/runtime/console_test.cpp` — Unit tests

**Acceptance criteria**:
- Command registration works
- `execute()` calls handler correctly
- Arguments parsed (quoted, unquoted, numbers)
- History maintained (max 100 entries)
- Auto-complete suggests commands
- 85% test coverage

---

### 2. Built-In Commands

**Responsibility**: Implement all engine-native commands

**Commands to implement**:

| Command | Args | Purpose |
|---------|------|---------|
| `help` | — | List all registered commands |
| `quit` | — | Exit game |
| `reload_scene` | — | Reload current scene from disk |
| `reload_shaders` | — | Hot-reload all HLSL shaders |
| `reload_scripts` | — | Hot-reload all Lua scripts |
| `set_timescale <float>` | 0.0–10.0 | Set game simulation speed |
| `stats` | — | Print FPS, memory, draw calls |
| `print_scene [filter]` | optional filter | Dump scene tree to console |
| `gc` | — | Force Lua garbage collection |
| `spawn <prefab> [x] [y] [z]` | prefab path, optional coords | Spawn prefab instance |
| `kill <node_id>` | node ID | Remove node from scene |
| `set_prop <node> <prop> <value>` | node ID, prop name, value | Set node property |
| `toggle_physics_debug` | — | Show/hide physics wireframes |
| `toggle_wireframe` | — | Toggle wireframe rendering |
| `toggle_normals` | — | Show/hide vertex normals |

**Subtasks**:
1. Help command
   - List all registered commands
   - Show signature and description
   - Format as table or list

2. Scene commands
   - Implement `reload_scene`: SceneManager::unload() + load()
   - Implement `print_scene`: Walk node tree, format as tree output
   - Implement `spawn`: SceneManager::spawn_prefab(path, pos)
   - Implement `kill`: SceneManager::remove_node(id)
   - Implement `set_prop`: Node::set_property(name, value_str)

3. Playback commands
   - Implement `set_timescale`: Set global TimeManager::timescale
   - Implement `stats`: Query current FPS, memory, draw calls

4. Shader/script commands
   - Implement `reload_shaders`: ShaderManager::reload_all()
   - Implement `reload_scripts`: LuaState::reload_all_scripts()

5. Debug visualization commands
   - Implement `toggle_physics_debug`: PhysicsDebugDraw::set_enabled()
   - Implement `toggle_wireframe`: Renderer::set_wireframe_mode()
   - Implement `toggle_normals`: Renderer::set_normal_visualization()

6. Memory commands
   - Implement `gc`: Call `lua_gc(L, LUA_GCCOLLECT, 0)`

7. Graceful shutdown
   - Implement `quit`: Set engine running flag to false

**Files**:
- `src/runtime/builtin_commands.h/cpp` — All built-in command implementations
- `src/runtime/builtin_commands_test.cpp` — Tests for each command

**Acceptance criteria**:
- All commands execute without crashes
- Commands return meaningful output
- Scene inspection (`print_scene`) works correctly
- Property setting (`set_prop`) updates scene state
- Performance commands report accurate data
- 85% test coverage

---

### 3. In-Game Console Overlay

**Responsibility**: Render console UI, capture input, display output

**Subtasks**:
1. Console panel UI
   - ImGui panel (or custom canvas for headless builds)
   - Input text field (multiline or single-line)
   - Output display (scrollable text area)
   - Command history display (bottom of panel)

2. Input handling
   - ~-key toggle (opens/closes console)
   - Enter executes command
   - Tab auto-completes
   - Up/Down arrows navigate history
   - Escape closes console
   - Backspace/Delete edit input

3. Input capture
   - When console open: capture all input (game world gets nothing)
   - When console closed: input passes through to game

4. Output rendering
   - Display command output (success/error messages)
   - Color-code output (green for success, red for errors, white for info)
   - Scroll to bottom on new output
   - Trim old output (keep last 500 lines)

5. Visibility control
   - Console only visible in Debug and Editor builds
   - Hidden in Release/Shipping builds
   - Can be toggled with ~-key

**Files**:
- `src/ui/console_overlay.h/cpp` — ImGui panel or custom rendering
- `src/ui/console_overlay_test.cpp` — Tests for input handling

**Acceptance criteria**:
- Console opens/closes on ~-key press
- Input field captures typing
- Enter executes command
- Tab auto-completes
- History navigation works (Up/Down arrows)
- Output displays correctly
- Performance: < 1ms per frame to render console
- No impact on game when closed

---

### 4. Lua Console Bindings

**Responsibility**: Expose console API to game scripts

**API**:
```lua
-- Register a command (game script calls this on startup)
ctx.console:register("god_mode", function(args)
    player.invincible = not player.invincible
    return "God mode: " .. tostring(player.invincible)
end)

-- Register with argument signature
ctx.console:register("give_item <item_id> [count]", function(args)
    local item_id = args[1]
    local count = tonumber(args[2]) or 1
    return "Added " .. item_id
end)

-- Unregister a command
ctx.console:unregister("god_mode")

-- Execute a command programmatically
local result = ctx.console:execute("set_timescale 0.5")

-- Get all registered commands
local commands = ctx.console:get_commands()

-- Get command history
local history = ctx.console:get_history(50)
```

**Subtasks**:
1. Lua binding layer
   - Create `lua_console_api.h/cpp`
   - Register `console` table with all functions
   - Bridge between Lua callbacks and ConsoleCommandRegistry

2. Command registration from Lua
   - `register(name_with_sig, callback)` → wraps Lua callback, registers with registry
   - Argument unpacking (Lua table → C++ span<string_view>)
   - Result formatting (Lua return value → string)

3. Event binding
   - Event firing: C++ command execution → Lua callback invocation
   - Argument passing: C++ args → Lua varargs or table

4. Error handling
   - Lua errors in callbacks caught and logged
   - Return error message from callback
   - Don't crash on bad Lua code

**Files**:
- `src/scripting/lua_console_api.h/cpp` — Binding implementation
- `src/scripting/lua_console_api_test.cpp` — Tests

**Acceptance criteria**:
- All `ctx.console` functions callable from Lua
- Commands registered from Lua callable from console
- Arguments passed correctly to Lua callbacks
- Lua errors handled gracefully
- 85% test coverage

---

### 5. IPC Handlers

**Responsibility**: Expose console to VSCode editor via IPC

**Handlers**:
1. `console.execute` — Execute a command
   ```json
   {"fn": "console.execute", "args": {"command": "set_timescale 0.5"}}
   ```
   Response:
   ```json
   {"ok": true, "output": "Timescale set to 0.5x", "command": "..."}
   ```

2. `console.get_commands` — List all registered commands
   ```json
   {"fn": "console.get_commands", "args": {}}
   ```
   Response:
   ```json
   {
     "ok": true,
     "commands": [
       {"name": "help", "signature": "help", "description": "..."},
       ...
     ]
   }
   ```

3. `console.get_history` — Get command history
   ```json
   {"fn": "console.get_history", "args": {"limit": 50}}
   ```
   Response:
   ```json
   {
     "ok": true,
     "history": [
       {"command": "...", "output": "...", "timestamp": "...", "success": true},
       ...
     ]
   }
   ```

**Subtasks**:
1. Register IPC handlers
   - Call `ipc_registry_.register()` for each handler
   - Dispatch based on JSON `fn` field

2. Handler implementations
   - `console.execute`: Call `ConsoleCommandRegistry::get().execute()`
   - `console.get_commands`: Iterate registry, build JSON array
   - `console.get_history`: Query history, format with timestamps

3. Error handling
   - Invalid command → return JSON error with suggestion
   - Command execution error → return error in output field

**Files**:
- IPC handler registration in main server code
- Handlers implemented in `src/runtime/console_ipc.h/cpp`

**Acceptance criteria**:
- All three IPC handlers registered and functional
- JSON serialization correct
- Error responses include helpful messages
- Editor can call handlers without crashing engine

---

### 6. Auto-Complete

**Responsibility**: Suggest command names and arguments as user types

**Subtasks**:
1. Command name completion
   - User types: `set_t[TAB]`
   - Engine suggests: `set_timescale`
   - Use prefix matching (case-insensitive)

2. Argument suggestions
   - For `spawn [TAB]`: suggest prefab paths from asset registry
   - For `set_prop <node> [TAB]`: suggest node IDs in scene
   - For `set_prop <node> <prop> [TAB]`: suggest properties on that node

3. Display suggestions
   - Show in console overlay as dropdown or list
   - Allow arrow keys to select
   - Tab or Enter accepts selection

**Files**:
- Auto-complete logic in `ConsoleCommandRegistry`
- UI rendering in console overlay

**Acceptance criteria**:
- Command name completion works
- Argument suggestions work (at least for spawn)
- UI responsive and doesn't block game

---

### 7. Documentation

**Subtasks**:
1. Spec (already done): `docs/specs/console-commands.md`

2. Quick reference
   - Create `docs/guides/console-reference.md` with all built-in commands
   - Brief description, usage, examples

3. API documentation
   - Lua `ctx.console` API
   - C++ `ConsoleCommandRegistry` API

**Files**:
- `docs/guides/console-reference.md` — Command reference

**Acceptance criteria**:
- All commands documented
- Usage examples provided
- API reference clear and complete

---

### 8. Testing

**Test coverage target**: 85%+

**Test types**:

1. **Unit tests** (doctest)
   - Command registration (duplicate names, invalid names)
   - Argument parsing (quoted, unquoted, numbers)
   - Command execution (success, error, not found)
   - History management (max 100 entries)
   - Auto-complete (prefix matching)

2. **Integration tests** (CTest)
   - Scene commands (`print_scene`, `spawn`, `kill`, `set_prop`)
   - Playback commands (`set_timescale`)
   - Shader/script reload commands
   - Lua registration and execution
   - IPC handlers

3. **Functional tests**
   - ~-key toggle opens/closes console
   - Tab key completes command names
   - Up/Down arrows navigate history
   - Enter executes command
   - Output displays correctly

4. **Performance tests**
   - Command execution < 1ms (most commands)
   - Console render < 1ms

**Files**:
- Test suites for each module
- Functional test script for key bindings

**Acceptance criteria**:
- 85%+ line coverage
- All tests pass
- No performance regressions
- Console is responsive under load

---

## Task Order

1. **ConsoleCommandRegistry** (week 1)
   - Registration, execution, parsing
   - Unit testing

2. **Built-in Commands** (week 2)
   - Implement all commands
   - Scene integration

3. **In-Game Console Overlay** (week 2–3)
   - ImGui panel or custom rendering
   - Input handling

4. **Lua Bindings** (week 3)
   - `ctx.console` API
   - Script registration

5. **IPC Handlers** (week 3)
   - Editor integration
   - JSON serialization

6. **Auto-Complete** (week 4)
   - Command name and argument suggestions
   - UI integration

7. **Testing & Docs** (week 4)
   - Comprehensive testing
   - Documentation

---

## Success Criteria

- [x] Spec complete (`docs/specs/console-commands.md`)
- [ ] ConsoleCommandRegistry functional
- [ ] All 13+ built-in commands working
- [ ] In-game console opens/closes on ~-key
- [ ] Tab auto-complete functional
- [ ] Lua `ctx.console:register()` works
- [ ] IPC handlers exposed to editor
- [ ] 85%+ test coverage
- [ ] Performance: console render < 1ms
- [ ] No regression in game performance when console closed

