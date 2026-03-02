# AI-Assisted Development Guide

This guide explains how to work with Claude Code and other AI agents to develop UzEngine games. The engine is designed from the ground up for AI-driven workflows.

---

## Why UzEngine is AI-Native

Unlike traditional engines, UzEngine uses **plain-text, AI-friendly formats** throughout:

- **.uzscene**: INI-like hierarchy (no binary data, no GUIDs)
- **Lua scripts**: Simple, readable text (easy to generate and understand)
- **HLSL shaders**: Direct source code (no intermediate bytecode)
- **Materials**: Plain text `.uzmat` config files
- **MCP server**: Programmatic API for reading/writing engine state

This eliminates the biggest obstacles to AI code generation:
- No GUI knowledge required
- No proprietary binary formats
- No opaque identifiers
- Diffable, versionable source

---

## MCP Tools Reference

The UzEngine **MCP server** exposes the following tools to Claude Code and other compatible AI systems.

### engine.ping

Check if the engine is running.

```json
{
    "command": "engine.ping"
}
```

Response:

```json
{
    "alive": true,
    "version": "0.1.0",
    "uptime_ms": 45230
}
```

### engine.status

Get engine metrics: FPS, memory usage, scene statistics.

```json
{
    "command": "engine.status"
}
```

Response:

```json
{
    "fps": 60.0,
    "frame_time_ms": 16.67,
    "memory_used_mb": 512,
    "memory_peak_mb": 768,
    "scene_name": "main",
    "entity_count": 42,
    "renderer": {
        "tris_rendered": 2450000,
        "draw_calls": 128
    }
}
```

### engine.screenshot

Capture the current rendered frame as PNG.

```json
{
    "command": "engine.screenshot"
}
```

Response:

```json
{
    "success": true,
    "path": "build/preview/screenshot_2026_03_01_120530.png",
    "width": 1280,
    "height": 720,
    "timestamp": "2026-03-01T12:05:30Z"
}
```

Use screenshots to verify visual results during iterative development.

### scene.getTree

Get the full scene hierarchy as JSON.

```json
{
    "command": "scene.getTree"
}
```

Response:

```json
{
    "root_id": 1,
    "nodes": [
        {
            "id": 1,
            "type": "Node3D",
            "name": "Root",
            "parent": 0,
            "position": [0, 0, 0],
            "rotation": [0, 0, 0, 1],
            "scale": [1, 1, 1],
            "properties": {
                "visible": true,
                "tags": ["world"]
            }
        },
        {
            "id": 2,
            "type": "StaticMesh",
            "name": "Ground",
            "parent": 1,
            "position": [0, -1, 0],
            "properties": {
                "mesh": "res://models/ground.glb",
                "material": "res://materials/grass.uzmat",
                "cast_shadow": true
            }
        }
    ]
}
```

### scene.addNode

Create a new node in the scene.

```json
{
    "command": "scene.addNode",
    "type": "StaticMesh",
    "name": "MyMesh",
    "parent": 1,
    "properties": {
        "mesh": "res://models/cube.glb",
        "material": "res://materials/default.uzmat",
        "position": [5, 0, 0]
    }
}
```

Response:

```json
{
    "success": true,
    "node_id": 10
}
```

### scene.setProperty

Modify a node property.

```json
{
    "command": "scene.setProperty",
    "node_id": 2,
    "property": "position",
    "value": [10, 5, -3]
}
```

Response:

```json
{
    "success": true,
    "previous_value": [0, 0, 0]
}
```

### scene.removeNode

Delete a node and its children.

```json
{
    "command": "scene.removeNode",
    "node_id": 10
}
```

Response:

```json
{
    "success": true,
    "removed_count": 5  ; Node + 4 children
}
```

### scene.findByTag

Find all nodes with a given tag.

```json
{
    "command": "scene.findByTag",
    "tag": "enemy"
}
```

Response:

```json
{
    "nodes": [
        {"id": 15, "name": "Goblin_01", "position": [10, 0, 0]},
        {"id": 16, "name": "Goblin_02", "position": [15, 0, 0]},
        {"id": 17, "name": "Goblin_03", "position": [20, 0, 0]}
    ],
    "count": 3
}
```

### shader.reload

Hot-reload a shader file.

```json
{
    "command": "shader.reload",
    "path": "res://shaders/materials/pbr.hlsl"
}
```

Response:

