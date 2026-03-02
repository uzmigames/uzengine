# Level Blueprint & Visual Event Graph Editor

> **Status**: Planned (Phase 2+)
> **Owner**: VSCode extension (graph editor WebView) + `game.dll` (Lua code generation target)
> **Output**: `.uzblueprint` (graph definition) + `.lua` (generated script)
> **Runtime**: LuaJIT 2.1 inside `game.dll` — generated Lua runs identically to hand-written scripts

---

## 1. Overview & Goals

The Level Blueprint Editor is a visual scripting system embedded in VSCode that allows game designers to author level logic — triggers, event sequences, timers, branching behavior — by connecting nodes in a graph rather than writing Lua code manually. The graph compiles to standard `.lua` script files that run in the LuaJIT VM inside `game.dll`.

### Design Principles

1. **Lua is the truth** — the visual graph is an authoring tool; the generated `.lua` file is the actual runtime artifact. If the `.lua` file is lost, it can be regenerated from the `.uzblueprint`. If the `.uzblueprint` is lost, the `.lua` file still works.
2. **No special runtime** — generated Lua uses the exact same `ctx` API, module pattern, timer system, and event bus as hand-written scripts. No interpreter, no VM bytecode format, no proprietary runtime.
3. **Coexistence with hand-written Lua** — designers can create the initial logic visually, then a programmer can extend the generated `.lua` file. A `-- [MANUAL]` marker separates generated and hand-written sections. The compiler preserves manual sections across recompilation.
4. **AI-friendly** — the `.uzblueprint` file format is plain-text INI (same as `.uzscene`), fully readable and writable by AI agents via MCP tools.

### Target Users

| Persona | How They Use Blueprints |
|---------|------------------------|
| **Level Designer** | Creates trigger zones, event sequences, door/switch logic, cutscene triggers — never writes Lua |
| **Game Designer** | Prototypes gameplay mechanics (damage zones, pickups, spawners) visually, hands off to programmer for optimization |
| **Programmer** | Reviews generated Lua, extends with manual code, creates custom function nodes for designers to use |
| **AI Agent** | Generates `.uzblueprint` files from natural language descriptions via MCP tools; reads graph structure for analysis |

### Non-Goals

- **Not a general-purpose programming language** — complex AI, state machines, and performance-critical code should be hand-written in Lua or C++
- **Not a shader graph** — shader authoring uses `.hlsl` files (see `specs/shader-system.md`)
- **Not a material editor** — material graphs are a separate system (future)

---

## 2. Editor Layout

The Level Blueprint Editor opens as a full-tab WebView panel in VSCode, activated from the Scene Tree context menu ("Edit Level Blueprint") or the command palette (`UzEngine: Open Level Blueprint`).

### Panel Layout

```
+===========================================================================+
| TOOLBAR                                                                    |
| [Compile] [Validate] [Clear All] [Zoom Fit] [Search Nodes] [Undo] [Redo] |
+============+==================================+===========================+
| EVENT      |                                  | DETAILS PANEL             |
| PANEL      |                                  |                           |
|            |                                  | -- Selected: Branch --    |
| OnBeginPlay|         GRAPH CANVAS             | Condition:                |
| OnTick     |                                  |   [Variable ▼] [health]   |
| OnTrigger..|    +---------+    +---------+    |   [Operator ▼] [ < ]     |
| OnKeyPress.|    | OnBegin |===>| Set Var |    |   [Value     ] [ 0 ]     |
| OnEntityDa.|    | Play    |    | speed=5 |    |                           |
| [Custom..] |    +---------+    +----+----+    | Pin Types:                |
|            |                        |         |   Exec In   (white)       |
+------------+               +--------+----+    |   Exec True (white)       |
| VARIABLES  |               | Print       |    |   Exec False(white)       |
|            |               | "ready!"    |    |   Condition (red/bool)    |
| doorOpen   |               +--------+----+    +===========================+
|  bool=false|                        |         | COMPILED LUA PREVIEW      |
| spawnCount |               +--------+----+    |                           |
|  int=0     |               | Branch     |    | local M = {}              |
| moveSpeed  |               | health < 0 |    |                           |
|  float=3.5 |               +--+------+--+    | function M:on_start(ctx)  |
| targetName |                  |      |        |   self.speed = 5          |
|  string="" |            True  |      | False  |   ctx:log("ready!")       |
| targetRef  |            +-----+  +---+----+   | end                       |
|  entity=nil|            |Die  |  |Heal    |   |                           |
|            |            +-----+  +--------+   | function M:on_update(...) |
+------------+                                  |   ...                     |
| NODE       |                                  | end                       |
| PALETTE    |                                  |                           |
|            |                                  | return M                  |
| [Search..] |                                  |                           |
| Flow       |                                  |                           |
|  Branch    |                                  |                           |
|  Sequence  |                                  |                           |
|  ForEach   |                                  |                           |
|  Delay     |                                  |                           |
| Entity     |                                  |                           |
|  GetByName |                                  |                           |
|  SetPos    |                                  |                           |
|  Destroy   |                                  |                           |
| Timer      |                                  |                           |
|  SetTimer  |                                  |                           |
|  Repeat    |                                  |                           |
| Audio      |                                  |                           |
|  PlaySound |                                  |                           |
| [+Custom]  |                                  |                           |
+============+==================================+===========================+
```

### Panel Descriptions

| Panel | Position | VSCode API | Description |
|-------|----------|------------|-------------|
| **Toolbar** | Top bar | WebView HTML | Compile, validate, layout, search, undo/redo buttons |
| **Graph Canvas** | Center (main area) | WebView + SVG/Canvas | Node graph with execution wires (white) and data wires (colored). Pan with middle-click drag, zoom with scroll wheel. Background grid snaps nodes to 16px increments |
| **Event Panel** | Left-top | WebView list | Lists all available event entry points. Drag an event onto the canvas to create an event node. Shows which events are already placed (checkmark) |
| **Variable Panel** | Left-mid | WebView list | Blueprint-scoped variables with type, name, default value. Drag a variable onto the canvas to create a Get or Set node |
| **Node Palette** | Left-bottom | WebView tree | Searchable, categorized list of all node types. Drag to create, or double-click to place at center of view. Fuzzy search with keyboard |
| **Details Panel** | Right-top | WebView form | Properties of the selected node. Changes are applied immediately and trigger recompilation |
| **Compiled Lua Preview** | Right-bottom (collapsible) | WebView `<pre>` | Read-only view of the generated Lua code, updated on every graph change. Syntax highlighted. Line numbers link back to graph nodes |

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Delete` / `Backspace` | Delete selected nodes and their connections |
| `Ctrl+C` / `Ctrl+V` | Copy/paste selected nodes (within same graph or across graphs) |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo/redo graph edits (separate undo stack from scene editor) |
| `Ctrl+F` | Focus search box in Node Palette |
| `Space` (on canvas) | Open quick-add menu at cursor (same as Node Palette search) |
| `F` | Zoom to fit all nodes |
| `Ctrl+S` | Save blueprint + recompile Lua |
| `Ctrl+Shift+E` | Toggle Compiled Lua Preview panel |
| Right-click (canvas) | Context menu: add node, add comment, align selection |
| Right-click (node) | Context menu: delete, duplicate, disable, collapse, breakpoint |

---

## 3. Execution Model

### Pin Types

The graph uses two categories of pins:

**Execution Pins (white triangles)** — control flow. A node executes only when its input exec pin is triggered. Output exec pins determine which node runs next.

**Data Pins (colored circles)** — data flow. Data pins carry values between nodes. A data pin is evaluated when the consuming node executes (pull-based evaluation).

### Pin Color Legend

| Type | Color | Example Values |
|------|-------|---------------|
| `bool` | Red | `true`, `false` |
| `int` | Green | `0`, `42`, `-1` |
| `float` | Blue | `0.0`, `3.14`, `-9.81` |
| `string` | Yellow | `"hello"`, `"Player"` |
| `entity` | Purple | NodeRef to a scene node |
| `vector3` | Cyan | `Vec3(1, 0, 0)` |
| `any` | Gray | Wildcard — accepts any type (with runtime coercion) |

### Data Pin Coercion Rules

| From | To | Rule |
|------|----|------|
| `int` | `float` | Implicit (lossless) |
| `float` | `int` | Implicit (truncates, warning in validator) |
| `bool` | `int` | `true` = 1, `false` = 0 |
| `int` | `bool` | 0 = `false`, non-zero = `true` |
| `int`/`float`/`bool` | `string` | `tostring()` |
| `string` | `int`/`float` | `tonumber()`, nil on failure (runtime error) |
| `entity` | `string` | `node.name` |
| `any` | All types | No coercion, passed as-is |

Incompatible connections (e.g., `vector3` to `bool`) are rejected at wire creation time — the wire snaps back and is not created. The cursor shows a red "X" indicator.

### Execution Flow

Execution follows the white exec wires from left to right. When an event fires, the engine calls into the generated Lua function, which executes nodes in topological order following the exec wire chain.

```
Event fires (e.g., OnBeginPlay)
    |
    v