```json
{
    "success": true,
    "variants_compiled": 4,
    "time_ms": 145
}
```

### shader.getErrors

Get compilation errors from the last shader reload.

```json
{
    "command": "shader.getErrors",
    "path": "res://shaders/materials/pbr.hlsl"
}
```

Response:

```json
{
    "errors": [
        {
            "line": 45,
            "column": 10,
            "severity": "error",
            "message": "undeclared identifier 'AlbedoSampler'"
        },
        {
            "line": 120,
            "column": 5,
            "severity": "warning",
            "message": "implicit cast from 'float4' to 'float3'"
        }
    ]
}
```

### lua.eval

Evaluate a Lua expression in the game context.

```json
{
    "command": "lua.eval",
    "code": "return player.health"
}
```

Response:

```json
{
    "success": true,
    "result": 85,
    "type": "number"
}
```

### lua.getErrors

Get runtime errors from Lua scripts.

```json
{
    "command": "lua.getErrors"
}
```

Response:

```json
{
    "errors": [
        {
            "script": "res://scripts/player.lua",
            "line": 45,
            "message": "attempt to call nil",
            "traceback": "on_update at player.lua:45"
        }
    ],
    "count": 1
}
```

### console.execute

Execute a debug console command.

```json
{
    "command": "console.execute",
    "input": "physics.debug_draw_toggle"
}
```

Response:

```json
{
    "success": true,
    "output": "Physics debug draw: enabled"
}
```

### audio.play

Play a sound effect for testing.

```json
{
    "command": "audio.play",
    "path": "res://audio/footsteps.wav",
    "volume": 0.8
}
```

Response:

```json
{
    "success": true,
    "duration_ms": 450
}
```

### physics.debugDraw

Toggle physics debug visualization.

```json
{
    "command": "physics.debugDraw",
    "enabled": true
}
```

Response:

```json
{
    "success": true,
    "state": "enabled"
}
```

---

## Generating .uzscene Files

### Simple Scene Generation

**Prompt to Claude:**

```
Create a .uzscene file for a simple test level with:
1. A ground plane (StaticMesh with mesh=res://models/ground.glb)
2. A player spawn point (Node3D, position [0, 2, 0], tag="spawn")
3. A directional light (sun, position [50, 100, 50])
4. A main camera (position [0, 1.7, 5], is_main=true)

Use INI format. Node IDs start at 1 (root). All parent relationships must be valid.
```

**Claude generates:**

```ini
[uzscene version=1 name="TestLevel"]

[node id=1 type=Node3D name="Root" parent=0]

[node id=2 type=StaticMesh name="Ground" parent=1]
mesh             = "res://models/ground.glb"
material         = "res://materials/grass.uzmat"
cast_shadow      = true
receive_shadow   = true

[node id=3 type=Node3D name="PlayerSpawn" parent=1]
position         = [0.0, 2.0, 0.0]
tags             = ["spawn"]

[node id=4 type=DirectionalLight name="Sun" parent=1]
color            = [1.0, 0.96, 0.87]
intensity        = 1.2
shadow_enabled   = true
shadow_cascades  = 3
position         = [50.0, 100.0, 50.0]
rotation         = [-45.0, 30.0, 0.0]

[node id=5 type=Camera name="MainCamera" parent=1]
position         = [0.0, 1.7, 5.0]
fov              = 75.0
near             = 0.1
far              = 1000.0
is_main          = true
```

### Scene with Physics and Scripting

**Prompt to Claude:**

```
Create a .uzscene with:
1. A wooden box (RigidBody with mass=5, collider_shape=box)
   - Use mesh res://models/cube.glb, material res://materials/wood.uzmat
   - Attach script res://scripts/breakable_box.lua
   - Tag: ["destructible", "wood"]
2. A player controller (Node3D with script res://scripts/player.lua)
   - Attach a camera as child (position [0, 0.5, -2])
   - Tag: ["player"]

Use standard .uzscene format. Ensure all node IDs are unique and parent links valid.
```

**Result:** Claude generates a complete, syntactically valid scene ready to load.

### Iterative Scene Design

**AI-driven workflow:**

1. **Claude generates** initial scene
2. **Human reviews** structure, makes notes
3. **Claude adjusts** based on feedback
4. **Take screenshot** (via MCP tool)
5. **Human reviews** visual result
6. **Claude refines** based on visual feedback
7. **Repeat until satisfied**

Example feedback loop:

```
Human: "The camera is too high. Move it down 1 meter and rotate to look down at 30 degrees."

Claude adjusts:
  position = [0, 0.7, 5]    ; Was [0, 1.7, 5]
  rotation = [30, 0, 0]      ; Was [0, 0, 0]

Take screenshot → Visual confirmation → Approved!
```

---

## Writing Lua Scripts with AI

### Script Generation Pattern

**Prompt to Claude:**

```
Write a Lua script for a player character controller with these behaviors:

1. on_start() — Initialize health to 100, position to [0, 2, 0]
2. on_update(dt) —
   - Move with WASD keys (speed 5 m/s)
   - Jump with Space (5 second cooldown)
   - Rotate with mouse (horizontal only)
3. on_collision_enter(other) —
   - If hit by projectile (tag="projectile"), take 10 damage
   - If health <= 0, disable self
4. Errors must be caught with pcall (never crash the game)

Use ctx.input for input, ctx.saves for persistent state, ctx.log for debugging.
```

**Claude generates complete, production-ready script:**

```lua
-- player.lua
local MOVE_SPEED = 5.0
local JUMP_COOLDOWN = 5.0

function on_start()
    self.health = ctx.saves:get("player_health") or 100
    self.last_jump_time = 0
    self.velocity = [0, 0, 0]
end

function on_update(dt)
    ; Get input
    local move_x = ctx.input:get_axis("horizontal")
    local move_y = ctx.input:get_axis("vertical")

    ; Move character
    local direction = [move_x, 0, move_y]
    self.position[1] = self.position[1] + direction[1] * MOVE_SPEED * dt
    self.position[3] = self.position[3] + direction[3] * MOVE_SPEED * dt

    ; Jump
    if ctx.input:is_key_pressed("space") then
        local now = ctx.time:current()
        if now - self.last_jump_time > JUMP_COOLDOWN then
            self.velocity[2] = 15.0  ; Jump velocity
            self.last_jump_time = now
        end
    end

    ; Mouse look
    local mouse_x = ctx.input:get_mouse_delta_x()
    self.rotation[2] = self.rotation[2] + mouse_x * 0.5

    ; Save health
    ctx.saves:set("player_health", self.health)
end

function on_collision_enter(other)
    if table.contains(other.tags, "projectile") then
        self.health = self.health - 10
        ctx.log("Player hit! Health: " .. self.health)

        if self.health <= 0 then
            self.visible = false
            self:on_stop()
        end
    end
end

function on_stop()
    ctx.log("Player died")
end
```

### Script Error Handling

When Claude generates scripts, emphasize **error safety**:

**Prompt:**

```
Write a Lua script that:
- Uses pcall() for any risky operations (file I/O, external calls)
- Never crashes the game (node is disabled if error occurs)
- Logs all errors to ctx.log() for debugging
- Gracefully handles missing properties (use defaults, don't crash)
```

**Claude's approach:**

```lua
function on_update(dt)
    ; Safe access to potentially missing properties
    local health = self.health or 100
    local pos = self.position or [0, 0, 0]

    ; Safe function call with error handling
    local ok, result = pcall(function()
        return calculate_complex_logic(health, pos)
    end)

    if not ok then
        ctx.log("Error in calculation: " .. result)
        return  ; Exit gracefully
    end

    ; Use result safely
    if result then
        apply_effect(result)
    end
end
```

---

## Co-Authoring Shaders with AI

### Shader Description Pattern

**Prompt to Claude:**

```
Write an HLSL shader that creates a "dissolve" effect:

1. Sample the base color texture (AlbedoTex)
2. Use Perlin noise based on world position and time
3. Alpha-clip when noise < -0.2 (progressive dissolve)
4. Add an orange edge glow around dissolve boundary
5. Use include files from shaders/include/ (common.hlsli, noise.hlsli)

Include cbuffers for dissolve_speed and glow_color as user-tunable parameters.
```

**Claude generates:**

```hlsl
#include "include/common.hlsli"
#include "include/noise.hlsli"

cbuffer DissolveParams : register(b0) {
    float dissolve_speed;
    float3 glow_color;
    float _pad;
};

Texture2D AlbedoTex : register(t0);
SamplerState LinearSampler : register(s0);

struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 world : TEXCOORD1;
};

float4 PSMain(VSOut i) : SV_TARGET {
    float3 base_color = AlbedoTex.Sample(LinearSampler, i.uv).rgb;

    ; Noise-based dissolve
    float noise = perlin_noise_3d(i.world * 0.5);
    float dissolve = noise + g_Time * dissolve_speed;

    ; Alpha clip
    if (dissolve < -0.2) discard;

    ; Edge glow
    float edge = saturate(-dissolve / 0.2);
    float3 glow = glow_color * edge;

    return float4(base_color + glow, 1.0);
}
```