[Node A] executes
    | exec-out wire to Node B
    v
[Node B] executes
    | exec-out wire to Branch node
    v
[Branch] evaluates condition data pin
    |           |
    | True      | False
    v           v
[Node C]    [Node D]
```

Data pins are **pull-evaluated**: when Node B needs the value of a data input, it follows the data wire backward to the source node and evaluates it. Pure nodes (no exec pins) are evaluated on-demand and cached per execution frame.

---

## 4. Event Nodes (Graph Entry Points)

Event nodes are the entry points of the graph — they have no exec input pin, only exec output pins. Each event node maps to a Lua lifecycle method or an EventBus subscription.

### Built-in Events

| Event Node | Exec Out Pins | Data Out Pins | Generated Lua Pattern |
|------------|---------------|---------------|----------------------|
| **OnBeginPlay** | `exec` | (none) | `M:on_start(ctx)` body |
| **OnTick** | `exec` | `dt` (float) | `M:on_update(ctx, dt)` body |
| **OnEndPlay** | `exec` | (none) | `M:on_stop(ctx)` body |
| **OnTriggerEnter** | `exec` | `other` (entity) | `ctx.events:subscribe("TriggerEnter", ...)` |
| **OnTriggerExit** | `exec` | `other` (entity) | `ctx.events:subscribe("TriggerExit", ...)` |
| **OnKeyPressed** | `exec` | `key` (string) | `if ctx.input:pressed(key) then ... end` in `on_update` |
| **OnKeyReleased** | `exec` | `key` (string) | `if ctx.input:released(key) then ... end` in `on_update` |
| **OnKeyHeld** | `exec` | `key` (string), `dt` (float) | `if ctx.input:held(key) then ... end` in `on_update` |
| **OnEntityDamaged** | `exec` | `entity` (entity), `amount` (float), `source` (entity) | `ctx.events:subscribe("EntityDamaged", ...)` |
| **OnSceneLoaded** | `exec` | `scene_id` (string) | `ctx.events:subscribe("SceneLoaded", ...)` |
| **OnTimerFired** | `exec` | `timer_name` (string) | Named timer callback |
| **Custom Event** | `exec` | user-defined data pins | `ctx.events:subscribe("UserEventName", ...)` |

### Custom Events

Designers can define custom events with arbitrary data pins. A custom event has:
- A **name** (string, unique within the blueprint)
- Zero or more **data output pins** (typed, named)

Custom events can be **called** from another part of the graph using the "Call Custom Event" action node, or emitted from external Lua scripts via `ctx.events:emit("EventName", data)`.

### Event Node Detail

```
+========================+
|  >> OnBeginPlay        |  << event icon (play triangle)
|========================|
|  [exec]  >-----------  |  << white exec output pin
+========================+

+========================+
|  >> OnTriggerEnter     |
|========================|
|  [exec]  >-----------  |  << white exec output
|  other   o-----------  |  << purple entity output
|  Tag Filter: [       ] |  << optional tag filter property
+========================+

+========================+
|  >> OnKeyPressed       |
|========================|
|  Key: [Space     ▼]   |  << dropdown in details panel
|  [exec]  >-----------  |
|  key     o-----------  |  << yellow string output
+========================+
```

### Multiple Event Handlers

A single blueprint can contain multiple event nodes of the same type. For example, two `OnKeyPressed` nodes — one for "Jump" and one for "Fire". Each generates a separate check in the `on_update` body:

```lua
function M:on_update(ctx, dt)
    -- OnKeyPressed: Jump
    if ctx.input:pressed("Jump") then
        -- ... jump logic nodes ...
    end

    -- OnKeyPressed: Fire
    if ctx.input:pressed("Fire") then
        -- ... fire logic nodes ...
    end
end
```

---

## 5. Action Nodes

Action nodes perform operations on the game world. They have exec input/output pins (they must be triggered) and optional data input/output pins.

### 5.1 Entity Nodes

| Node | Inputs | Outputs | Lua Code |
|------|--------|---------|----------|
| **Get Entity By Name** | `name` (string) | `entity` (entity) | `ctx.scene:find_node(name)` |
| **Get Entity By Id** | `id` (int) | `entity` (entity) | `ctx.scene:find_node_by_id(id)` |
| **Get Self** | (none) | `entity` (entity) | `self` (the node this blueprint is attached to) |
| **Get Child** | `entity` (entity), `name` (string) | `child` (entity) | `entity:get_child(name)` |
| **Get Parent** | `entity` (entity) | `parent` (entity) | `entity:get_parent()` |
| **Get Position** | `entity` (entity) | `position` (vector3) | `entity.position` |
| **Set Position** | `entity` (entity), `position` (vector3) | (exec out) | `entity.position = pos` |
| **Get Rotation** | `entity` (entity) | `rotation` (vector3) | `entity.rotation` |
| **Set Rotation** | `entity` (entity), `rotation` (vector3) | (exec out) | `entity.rotation = rot` |
| **Set Scale** | `entity` (entity), `scale` (vector3) | (exec out) | `entity.scale = scale` |
| **Set Enabled** | `entity` (entity), `enabled` (bool) | (exec out) | `entity.enabled = val` |
| **Is Valid** | `entity` (entity) | `valid` (bool) | `entity:is_valid()` |
| **Destroy Entity** | `entity` (entity) | (exec out) | `ctx.scene:remove_node(entity)` |
| **Spawn Entity** | `prefab` (string), `position` (vector3), `parent` (entity) | `spawned` (entity), (exec out) | `ctx.scene:instantiate(prefab, parent)` then set position |
| **Has Tag** | `entity` (entity), `tag` (string) | `result` (bool) | `entity:has_tag(tag)` |

### 5.2 Timer Nodes

| Node | Inputs | Outputs | Lua Code |
|------|--------|---------|----------|
| **Delay** | `duration` (float) | (exec out, fires after delay) | `ctx:schedule_after(duration, function() ... end)` |
| **Set Timer** | `name` (string), `duration` (float) | `timer_id` (int), (exec out immediate) | `ctx:schedule_after(dur, callback)` |
| **Set Repeating Timer** | `name` (string), `interval` (float) | `timer_id` (int), (exec out immediate) | `ctx:schedule_every(interval, callback)` |
| **Cancel Timer** | `timer_id` (int) | (exec out) | `ctx:cancel_timer(timer_id)` |
| **Schedule Next Frame** | (none) | (exec out, fires next frame) | `ctx:schedule_next_frame(function() ... end)` |

### 5.3 Audio Nodes

| Node | Inputs | Outputs | Lua Code |
|------|--------|---------|----------|
| **Play Sound 2D** | `clip` (string), `bus` (string), `volume` (float) | (exec out) | `ctx.audio:play_2d(clip, bus, volume)` |
| **Play Sound At** | `clip` (string), `position` (vector3), `bus` (string), `volume` (float) | (exec out) | `ctx.audio:play_at(clip, pos, bus, volume)` |
| **Stop All Sounds** | (none) | (exec out) | `ctx.audio:stop_all()` |
| **Set Bus Volume** | `bus` (string), `volume` (float) | (exec out) | `ctx.audio:set_bus_volume(bus, volume)` |

### 5.4 Camera Nodes

| Node | Inputs | Outputs | Lua Code |
|------|--------|---------|----------|
| **Set Camera Target** | `camera` (entity), `target` (entity) | (exec out) | `camera.target = target` |
| **Camera Shake** | `intensity` (float), `duration` (float) | (exec out) | `ctx.camera:shake(intensity, duration)` |
| **Set Camera FOV** | `camera` (entity), `fov` (float) | (exec out) | `camera.fov = fov` |

### 5.5 Physics Nodes

| Node | Inputs | Outputs | Lua Code |
|------|--------|---------|----------|
| **Apply Force** | `entity` (entity), `force` (vector3) | (exec out) | `entity:apply_force(force)` |
| **Apply Impulse** | `entity` (entity), `impulse` (vector3) | (exec out) | `entity:apply_impulse(impulse)` |
| **Set Velocity** | `entity` (entity), `velocity` (vector3) | (exec out) | `entity:set_velocity(velocity)` |
| **Raycast** | `origin` (vector3), `direction` (vector3), `distance` (float) | `hit` (bool), `point` (vector3), `normal` (vector3), `entity` (entity) | `ctx.world:raycast(origin, dir, dist)` |
| **Overlap Sphere** | `center` (vector3), `radius` (float) | `entities` (entity[]) | `ctx.world:overlap_sphere(center, radius)` |

### 5.6 Scene Nodes

| Node | Inputs | Outputs | Lua Code |
|------|--------|---------|----------|
| **Load Scene** | `path` (string) | (exec out) | `ctx.scene:load(path)` |
| **Reload Scene** | (none) | (exec out) | `ctx.scene:reload()` |

### 5.7 UI Nodes

| Node | Inputs | Outputs | Lua Code |
|------|--------|---------|----------|
| **Show UI** | `panel` (string) | (exec out) | `ctx.ui:show(panel)` |
| **Hide UI** | `panel` (string) | (exec out) | `ctx.ui:hide(panel)` |
| **Set UI Property** | `panel` (string), `property` (string), `value` (any) | (exec out) | `ctx.ui:set(panel, property, value)` |

### 5.8 Event Nodes (Emitting)

| Node | Inputs | Outputs | Lua Code |
|------|--------|---------|----------|
| **Emit Event** | `name` (string), `data` (any) | (exec out) | `ctx.events:emit(name, data)` |
| **Call Custom Event** | `name` (string), data pins matching target event | (exec out) | Calls the custom event handler function |

### 5.9 Utility Nodes

| Node | Inputs | Outputs | Lua Code |
|------|--------|---------|----------|
| **Log** | `message` (string) | (exec out) | `ctx:log(message)` |
| **Log Warning** | `message` (string) | (exec out) | `ctx:log_warn(message)` |
| **Log Error** | `message` (string) | (exec out) | `ctx:log_error(message)` |
| **Console Execute** | `command` (string) | `result` (string), (exec out) | `ctx.console:execute(command)` |
| **Execute Lua** | `code` (string) | `result` (any), (exec out) | Raw Lua string executed via `pcall(load(code))` (escape hatch, editor-only) |

### 5.10 Math / Pure Nodes (No Exec Pins)

Pure nodes have no exec pins. They are evaluated on-demand when a connected node needs their data.

| Node | Inputs | Outputs |
|------|--------|---------|
| **Add** | `a` (float), `b` (float) | `result` (float) |
| **Subtract** | `a` (float), `b` (float) | `result` (float) |
| **Multiply** | `a` (float), `b` (float) | `result` (float) |
| **Divide** | `a` (float), `b` (float) | `result` (float) |
| **Negate** | `value` (float) | `result` (float) |
| **Abs** | `value` (float) | `result` (float) |
| **Clamp** | `value` (float), `min` (float), `max` (float) | `result` (float) |
| **Lerp** | `a` (float), `b` (float), `t` (float) | `result` (float) |
| **Random Float** | `min` (float), `max` (float) | `result` (float) |
| **Random Int** | `min` (int), `max` (int) | `result` (int) |
| **Make Vec3** | `x` (float), `y` (float), `z` (float) | `result` (vector3) |
| **Break Vec3** | `vector` (vector3) | `x` (float), `y` (float), `z` (float) |
| **Vec3 Length** | `vector` (vector3) | `length` (float) |
| **Vec3 Normalized** | `vector` (vector3) | `result` (vector3) |
| **Vec3 Distance** | `a` (vector3), `b` (vector3) | `distance` (float) |
| **Vec3 Lerp** | `a` (vector3), `b` (vector3), `t` (float) | `result` (vector3) |
| **Dot Product** | `a` (vector3), `b` (vector3) | `result` (float) |
| **Cross Product** | `a` (vector3), `b` (vector3) | `result` (vector3) |
| **Equal** | `a` (any), `b` (any) | `result` (bool) |
| **Not Equal** | `a` (any), `b` (any) | `result` (bool) |
| **Greater Than** | `a` (float), `b` (float) | `result` (bool) |
| **Less Than** | `a` (float), `b` (float) | `result` (bool) |
| **And** | `a` (bool), `b` (bool) | `result` (bool) |
| **Or** | `a` (bool), `b` (bool) | `result` (bool) |
| **Not** | `value` (bool) | `result` (bool) |
| **To String** | `value` (any) | `result` (string) |
| **To Number** | `value` (string) | `result` (float) |
| **String Concat** | `a` (string), `b` (string) | `result` (string) |
| **String Format** | `format` (string), `args` (any[]) | `result` (string) |

---

## 6. Flow Control Nodes

Flow control nodes have exec pins and determine which downstream nodes execute.

### 6.1 Branch (If/Else)

```
        +===============+
 exec-->|    Branch     |
        |===============|
  cond o-|  condition   |
        |---------------|
        | True  >-------|-->  exec (true path)
        | False >-------|-->  exec (false path)
        +===============+
```

**Generated Lua:**
```lua
if condition then
    -- true path nodes
else
    -- false path nodes
end
```

### 6.2 Sequence

Executes outputs A, B, C in order. All outputs fire unconditionally.

```
        +===============+
 exec-->|   Sequence    |
        |===============|
        | Then 0 >------|-->  first
        | Then 1 >------|-->  second
        | Then 2 >------|-->  third
        | [+ Add Pin]   |
        +===============+
```

**Generated Lua:**
```lua
-- Then 0
first_node_code()
-- Then 1
second_node_code()
-- Then 2
third_node_code()
```

### 6.3 For Each Loop

Iterates over an array. The `element` data pin provides the current item; `index` provides the 1-based index.

```
        +==================+
 exec-->|    For Each      |
        |==================|
 array o-|  array          |
        |------------------|
        | Loop Body >------|-->  exec (per element)
        | element   o------|-->  data (current element)
        | index     o------|-->  data (int, 1-based)
        | Completed >------|-->  exec (after all iterations)
        +==================+
```

**Generated Lua:**
```lua
for __idx, __elem in ipairs(array) do
    -- loop body nodes, with __elem and __idx available