### Iterative Shader Development

**Feedback loop:**

1. Claude generates shader based on description
2. **Manual test**: Update `.uzmat` to reference shader
3. **Take screenshot**: Visual inspection
4. **Feedback**: "The dissolve is too fast, make glow more intense"
5. Claude modifies:
   ```hlsl
   float dissolve = noise + g_Time * dissolve_speed * 0.5;  ; Slower
   float3 glow = glow_color * edge * 2.0;  ; Brighter
   ```
6. **Save** → **hot-reload** → **screenshot** → **verify**

---

## Debugging with AI

### Using Screenshots for Feedback

When iteration needs visual validation:

```python
# Pseudo-code for AI iteration loop

prompt = "Create a scene with a debug visualization..."
claude.generate_scene(prompt)

# Verify visually
screenshot = engine.screenshot()

feedback = "The light is too dim. The ground is too bright."
claude.update_scene(feedback)

screenshot = engine.screenshot()
# Repeat until correct
```

### Using Lua Errors for Debugging

Claude can read error logs and fix issues:

```json
{
    "command": "lua.getErrors"
}

; Response: "player.lua:45: attempt to call nil"

Claude's response: "The issue is on line 45 where we call
a method that doesn't exist. Let me fix that..."
```

### Using Shader Compilation Errors

```json
{
    "command": "shader.getErrors",
    "path": "res://shaders/materials/custom.hlsl"
}

; Response: "Line 45: undeclared identifier 'AlbedoSampler'"

Claude: "Missing sampler declaration. Adding 'SamplerState LinearSampler : register(s0);'..."
```

---

## Best Practices for AI Development

### 1. Start with Specifications

Before asking Claude to generate code, provide:
- **What**: Feature description (what does it do?)
- **Why**: Rationale (why is it needed?)
- **How**: Implementation hints (what patterns to use?)

**Good prompt:**

```
Create a scene with multiple enemies (goblins) that patrol
between waypoints. The AI should:
- Use Lua scripts for behavior
- Check paths are valid before moving
- Log when reaching waypoints
- Support tag-based queries to find nearby players

See docs/specs/scene-system.md for .uzscene format reference.
See docs/guides/lua-scripting-guide.md for Lua API reference.
```

### 2. Verify Generated Code

After Claude generates code:
- **Syntax check**: Is the INI/Lua/HLSL syntactically valid?
- **Reference check**: Do all `res://` paths exist?
- **Logic check**: Does the code match the spec?
- **Test**: Load it in the editor, take a screenshot

Example test:

```
1. Generate scene file
2. Load in editor (verify no parse errors)
3. Play (verify no runtime errors)
4. Screenshot (verify visual correctness)
```

### 3. Incremental Edits

Instead of asking Claude to regenerate entire files, request **incremental changes**:

**Good prompt:**

```
In the player.lua script, modify the on_update() function to:
- Check for jump input every frame (not just once)
- Apply gravity to velocity each frame (gravity = -9.8)
- Add the velocity to position

Keep all other functions unchanged.
```

**Bad prompt:**

```
Rewrite the entire player.lua script to add gravity.
```

Incremental edits are less error-prone and easier to review.

### 4. Use Hot-Reload During Development

The rapid feedback loop is key:

```
Edit → Save → Screenshot → Review → Adjust → Repeat
```

This 5-second cycle allows quick iteration.

### 5. Reference the Specs

Point Claude to relevant specifications and guides:

```
See docs/specs/scene-system.md for complete node type reference.
See docs/guides/scene-authoring.md for .uzscene format examples.
See docs/guides/lua-scripting-guide.md for ctx.* API.
```

This ensures Claude generates correct, idiomatic code.

---

## Common Prompts

### Prompt: Generate a Complete Game Level