end
-- completed path nodes
```

### 6.4 While Loop

Repeats the body while the condition is true. Maximum iteration guard prevents infinite loops (default: 10000, configurable in Details Panel).

```
        +==================+
 exec-->|    While Loop    |
        |==================|
  cond o-|  condition      |
        |  Max: [10000]    |
        |------------------|
        | Loop Body >------|-->  exec (per iteration)
        | Completed >------|-->  exec (when condition becomes false)
        +==================+
```

**Generated Lua:**
```lua
local __while_guard = 0
while condition and __while_guard < 10000 do
    __while_guard = __while_guard + 1
    -- loop body nodes
end
-- completed path nodes
```

### 6.5 Gate

A gate can be opened or closed. When open, execution passes through. When closed, execution is blocked. Toggling is done via the `Open`, `Close`, and `Toggle` exec inputs.

```
        +==================+
 Enter->|      Gate        |
  Open->|                  |
 Close->|                  |
Toggle->|                  |
        |  Start: [Open ▼] |
        |------------------|
        | Exit   >---------|-->  exec (when gate is open and Enter fires)
        +==================+
```

**Generated Lua:**
```lua
-- State variable
self.__gate_1 = true  -- starts open

-- On Enter (only if gate is open)
if self.__gate_1 then
    -- exit path nodes
end

-- Open/Close/Toggle modify self.__gate_1
```

### 6.6 Flip Flop

Alternates between two outputs on each call.

```
        +==================+
 exec-->|   Flip Flop      |
        |==================|
        | A      >---------|-->  exec (odd calls: 1st, 3rd, 5th...)
        | B      >---------|-->  exec (even calls: 2nd, 4th, 6th...)
        | Is A   o---------|-->  bool (true when A was last fired)
        +==================+
```

**Generated Lua:**
```lua
self.__flipflop_1 = not self.__flipflop_1
if self.__flipflop_1 then
    -- A path nodes
else
    -- B path nodes
end
```

### 6.7 Do Once

Executes only on the first call. Subsequent calls are ignored unless reset.

```
        +==================+
 exec-->|    Do Once       |
 Reset->|                  |
        |==================|
        | Completed >------|-->  exec (first time only)
        +==================+
```

**Generated Lua:**
```lua
if not self.__doonce_1 then
    self.__doonce_1 = true
    -- completed path nodes
end

-- Reset sets self.__doonce_1 = false
```

### 6.8 Do N Times

Executes the first N calls, then stops. Has a counter output.

```
        +==================+
 exec-->|   Do N Times     |
 Reset->|                  |
        |  N: [3         ] |
        |==================|
        | Exec    >--------|-->  exec (first N calls)
        | Counter o--------|-->  int (current count, 1-based)
        | Exceeded>--------|-->  exec (calls after N)
        +==================+
```

### 6.9 Delay

Pauses execution flow for a duration. Uses the timer system (`ctx:schedule_after`). Everything downstream of the Delay node fires asynchronously after the delay.

```
        +==================+
 exec-->|     Delay        |
        |==================|
  dur o-|  duration (sec)  |
        |------------------|
        | Completed >------|-->  exec (fires after delay)
        +==================+
```

**Generated Lua:**
```lua
ctx:schedule_after(duration, function()
    -- completed path nodes (captured as closure)
end)
```

**Important**: Nodes after a Delay run in a timer callback, not in the original `on_update`/`on_start` call. The generated code must capture all needed state in the closure. The compiler handles this automatically.

---

## 7. Variable System

### Variable Types

| Type | Pin Color | Lua Type | Default |
|------|-----------|----------|---------|
| `bool` | Red | `boolean` | `false` |
| `int` | Green | `number` (integer) | `0` |
| `float` | Blue | `number` | `0.0` |
| `string` | Yellow | `string` | `""` |
| `Vector3` | Cyan | `Vec3` userdata | `Vec3(0,0,0)` |
| `Entity Reference` | Purple | `NodeRef` userdata | `nil` |
| `Array` | White (striped) | Lua table (array) | `{}` |

### Variable Declaration

Variables are declared in the Variable Panel. Each variable has:
- **Name**: alphanumeric + underscore, must be unique within the blueprint
- **Type**: one of the types above
- **Default Value**: set in the variable panel, applied in `on_start`
- **Exposed**: checkbox. If true, the variable appears in the Inspector panel when the blueprint node is selected in the Scene Tree, allowing designers to override the default without opening the graph

### Get / Set Variable Nodes

Dragging a variable onto the canvas creates a choice dialog: "Get" or "Set".

**Get Variable** (pure node, no exec pins):
```
+================+
| Get: doorOpen  |
|================|
| value o--------|-->  bool
+================+
```

**Set Variable** (exec node):
```
        +================+
 exec-->| Set: doorOpen  |
        |================|
  val o-|  value         |
        |----------------|
        | exec   >-------|-->
        +================+
```

**Generated Lua** (variables become `self` fields):
```lua
function M:on_start(ctx)
    self.doorOpen = false      -- default from variable declaration
    self.spawnCount = 0
    self.moveSpeed = 3.5
    self.targetName = ""
    self.targetRef = nil
end

-- Get: doorOpen → self.doorOpen
-- Set: doorOpen → self.doorOpen = value
```

### Exposed Variables

When a variable is marked "Exposed", it is declared as a custom property in the scene node and read in `on_start`:

```lua
function M:on_start(ctx)
    -- Read exposed variables from scene properties (with defaults)
    self.moveSpeed = self.moveSpeed or 3.5
    self.targetName = self.targetName or ""
end
```

The `.uzscene` file can then override the default:

```ini
[node id=20 type=Node3D name="DoorTrigger" parent=1]
script    = "res://game/level_blueprint.lua"
moveSpeed = 5.0
targetName = "BossDoor"
```

---

## 8. Function Graphs

### Function Definitions

A blueprint can contain multiple **function sub-graphs** in addition to the main event graph. Functions are defined in a separate tab within the editor (tabs at the top of the graph canvas: `[Event Graph] [OpenDoor] [SpawnWave] [+]`).

Each function has:
- A **name** (alphanumeric + underscore)
- Zero or more **input parameters** (typed, named)
- Zero or one **return value** (typed)
- Its own **graph canvas** with a single entry node and optional return nodes

### Function Entry and Return Nodes

```
+======================+                    +================+
| >> Function: OpenDoor|                    |     Return     |
|======================|                    |================|
| [exec]  >-----------|-->                  | exec-->        |
| door    o-----------|-->  entity          | value o--------|
| speed   o-----------|-->  float           +================+
+======================+
```

### Call Function Node

Used from the event graph or from other functions:

```
        +===================+
 exec-->| Call: OpenDoor    |
        |===================|
 door o-|  door   (entity)  |
speed o-|  speed  (float)   |
        |-------------------|
        | exec   >----------|-->
        | return o----------|-->  (return value, if any)
        +===================+
```

### Pure Functions

A function can be marked as **Pure** (no exec pins, no side effects). Pure functions are evaluated on-demand like math nodes. They are useful for reusable calculations:

```
+=======================+
| [Pure] DistanceToPlayer|
|=======================|
| entity o--------------|-->  entity input
|                       |
| result o--------------|-->  float output
+=======================+
```

**Generated Lua:**
```lua
-- Function definition (pure)
function M:__fn_DistanceToPlayer(entity)
    local player = ctx.scene:find_node("Player")
    if not player then return 0 end
    return (entity.position - player.position):length()
end
```

---

## 9. Code Generation

### Generation Pipeline

```
.uzblueprint (graph definition)
        |
        v
  [1] Parse graph: nodes, connections, variables
        |
        v
  [2] Topological sort per execution chain
        |
        v
  [3] Identify lifecycle buckets:
        - on_start: OnBeginPlay chain + variable initialization
        - on_update: OnTick chains + OnKeyPressed/Released/Held chains
        - on_stop: OnEndPlay chain + timer cancellation + event unsubscription
        - event_handlers: OnTriggerEnter, OnEntityDamaged, Custom events
        - functions: user-defined function sub-graphs
        |
        v
  [4] Emit Lua code via node visitors (template strings)
        |
        v
  [5] Merge with manual sections (if any)
        |
        v
  [6] Write .lua file to scene directory