```
Create a .uzscene file for a game level with:

1. Environment:
   - Ground plane (mesh: res://models/terrain.glb, material: grass)
   - 3 trees scattered around (mesh: res://models/tree.glb)
   - Sun directional light

2. Gameplay:
   - Player spawn point (tag: "spawn")
   - 5 enemy goblins (prefab: res://prefabs/goblin.uzscene)
   - 3 pickups (prefab: res://prefabs/coin.uzscene)
   - Win zone trigger (area where player wins)

3. Camera:
   - Main camera positioned behind player

Use the .uzscene format from docs/guides/scene-authoring.md.
Ensure all parent/child relationships are valid.
Use unique node IDs starting from 1.
```

### Prompt: Generate a Behavior Script

```
Write a Lua script for an enemy that:

1. Patrols between 3 waypoints:
   - Waypoint 1: [0, 0, 0]
   - Waypoint 2: [10, 0, 0]
   - Waypoint 3: [10, 0, 10]

2. Stops for 2 seconds at each waypoint

3. Chases the player if within 5 meters (use scene.find_by_tag("player"))

4. Attacks when within 1 meter (calls on_attack_player)

5. Logs all behavior to ctx.log

Use pcall() for safety. Reference docs/guides/lua-scripting-guide.md
for the ctx.* API. Return health, position, and state via ctx.saves
for persistence across hot-reloads.
```

### Prompt: Generate a Visual Effect Shader

```
Create an HLSL shader for a "magic aura" effect:

1. Sample the base color texture
2. Add a pulsing glow based on time (use sin(g_Time))
3. Apply a rainbow color shift (use HSV to RGB conversion)
4. Displace vertices slightly along normal (height map effect)

Include files available:
- shaders/include/common.hlsli (utility functions)
- shaders/include/global_cbuffer.hlsli (g_Time, g_CameraPos, etc.)

Make glow_intensity and color_shift_speed tunable via cbuffer.
```

---

## AI Development Workflow Example

**Scenario**: Building an interactive puzzle using Claude Code.

### Step 1: Design the Puzzle (5 min)

**Human specs:**
- 3 colored buttons (red, green, blue)
- Correct sequence: red → blue → red → green
- Visual feedback (buttons glow when pressed)
- Win trigger when sequence is correct

### Step 2: Generate Scene (2 min)

**Human prompt to Claude:**

```
Create a .uzscene for a color puzzle:
- 3 button nodes (StaticMesh, positioned horizontally)
- A win zone (TriggerVolume)
- A UI text element to show puzzle progress
All buttons should have tag "button" + color name (e.g., "button_red")
```

**Claude generates** scene file.

### Step 3: Create Button Script (3 min)

**Human:** "Write button.lua that detects clicks and calls on_button_pressed."

**Claude generates** button.lua with collision detection.

### Step 4: Create Puzzle Manager (5 min)

**Human:** "Write puzzle_manager.lua that tracks button sequence and validates against [red, blue, red, green]."

**Claude generates** manager script with state tracking.

### Step 5: Test and Iterate (5 min)

**Human:**
1. Load scene in editor
2. Take screenshot → visual check
3. Play mode → test interaction
4. Feedback: "Buttons don't look clickable. Add bright color."
5. Claude updates material or script
6. Screenshot → verify → done!

**Total time**: ~20 minutes from concept to working puzzle.

---

## Safety and Best Practices

### What NOT to Ask Claude

- ❌ "Generate a complete game" (too vague, too large)
- ❌ "Write a complex ECS system" (C++, not AI-friendly formats)
- ❌ "Optimize performance without telling me what to optimize" (too open-ended)

### What TO Ask Claude

- ✅ "Generate a .uzscene with a simple test level"
- ✅ "Write a Lua script for enemy patrol behavior"
- ✅ "Create an HLSL shader for a dissolve effect"
- ✅ "Fix the Lua error: attempt to call nil on line 45"

### Validation Checklist

After Claude generates code:

- [ ] **Syntax check**: File parses without errors
- [ ] **Path check**: All `res://` references exist
- [ ] **Type check**: Properties have correct types
- [ ] **Reference check**: Node IDs valid, parent links correct
- [ ] **Test**: Load in editor, no errors
- [ ] **Visual**: Screenshot looks correct

---

## Next Steps

- **Scene authoring**: [scene-authoring.md](scene-authoring.md)
- **Lua scripting**: [lua-scripting-guide.md](../guides/lua-scripting-guide.md)
- **Shader authoring**: [shader-authoring.md](shader-authoring.md)
- **Editor workflow**: [editor-workflow.md](editor-workflow.md)