```

### Generated File Structure

```lua
-- =============================================================
-- AUTO-GENERATED by UzEngine Level Blueprint Editor
-- Source: res://scenes/level1.uzblueprint
-- Generated: 2026-03-02T14:30:00Z
-- DO NOT EDIT between [GENERATED-START] and [GENERATED-END]
-- =============================================================

local M = {}

-- [GENERATED-START]

-- ============================================
-- Variables (defaults)
-- ============================================

-- [node:var_init]
function M:__init_vars()
    self.doorOpen = false          -- bool, exposed=false
    self.spawnCount = 0            -- int, exposed=false
    self.moveSpeed = 3.5           -- float, exposed=true
    self.targetName = ""           -- string, exposed=true
end

-- ============================================
-- Functions
-- ============================================

-- [node:fn_OpenDoor, id=42]
function M:__fn_OpenDoor(door, speed)
    -- [node:SetPosition, id=43]
    door.position = door.position + Vec3(0, speed * 2.0, 0)
    -- [node:PlaySound2D, id=44]
    ctx.audio:play_2d("res://audio/door_open.wav", "SFX", 1.0)
end

-- ============================================
-- Lifecycle: on_start (OnBeginPlay)
-- ============================================

function M:on_start(ctx)
    self:__init_vars()

    -- Read exposed variables from scene properties
    self.moveSpeed = self.moveSpeed or 3.5
    self.targetName = self.targetName or ""

    -- [node:OnBeginPlay, id=1]

    -- [node:GetEntityByName, id=2]
    local __n2 = ctx.scene:find_node("BossDoor")

    -- [node:SetVariable_targetRef, id=3]
    self.targetRef = __n2

    -- [node:Log, id=4]
    ctx:log("Level blueprint initialized, door=" .. tostring(__n2))

    -- Event subscriptions
    -- [node:OnTriggerEnter, id=10]
    self.__sub_10 = ctx.events:subscribe("TriggerEnter", function(data)
        local other = data.entity
        -- [node:HasTag, id=11]
        local __n11 = other:has_tag("Player")
        -- [node:Branch, id=12]
        if __n11 then
            -- [node:Call_OpenDoor, id=13]
            self:__fn_OpenDoor(self.targetRef, self.moveSpeed)
            -- [node:SetVariable_doorOpen, id=14]
            self.doorOpen = true
            -- [node:EmitEvent, id=15]
            ctx.events:emit("DoorOpened", { door = self.targetRef })
        end
    end)

    -- [node:OnEntityDamaged, id=20]
    self.__sub_20 = ctx.events:subscribe("EntityDamaged", function(data)
        local entity = data.entity
        local amount = data.amount
        -- [node:Log, id=21]
        ctx:log("Damage: " .. tostring(amount))
    end)
end

-- ============================================
-- Lifecycle: on_update (OnTick, Input events)
-- ============================================

function M:on_update(ctx, dt)
    -- [node:OnKeyPressed_Jump, id=30]
    if ctx.input:pressed("Jump") then
        -- [node:Log, id=31]
        ctx:log("Jump pressed!")
    end
end

-- ============================================
-- Lifecycle: on_stop (OnEndPlay)
-- ============================================

function M:on_stop(ctx)
    -- Unsubscribe events
    if self.__sub_10 then ctx.events:unsubscribe(self.__sub_10) end
    if self.__sub_20 then ctx.events:unsubscribe(self.__sub_20) end

    -- Cancel timers
    -- (none in this blueprint)
end

-- [GENERATED-END]

-- =============================================================
-- [MANUAL] — Hand-written code below this line is preserved
-- across recompilation. Add custom functions, overrides, etc.
-- =============================================================



-- =============================================================
return M
```

### Node ID Comments

Every generated block includes a comment with the source node ID (`-- [node:NodeType, id=N]`). This allows:
- The editor to highlight the corresponding node when a Lua error references a line number
- AI agents to trace generated code back to graph nodes
- Debugging: `lua.get_errors` returns file+line, which the extension maps to node IDs

### Code Generation Rules

1. **Variable naming**: generated temporaries use `__nID` pattern (e.g., `__n2`, `__n43`) to avoid conflicts with user variable names
2. **State variables**: flow control state uses `self.__TYPE_ID` pattern (e.g., `self.__gate_1`, `self.__flipflop_1`)
3. **Event subscriptions**: stored in `self.__sub_ID` and unsubscribed in `on_stop`
4. **Timer IDs**: stored in `self.__timer_ID` and cancelled in `on_stop`
5. **Delay chains**: everything after a Delay node is wrapped in `ctx:schedule_after(duration, function() ... end)` with proper closure capture
6. **Pure nodes**: inlined at their consumption site. If consumed multiple times, a local variable caches the result
7. **No global pollution**: all generated code uses `local` declarations and `self` fields. No globals are created

### Full Example: Door Trigger

The following graph represents a classic trigger-based door opener:

```
+===============+     +==================+     +===============+
| OnBeginPlay   |====>| GetEntityByName  |====>| Set: doorRef  |
|               |     | name="BossDoor"  |     | = (entity)    |
+===============+     +====o=============+     +======|========+
                            |entity                    |
                            +------------+             |
                                         |             v
                                    +====|=====+  +=============+
                                    |  unused  |  | Log         |
                                    +==========+  | "Ready!"    |
                                                  +=====|=======+
                                                        | exec
                                                        v
+================+     +=============+     +==================+
| OnTriggerEnter |====>| HasTag      |====>| Branch           |
| other (entity) |     | tag="Player"|     | condition=result |
+====o===========+     +=o===========+     +=====|======|=====+
     |                    |                  True |      | False
     |other          result|                     v      v (nothing)
     +------>---------+    |            +===============+
                           |            | Call: OpenDoor |
                           +------>     | door = doorRef |
                                        | speed= 5.0    |
                                        +======|========+
                                               | exec
                                               v
                                        +===============+
                                        | PlaySound2D   |
                                        | clip="door.wav"|
                                        | bus="SFX"     |
                                        +======|========+
                                               | exec
                                               v
                                        +===============+
                                        | ShowUI        |
                                        | panel="toast" |
                                        +=======|=======+
                                                | exec
                                                v
                                        +===============+
                                        | Set UI Prop   |
                                        | panel="toast" |
                                        | prop="text"   |
                                        | value="Door   |
                                        |   opened!"    |
                                        +===============+
```

**Generated Lua output:**

```lua
-- =============================================================
-- AUTO-GENERATED by UzEngine Level Blueprint Editor
-- Source: res://scenes/level1.uzblueprint
-- Generated: 2026-03-02T14:30:00Z
-- =============================================================

local M = {}

-- [GENERATED-START]

function M:__init_vars()
    self.doorRef = nil   -- entity, exposed=false
end

function M:__fn_OpenDoor(door, speed)
    -- [node:SetPosition, id=50]
    door.position = door.position + Vec3(0, speed * 2.0, 0)
end

function M:on_start(ctx)
    self:__init_vars()

    -- [node:OnBeginPlay, id=1]
    -- [node:GetEntityByName, id=2]
    local __n2 = ctx.scene:find_node("BossDoor")
    -- [node:SetVariable_doorRef, id=3]
    self.doorRef = __n2
    -- [node:Log, id=4]
    ctx:log("Ready!")

    -- [node:OnTriggerEnter, id=10]
    self.__sub_10 = ctx.events:subscribe("TriggerEnter", function(data)
        local other = data.entity
        -- [node:HasTag, id=11]
        local __n11 = other:has_tag("Player")
        -- [node:Branch, id=12]
        if __n11 then
            -- [node:Call_OpenDoor, id=13]
            self:__fn_OpenDoor(self.doorRef, 5.0)
            -- [node:PlaySound2D, id=14]
            ctx.audio:play_2d("res://audio/door_open.wav", "SFX", 1.0)
            -- [node:ShowUI, id=15]
            ctx.ui:show("toast")
            -- [node:SetUIProp, id=16]
            ctx.ui:set("toast", "text", "Door opened!")
        end
    end)
end

function M:on_update(ctx, dt)
    -- (no OnTick or input events in this blueprint)
end

function M:on_stop(ctx)
    if self.__sub_10 then ctx.events:unsubscribe(self.__sub_10) end
end

-- [GENERATED-END]

return M
```

---

## 10. Debugging

### Breakpoints

Right-click any node and select "Toggle Breakpoint". The node displays a red dot in its top-left corner. When execution reaches a breakpoint node, the engine pauses (sets timescale to 0) and the editor highlights the node.

### Breakpoint Behavior

```
Engine running -> execution hits breakpoint node
    |
    v
1. Engine pauses (timescale = 0)
2. IPC message: blueprint.breakpoint_hit { blueprint, node_id, locals }
    |
    v
3. Extension highlights the node with a yellow border
4. Details Panel shows all wire values flowing into/out of the node
5. Step controls appear in toolbar:
   [Continue] [Step Over] [Step Into Function] [Step Out]
    |
    v
6. User clicks [Continue] -> IPC: blueprint.continue -> timescale restored
```

### Step Execution

| Button | Behavior |
|--------|----------|
| **Continue** | Resume normal execution |
| **Step Over** | Execute the current node, pause on the next node in the same exec chain |
| **Step Into** | If current node is "Call Function", enter the function graph and pause at first node |
| **Step Out** | If inside a function graph, execute remaining nodes and pause at the caller's next node |

### Wire Value Preview

When the game is running in editor mode, hovering over any data wire shows the last value that flowed through it in a tooltip:

```
            +-----------+
 health o---|-- 42.0 ---|---o value
            +-----------+
              ^ tooltip
```

Wire values are captured during execution by injected logging code (editor-only, stripped in shipping). The extension polls `blueprint.get_wire_values` IPC to update previews.

### Execution Trace

After each frame, the editor highlights the most recently executed path with a bright green overlay on the exec wires. Nodes that executed this frame get a subtle green pulse animation. This lets designers see which branches were taken without stepping through.

### Error Highlighting

When a Lua runtime error occurs in generated code:

1. The `lua.get_errors` IPC returns the error with file and line number
2. The extension maps the line number to the node ID comment (e.g., `-- [node:HasTag, id=11]`)
3. The corresponding node is highlighted with a red border and a red error icon
4. The Details Panel shows the error message
5. Hovering the node shows the full error traceback in a tooltip

```
+==================+
| X HasTag    [!]  |    <-- red border, error icon
|==================|
| tag="Player"     |
+==================+
  Error: attempt to call method 'has_tag' (a nil value)
  at res://game/level_blueprint.lua:47
```

---

## 11. File Format (.uzblueprint)

The blueprint is stored as a plain-text INI file, consistent with other UzEngine file formats (`.uzscene`, `.uzmat`, `.uzinput`). This makes it readable and writable by AI agents.

### Format Structure

```ini
; =============================================================
; Level Blueprint: Level1 Door Logic
; Scene: res://scenes/level1.uzscene
; Generated by: UzEngine Blueprint Editor v1.0
; =============================================================

[uzblueprint version=1 name="Level1_DoorLogic"]
scene      = "res://scenes/level1.uzscene"
lua_output = "res://game/level1_blueprint.lua"

; ==========================
; Variables
; ==========================

[variable name="doorRef" type="entity"]
default  = nil
exposed  = false

[variable name="moveSpeed" type="float"]
default  = 3.5
exposed  = true
min      = 0.0
max      = 20.0

[variable name="doorOpen" type="bool"]
default  = false
exposed  = false

; ==========================
; Function Definitions
; ==========================

[function name="OpenDoor"]
params = door:entity, speed:float
return = void

; ==========================
; Nodes
; ==========================

; -- Event Graph --

[node id=1 type="OnBeginPlay"]
position = [100, 200]

[node id=2 type="GetEntityByName"]
position = [350, 200]
name     = "BossDoor"

[node id=3 type="SetVariable"]
position = [600, 200]
variable = "doorRef"

[node id=4 type="Log"]
position = [850, 200]
message  = "Ready!"

[node id=10 type="OnTriggerEnter"]
position = [100, 500]
tag_filter = "Player"

[node id=11 type="HasTag"]
position = [350, 500]
tag      = "Player"

[node id=12 type="Branch"]
position = [600, 500]

[node id=13 type="CallFunction"]
position = [850, 450]
function = "OpenDoor"

[node id=14 type="PlaySound2D"]
position = [1100, 450]
clip     = "res://audio/door_open.wav"
bus      = "SFX"
volume   = 1.0

[node id=15 type="ShowUI"]
position = [1350, 450]
panel    = "toast"

[node id=16 type="SetUIProperty"]
position = [1600, 450]
panel    = "toast"
property = "text"
value    = "Door opened!"

; -- Function: OpenDoor --

[node id=50 type="FunctionEntry" function="OpenDoor"]
position = [100, 100]

[node id=51 type="SetPosition"]
position = [400, 100]

[node id=52 type="Add_Vec3"]
position = [250, 200]

[node id=53 type="MakeVec3"]
position = [100, 250]

; ==========================
; Connections
; ==========================
; Format: source_node.pin -> target_node.pin
; exec pins: exec_out, exec_true, exec_false, then_0, then_1, ...
; data pins: named by pin name

[connections]
; OnBeginPlay chain
1.exec_out       -> 2.exec_in
2.exec_out       -> 3.exec_in
3.exec_out       -> 4.exec_in

; Data: GetEntityByName result -> SetVariable value
2.entity         -> 3.value

; OnTriggerEnter chain
10.exec_out      -> 11.exec_in
10.other         -> 11.entity
11.exec_out      -> 12.exec_in
11.result        -> 12.condition

; Branch true path
12.exec_true     -> 13.exec_in
13.exec_out      -> 14.exec_in
14.exec_out      -> 15.exec_in
15.exec_out      -> 16.exec_in

; CallFunction data inputs
; doorRef variable -> OpenDoor door parameter
var.doorRef      -> 13.door
const.5.0        -> 13.speed

; Function: OpenDoor internal connections
50.exec_out      -> 51.exec_in
50.door          -> 51.entity
52.result        -> 51.position
50.door          -> 52.a           ; current position
53.result        -> 52.b           ; offset vector
const.0          -> 53.x
50.speed         -> 53.y           ; speed * 2 as Y offset
const.0          -> 53.z

; ==========================
; Comments (visual annotations)
; ==========================

[comment id=100]
position = [300, 100]
size     = [700, 180]
text     = "Initialize door reference on level start"
color    = [0.2, 0.3, 0.5, 0.3]

[comment id=101]
position = [300, 400]
size     = [1400, 250]
text     = "When player enters trigger, open the door"
color    = [0.3, 0.5, 0.2, 0.3]

; ==========================
; Breakpoints
; ==========================

[breakpoints]
nodes = 12, 13
```

### Format Rules

1. **Version**: `version=1` — increment on breaking changes
2. **Node IDs**: monotonically increasing integers, never reused within a blueprint
3. **Positions**: `[x, y]` in pixel coordinates on the graph canvas
4. **Connections**: `source_node.pin_name -> target_node.pin_name` — one per line
5. **Special connection prefixes**: `var.NAME` for variable reads, `const.VALUE` for inline constants
6. **Comments**: visual annotation boxes (not code comments) — rendered as colored rectangles on the canvas
7. **Breakpoints**: list of node IDs with active breakpoints

### Companion .lua File

The generated `.lua` file is written alongside the blueprint:

```
res://scenes/
    level1.uzscene
    level1.uzblueprint       <-- graph definition (committed to source)
res://game/
    level1_blueprint.lua     <-- generated Lua (committed to source)
```

Both files are committed to source control. The `.lua` file can be regenerated from the `.uzblueprint` at any time (via toolbar "Compile" or `blueprint.compile` IPC). The `.lua` file is the runtime artifact — the `.uzblueprint` is the authoring artifact.

---

## 12. Macro Nodes (.uzmacro)

### Overview

Macro nodes are reusable sub-graphs that can be shared across multiple blueprints. They are stored as `.uzmacro` files in the project's `res://macros/` directory.

### File Format

```ini
[uzmacro version=1 name="DamageAndKnockback"]
description = "Apply damage to entity and push it away from source"

[input name="target" type="entity"]
[input name="source" type="entity"]
[input name="damage" type="float"]
[input name="force" type="float"]

[output name="killed" type="bool"]

; Internal nodes and connections (same format as .uzblueprint)
[node id=1 type="FunctionEntry"]
; ...
[connections]
; ...
```

### Usage

Macros appear in the Node Palette under a "Macros" category. When placed on the canvas, they show as a single collapsed node with the defined inputs and outputs. Double-clicking a macro node opens its internal graph in a new tab (read-only if the macro is a shared asset; editable if it is local to the current blueprint).

### Code Generation

Macros are inlined at the call site during code generation (not called as separate functions). This ensures zero overhead and keeps the generated Lua simple. If a macro is used more than 3 times in the same blueprint, the compiler may optionally emit it as a local function to reduce code size.

---

## 13. Implementation Notes

### C++ Class Hierarchy (Blueprint Compiler)

```
src/extension/blueprint/           (TypeScript, VSCode extension)
    BlueprintEditorPanel.ts        WebView panel controller
    GraphCanvas.tsx                React component: SVG graph renderer
    NodePalette.tsx                Searchable node list
    VariablePanel.tsx              Variable editor
    DetailsPanel.tsx               Node property editor
    LuaPreview.tsx                 Generated code viewer
    UndoStack.ts                   Blueprint-specific undo/redo
    types/
        BlueprintGraph.ts          Node, Pin, Connection, Variable types
        NodeDefinitions.ts         Registry of all built-in node types

src/extension/blueprint/compiler/  (TypeScript, runs in extension host)
    BlueprintCompiler.ts           Orchestrates the compilation pipeline
    GraphParser.ts                 Parses .uzblueprint INI into BlueprintGraph
    GraphValidator.ts              Checks for unconnected pins, cycles, type mismatches
    TopologicalSorter.ts           Sorts nodes per execution chain
    LifecycleBucketer.ts           Assigns nodes to on_start/on_update/on_stop
    LuaEmitter.ts                  Visits sorted nodes and emits Lua code
    ManualSectionPreserver.ts      Preserves [MANUAL] code across recompilation
    BlueprintSerializer.ts         Writes BlueprintGraph back to .uzblueprint INI

src/extension/blueprint/nodes/     (TypeScript, node visitor implementations)
    EventNodeEmitter.ts            OnBeginPlay, OnTick, OnTriggerEnter, etc.
    FlowNodeEmitter.ts             Branch, Sequence, ForEach, Gate, etc.
    EntityNodeEmitter.ts           GetByName, SetPosition, Destroy, etc.
    TimerNodeEmitter.ts            Delay, SetTimer, CancelTimer
    AudioNodeEmitter.ts            PlaySound2D, PlaySoundAt, SetBusVolume
    PhysicsNodeEmitter.ts          ApplyForce, Raycast, OverlapSphere
    MathNodeEmitter.ts             Add, Subtract, MakeVec3, Lerp, etc.
    VariableNodeEmitter.ts         GetVariable, SetVariable
    FunctionNodeEmitter.ts         CallFunction, FunctionEntry, Return
```

### Compilation Pipeline (TypeScript)

```typescript
// src/extension/blueprint/compiler/BlueprintCompiler.ts

export class BlueprintCompiler {
    private parser: GraphParser;
    private validator: GraphValidator;
    private sorter: TopologicalSorter;
    private bucketer: LifecycleBucketer;
    private emitter: LuaEmitter;
    private preserver: ManualSectionPreserver;

    /**
     * Compile a .uzblueprint file into a .lua file.
     * Returns diagnostics (warnings/errors) for the editor to display.
     */
    async compile(blueprintPath: string): Promise<CompileResult> {
        // 1. Parse .uzblueprint INI into graph model
        const ini = await fs.readFile(blueprintPath, 'utf8');
        const graph = this.parser.parse(ini);

        // 2. Validate: type mismatches, unconnected required pins, cycles
        const diagnostics = this.validator.validate(graph);
        if (diagnostics.hasErrors()) {
            return { success: false, diagnostics, lua: '' };
        }

        // 3. Topological sort per execution chain
        const sortedChains = this.sorter.sort(graph);

        // 4. Bucket into lifecycle methods
        const buckets = this.bucketer.assign(sortedChains, graph);

        // 5. Emit Lua code
        const generatedLua = this.emitter.emit(buckets, graph);

        // 6. Preserve manual sections from existing .lua file
        const luaPath = graph.metadata.lua_output;
        const existingLua = await fs.readFile(luaPath, 'utf8').catch(() => '');
        const finalLua = this.preserver.merge(generatedLua, existingLua);

        // 7. Write output
        await fs.writeFile(luaPath, finalLua, 'utf8');

        return { success: true, diagnostics, lua: finalLua };
    }
}
```

### Node Definition Registry

Each node type is described by a `NodeDefinition` object:

```typescript
// src/extension/blueprint/types/NodeDefinitions.ts

interface PinDefinition {
    name: string;
    type: PinType;       // 'exec' | 'bool' | 'int' | 'float' | 'string' | 'entity' | 'vector3' | 'any'
    direction: 'input' | 'output';
    optional?: boolean;  // if true, unconnected pin uses default value
    default?: unknown;   // default value when unconnected
}

interface NodeDefinition {
    type: string;                    // e.g., "Branch", "GetEntityByName"
    category: string;                // e.g., "Flow", "Entity", "Timer"
    label: string;                   // Display name
    description: string;             // Tooltip text
    pins: PinDefinition[];
    properties: PropertyDefinition[]; // Editable in Details Panel
    isPure: boolean;                 // true = no exec pins, evaluated on demand
    color: string;                   // Node header color (CSS hex)
    icon: string;                    // VS Codicon name
}

// Example: Branch node definition
const BranchNode: NodeDefinition = {
    type: 'Branch',
    category: 'Flow',
    label: 'Branch',
    description: 'If/else: routes execution based on a boolean condition',
    pins: [
        { name: 'exec_in',   type: 'exec',  direction: 'input' },
        { name: 'condition',  type: 'bool',  direction: 'input', default: false },
        { name: 'exec_true',  type: 'exec',  direction: 'output' },
        { name: 'exec_false', type: 'exec',  direction: 'output' },
    ],
    properties: [],
    isPure: false,
    color: '#4A6FA5',
    icon: 'git-merge',
};
```

### WebView Graph Renderer

The graph canvas is implemented as a React component rendered inside a VSCode WebView:

```
WebView (iframe)
    React root
        <GraphCanvas>                   SVG-based node graph
            <BackgroundGrid />          Infinite grid with zoom
            <ConnectionLayer />         SVG <path> elements for wires
                <ExecWire />            White bezier curves (exec flow)
                <DataWire />            Colored bezier curves (data flow)
            <NodeLayer />               Positioned <div> elements for nodes
                <BlueprintNode />       Individual node with header, pins, body
                    <PinWidget />       Circle/triangle pin with label
            <SelectionRect />           Rubber-band selection
            <CommentBox />              Colored annotation rectangles
            <MiniMap />                 Bottom-right overview
        <QuickAddMenu />                Space-key node search popup
```

### IPC Integration

| IPC Command | Direction | Purpose |
|-------------|-----------|---------|
| `blueprint.compile` | Extension -> Engine | Trigger Lua recompilation (also calls `lua.reload_script`) |
| `blueprint.validate` | Extension -> Engine | Validate generated Lua loads without error |
| `blueprint.get_wire_values` | Extension -> Engine | Poll current wire values for live preview |
| `blueprint.breakpoint_hit` | Engine -> Extension | Notify breakpoint reached, send locals |
| `blueprint.continue` | Extension -> Engine | Resume from breakpoint |
| `blueprint.step` | Extension -> Engine | Step to next node |
| `lua.reload_script` | Extension -> Engine | Hot-reload the generated `.lua` file |
| `lua.get_errors` | Extension -> Engine | Get Lua errors for node error highlighting |

### Integration with LuaVM Hot-Reload

When the blueprint is compiled (Ctrl+S or toolbar "Compile"), the extension:

1. Runs `BlueprintCompiler.compile()` to generate the `.lua` file
2. Sends `lua.reload_script` IPC to the engine with the `.lua` file path
3. The engine's `LuaScriptReloader` picks up the change (or is triggered directly by IPC)
4. Hot-reload completes in ~60ms (same path as hand-edited Lua files)

The designer sees changes reflected in the running game within one frame after saving.

### Undo/Redo for Blueprint Edits

The blueprint editor maintains its own `UndoStack` (separate from the scene editor's undo stack). Blueprint undo commands include:

| Command | Undo Action |
|---------|-------------|
| `AddNodeCommand` | Remove the added node and its connections |
| `RemoveNodeCommand` | Restore the removed node, connections, and position |
| `MoveNodeCommand` | Restore previous position |
| `ConnectPinsCommand` | Remove the connection |
| `DisconnectPinsCommand` | Restore the connection |
| `SetNodePropertyCommand` | Restore previous property value |
| `AddVariableCommand` | Remove the variable |
| `RemoveVariableCommand` | Restore the variable |
| `PasteNodesCommand` | Remove all pasted nodes |

Each undo/redo also triggers recompilation of the Lua output (debounced to 300ms to avoid excessive compilation during rapid undo).

---

## 14. AI Integration (MCP Tools)

The blueprint system exposes MCP tools for AI-assisted level design:

### `blueprint.create`

Create a new blueprint from a natural language description:

```json
{
    "fn": "blueprint.create",
    "args": {
        "scene": "res://scenes/level1.uzscene",
        "description": "When the player enters the trigger zone near the castle gate, play a horn sound, open the gate by moving it up 5 units over 2 seconds, and show a UI message saying 'The gate opens!'"
    }
}
```

The AI agent translates the description into a `.uzblueprint` file and a compiled `.lua` file.

### `blueprint.add_nodes`

Add nodes to an existing blueprint:

```json
{
    "fn": "blueprint.add_nodes",
    "args": {
        "blueprint": "res://scenes/level1.uzblueprint",
        "description": "Add a repeating timer that spawns an enemy every 10 seconds at position (5, 0, 10)"
    }
}
```

### `blueprint.get_graph`

Return the full graph structure for AI analysis:

```json
{
    "fn": "blueprint.get_graph",
    "args": {
        "blueprint": "res://scenes/level1.uzblueprint"
    }
}
```

Returns the parsed graph as JSON (nodes, connections, variables, functions).

---

## 15. Performance Considerations

### Generated Code Quality

The generated Lua code must not introduce overhead compared to hand-written Lua:

| Concern | Mitigation |
|---------|-----------|
| Excessive local variables | Compiler eliminates unused temporaries in a dead-code pass |
| Deep closure nesting (from Delay chains) | Maximum 4 nested Delay nodes; deeper nesting triggers a compiler warning |
| LuaJIT NYI operations | Compiler avoids `table.unpack`, `string.format` with `%s` in hot paths, `math.fmod` |
| Large `on_update` bodies | OnTick event is marked with a warning icon ("runs every frame"). Compiler emits a comment warning |
| Memory allocation in hot paths | Pure nodes cached per frame; no table creation in `on_update` unless necessary |

### Editor Performance

| Metric | Target |
|--------|--------|
| Graph canvas FPS | 60 FPS with up to 500 nodes |
| Compilation time | < 50ms for graphs under 200 nodes |
| Blueprint file parse | < 10ms |
| Lua hot-reload after compile | < 60ms (LuaJIT standard) |
| Wire value polling | 10 Hz (not every frame) |

---

## 16. Comparison with Other Engines

| Feature | Unreal Blueprint | Unity Visual Scripting | Godot VisualScript | UzEngine Blueprint |
|---------|-----------------|----------------------|-------------------|--------------------|
| Output format | Bytecode (proprietary) | IL (C# backend) | Custom interpreter | Plain Lua source |
| Editable output | No | No | No | Yes (hand-edit generated Lua) |
| Runtime | UObject VM | Mono/.NET | GDScript VM | LuaJIT |
| File format | Binary `.uasset` | Binary | `.tres` (text-ish) | `.uzblueprint` (INI, AI-friendly) |
| Embedded in | Unreal Editor (custom) | Unity Editor (custom) | Godot Editor | VSCode WebView |
| AI-generatable | Difficult (binary format) | Difficult | Possible | Easy (plain text INI) |
| Hot-reload | Compile-on-save | Domain reload (~2s) | Immediate | ~60ms via LuaJIT |
| Coexists with code | C++ alongside BP | C# alongside | GDScript alongside | Lua alongside (same file!) |

---

## 17. Phase Roadmap

| Phase | Deliverable |
|-------|-------------|
| **Phase 2** | Core graph editor WebView, basic node set (events, flow control, entity, variables), code generation, `.uzblueprint` format, Lua compilation, hot-reload integration |
| **Phase 3** | Timer nodes, audio nodes, physics nodes, camera nodes, UI nodes, function sub-graphs, macro system, debugging (breakpoints, wire preview, execution trace) |
| **Phase 4** | AI MCP tools (`blueprint.create`, `blueprint.add_nodes`), advanced flow nodes (Gate, FlipFlop, DoN), macro sharing across projects, blueprint templates library |

---

## 18. Related Docs

- [specs/lua-scripting.md](../specs/lua-scripting.md) -- LuaJIT VM, module pattern, `ctx` API, hot-reload
- [specs/event-bus.md](../specs/event-bus.md) -- EventBus, `ctx.events:subscribe/emit`, C++ <-> Lua bridge
- [specs/timer-system.md](../specs/timer-system.md) -- `ctx:schedule_after/every/cancel_timer`, drain loop
- [specs/scene-system.md](../specs/scene-system.md) -- `.uzscene` format, node types, SceneManager API
- [specs/console-commands.md](../specs/console-commands.md) -- Console command registration from Lua
- [specs/input-system.md](../specs/input-system.md) -- InputAction system, `ctx.input:pressed/held/released`
- [specs/editor-panels.md](../specs/editor-panels.md) -- VSCode panel layout, Inspector, Scene Tree
- [specs/undo-redo.md](../specs/undo-redo.md) -- EditorCommand pattern, UndoStack
- [specs/audio-system.md](../specs/audio-system.md) -- XAudio2, bus system, Lua audio API
- [specs/physics-system.md](../specs/physics-system.md) -- Jolt Physics, collision events, queries
- [specs/camera-system.md](../specs/camera-system.md) -- Camera modes, shake, spring arm
