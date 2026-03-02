# Advanced Physics Guide

This guide covers practical setup and debugging of advanced physics features in UzEngine: vehicles, cloth simulation, destructible objects, and custom constraint systems. Build on the foundation in [specs/physics-system.md](../specs/physics-system.md) before reading this.

---

## Table of Contents

1. [Vehicle Setup](#vehicle-setup)
2. [Cloth Integration](#cloth-integration)
3. [Destructible Objects](#destructible-objects)
4. [Custom Constraints](#custom-constraints)
5. [Physics Debugging](#physics-debugging)
6. [Performance Optimization](#performance-optimization)
7. [Common Issues](#common-issues)

---

## Vehicle Setup

### Creating a Basic Car

Start with a vehicle asset (`.uzvehicle`):

**res://vehicles/simple_car.uzvehicle**

```ini
[Vehicle]
name = simple_car
vehicle_type = car
mass = 1200
wheelbase = 2.5
track_width = 1.5
center_of_mass = [0, -0.3, 0]

[Wheels]
radius = 0.33
width = 0.2
suspension_travel = 0.15
spring_stiffness = 25000
spring_damping = 2000
lateral_friction = 1.1
longitudinal_friction = 1.0

[Drivetrain]
mode = FWD
torque = 250
max_rpm = 5500
gear_ratios = [-3.0, 3.0, 1.8, 1.0, 0.7]
differential_ratio = 3.5
brake_torque_front = 4000
brake_torque_rear = 2500
brake_bias = 0.65

[Drivetrain.TorqueCurve]
0 = 0
1000 = 150
3000 = 250
5000 = 240
5500 = 100

[Steering]
max_steer_angle = 0.52
steer_rate = 3.0
center_return_rate = 2.5
use_ackermann = true
ackermann_correction = 0.8

[Aerodynamics]
drag_coefficient = 0.3
frontal_area = 2.1
downforce_coefficient = 0
downforce_area = 2.1
```

Now create the scene with wheels:

**res://scenes/car_race.uzscene**

```ini
[node id=1 type=Node3D name="World" parent=0]

[node id=100 type=Node3D name="Car" parent=1]
position = [0, 1, 0]
vehicle = res://vehicles/simple_car.uzvehicle

; Four wheels (attach to car parent)
[node id=101 type=Node3D name="WheelFL" parent=100]
position = [0.75, 0.33, 1.25]
wheel_radius = 0.33
wheel_width = 0.2
wheel_index = 0
suspension_travel = 0.15
suspension_stiffness = 25000
suspension_damping = 2000

[node id=102 type=Node3D name="WheelFR" parent=100]
position = [-0.75, 0.33, 1.25]
wheel_radius = 0.33
wheel_width = 0.2
wheel_index = 1
suspension_travel = 0.15
suspension_stiffness = 25000
suspension_damping = 2000

[node id=103 type=Node3D name="WheelRL" parent=100]
position = [0.75, 0.33, -1.25]
wheel_radius = 0.33
wheel_width = 0.2
wheel_index = 2
suspension_travel = 0.15
suspension_stiffness = 23000
suspension_damping = 2000

[node id=104 type=Node3D name="WheelRR" parent=100]
position = [-0.75, 0.33, -1.25]
wheel_radius = 0.33
wheel_width = 0.2
wheel_index = 3
suspension_travel = 0.15
suspension_stiffness = 23000
suspension_damping = 2000

; Visual mesh
[node id=105 type=Node3D name="Body" parent=100]
mesh = res://meshes/car_body.uzmesh

; Player control script
[node id=106 type=Node3D name="Controller" parent=100]
script = res://scripts/car_controller.lua
```

**res://scripts/car_controller.lua**

```lua
local M = {}

function M:on_start(ctx)
    self.speed = 0
    self.steering_angle = 0
    self.throttle = 0
    self.brake_force = 0
    self.handbrake_force = 0
    self.max_steer_angle = 0.52
end

function M:on_update(ctx, dt)
    -- Read input
    local throttle_input = ctx.input:axis("Throttle")
    self.throttle = math.max(0, throttle_input)  -- only positive throttle
    self.steering_angle = ctx.input:axis("Horizontal") * self.max_steer_angle

    if ctx.input:pressed("Brake") then
        self.brake_force = 1.0
    else
        self.brake_force = 0.0
    end

    if ctx.input:pressed("Handbrake") then
        self.handbrake_force = 1.0
    else
        self.handbrake_force = 0.0
    end

    -- Apply to vehicle
    ctx.vehicle:accelerate(self, self.throttle)
    ctx.vehicle:steer(self, self.steering_angle)
    ctx.vehicle:brake(self, self.brake_force)
    ctx.vehicle:handbrake(self, self.handbrake_force)

    -- Update speed display
    self.speed = ctx.vehicle:get_speed(self)
end

return M
```

### Fine-Tuning Vehicle Feel

**Suspension Tuning**:

```
Lower spring_stiffness → softer, bouncier suspension (comfort)
Higher spring_stiffness → stiffer, responsive suspension (racing)

Lower spring_damping → bouncy, oscillating (arcade feel)
Higher spring_damping → stable, predictable (realistic feel)

Typical: stiffness = 20000–30000, damping = 1500–3000
```

**Braking Tuning**:

```
brake_bias = 0.65   → 65% front, 35% rear (typical car)
brake_bias = 0.5    → even front/rear (truck)
brake_bias = 0.8    → more front bias (racing)

brake_torque_front + brake_torque_rear should equal max stopping force
```

**Steering Tuning**:

```
max_steer_angle = 0.52 rad = ~30°  (typical car)
max_steer_angle = 0.35 rad = ~20°  (truck, less agile)
max_steer_angle = 0.7  rad = ~40°  (go-kart, very agile)

steer_rate: how fast steering response
- Low (1.0): delayed, smooth input feel
- High (5.0): immediate, twitchy response
```

---

## Cloth Integration

### Setting Up Character Cloth

Create a cloth asset for a cape or skirt:

**res://cloth/cape.uzcloth**

```ini
[Cloth]
name = character_cape
mesh_asset = res://meshes/cape_mesh.uzmesh
mass_per_vertex = 0.05
damping = 0.3
stiffness = 0.8

[Groups]
shoulder_pins = [0, 1, 2, 3, 4, 5]      ; pinned to shoulder bone
upper_back_free = [6, 7, ..., 500]     ; freely simulated

[Constraints]
edge_compliance = 0.02
edge_damping = 0.1
bend_compliance = 0.05
bend_damping = 0.15

[Collision]
self_collision = false
environment_collision = true
self_collision_margin = 0.02

[Wind]
wind_enabled = true
base_wind_strength = 2.0
turbulence = 0.2

[Pinning]
shoulder = "Armature.Shoulder"
upper_back = "Armature.Spine.001"
```

Add cloth to a character node:

```ini
[node id=10 type=Node3D name="Character" parent=1]
position = [0, 1, 0]
mesh = res://meshes/character.uzmesh
skeleton = Armature

[node id=11 type=Node3D name="Cape" parent=10]
cloth_asset = res://cloth/cape.uzcloth
cloth_mesh = res://meshes/cape_mesh.uzmesh
cloth_mass_per_vertex = 0.05
cloth_damping = 0.3
cloth_stiffness = 0.8
cloth_wind_enabled = true

[node id=12 type=Node3D name="CapeController" parent=11]
script = res://scripts/cape_physics.lua
```

**res://scripts/cape_physics.lua**

```lua
local M = {}

function M:on_start(ctx)
    self.current_blend = 0.0
    self.target_blend = 0.0
    self.blend_speed = 1.5  -- blend per second
end

function M:on_update(ctx, dt)
    local parent = self:get_parent()
    local parent_speed = parent:get_velocity():length()

    -- Blend based on character speed
    if parent_speed < 0.5 then
        self.target_blend = 0.0  -- idle: full skeleton
    elseif parent_speed < 3.0 then
        self.target_blend = 0.2  -- slow walk
    elseif parent_speed < 6.0 then
        self.target_blend = 0.4  -- normal walk
    else
        self.target_blend = 0.6  -- running
    end

    -- Smooth transition
    self.current_blend = lerp(self.current_blend, self.target_blend, self.blend_speed * dt)
    ctx.cloth:set_blend(self, self.current_blend)

    -- Apply wind (optional: from environment)
    if parent:has_tag("outdoor") then
        ctx.cloth:set_wind_zone(self, {
            direction = Vec3(1, 0, 0):normalized(),
            strength = 3.0,
            turbulence = 0.3,
            gust_frequency = 0.5,
            gust_strength = 1.5,
            gust_duration = 0.3
        })
    else
        ctx.cloth:clear_wind_zone(self)
    end
end

return M
```

### Cloth Debugging

Enable physics debug draw in editor:

```
UzEngine: Toggle Physics Debug → See cloth vertices, edges, constraints
UzEngine: Cloth Inspector → Blend percentage, vertex count, wind force
```

**Common cloth issues**:

| Issue | Cause | Solution |
|-------|-------|----------|
| Cloth sinks through body | Too stiff / too few constraints | Increase bend constraints, lower mass_per_vertex |
| Cloth flails wildly | Too loose / low damping | Increase damping to 0.4–0.5, increase stiffness |
| Self-collision failures | Too many vertices | Reduce mesh density, use LOD |
| Wind has no effect | wind_enabled = false | Check cloth config, set wind_strength > 0 |
| Jerky movement | Skeleton and physics fighting | Reduce physics_blend, use easing |

---

## Destructible Objects

### Creating Breakable Crates

First, generate the fracture pattern offline:

```bash
# Terminal
cd E:\UzEngine
.\build\Release\UzBuild.exe fracture res://meshes/crate.gltf res://fractures/crate.uzfrac --sites=8
```

This creates `crate.uzfrac` with 8 Voronoi cells.

Add to scene:

```ini
[node id=50 type=Node3D name="Crate" parent=1]
position = [5, 1, 0]
mesh = res://meshes/crate.uzmesh
destructible = res://fractures/crate.uzfrac
destructible_impulse_threshold = 50.0
destructible_propagate_damage = true
destructible_fragment_lifetime = 5.0

[node id=51 type=Node3D name="CrateController" parent=50]
script = res://scripts/breakable_crate.lua
```

**res://scripts/breakable_crate.lua**

```lua
local M = {}

function M:on_start(ctx)
    self.max_impact_log = 100.0
    self.item_inside = "treasure"
end

function M:on_destructible_broken(ctx, event)
    local damage_pct = 100 * event.impact_magnitude / self.max_impact_log

    if damage_pct > 50 then
        -- Drop item
        local drop_pos = self.position + Vec3(0, 1, 0)
        local item = ctx.scene:spawn_node("item_" .. self.item_inside, drop_pos)

        -- Give item velocity from explosion
        item:set_velocity(event.impact_impulse:normalized() * 5.0)

        ctx:log("Crate destroyed! Found: " .. self.item_inside)
        self:play_break_sound(ctx, event.impact_magnitude)
    end
end

return M
```

### Fracture Optimization

**For best performance**:

1. **Limit cells**: 6–10 cells (not 20+)
2. **Simple shapes**: Box/sphere geometry is cheaper than convex meshes
3. **No propagation**: For small objects, only break impact cell
4. **Shorter lifetime**: 3–4 seconds before cleanup
5. **Debris pooling**: Share pool across all destructibles (100 pieces max)

**In `.uzproj`**:

```toml
[physics]
destructible_lod_distance = 100    ; beyond this, don't fracture
destructible_max_fragments = 100   ; pool size
destructible_fragment_lifetime = 4.0
```

---

## Custom Constraints

### Hinged Door

Create a door that swings on a hinge:

**res://scripts/hinged_door.lua**

```lua
local M = {}

function M:on_start(ctx)
    self.is_open = false
    self.hinge_strength = 5000.0   -- N·m
    self.return_force = 2000.0     -- damping
    self.max_angle = 1.57          -- π/2 radians = 90°
end

function M:on_update(ctx, dt)
    -- Calculate hinge torque to keep door at target angle
    local current_angle = self.rotation.y
    local target_angle = self.is_open and self.max_angle or 0.0

    local angle_error = target_angle - current_angle
    local angular_vel = self:get_angular_velocity().y

    -- PID-like control
    local torque = angle_error * self.hinge_strength - angular_vel * self.return_force
    self:apply_torque(Vec3(0, torque, 0))
end

function M:on_trigger_enter(ctx, other)
    if other:has_tag("player") then
        self:open(ctx)
    end
end

function M:open(ctx)
    self.is_open = true
end

function M:close(ctx)
    self.is_open = false
end

return M
```

### Rope/Chain

Simulate a rope by chaining spheres with distance constraints:

**res://scripts/rope_simulator.lua**

```lua
local M = {}

function M:on_start(ctx)
    self.segment_count = 20
    self.segment_mass = 0.1
    self.segment_radius = 0.05
    self.rest_distance = 0.3  -- distance between segments

    -- Create segments
    self.segments = {}
    local start_pos = self.position
    for i = 1, self.segment_count do
        local seg_pos = start_pos + Vec3(0, -i * self.rest_distance, 0)
        local segment = ctx.scene:spawn_node("rope_segment_" .. i, seg_pos)
        segment:set_mass(self.segment_mass)
        segment:set_gravity_scale(1.0)
        table.insert(self.segments, segment)
    end
end

function M:on_update(ctx, dt)
    -- Maintain distance constraints between segments
    for i = 1, #self.segments - 1 do
        local seg_a = self.segments[i]
        local seg_b = self.segments[i + 1]

        local pos_a = seg_a.position
        local pos_b = seg_b.position
        local distance = (pos_b - pos_a):length()

        -- Pull towards rest distance
        if distance > self.rest_distance * 1.1 then
            local direction = (pos_b - pos_a):normalized()
            local pull_force = (distance - self.rest_distance) * 100.0
            seg_b:apply_force(-direction * pull_force)
            seg_a:apply_force(direction * pull_force)
        end
    end
end

return M
```

---

## Physics Debugging

### Enabling Debug Visualization

In editor, toggle debug mode:

**VSCode command palette**:

```
UzEngine: Toggle Physics Debug
```

This shows:
- **White wireframes**: Dynamic bodies
- **Cyan wireframes**: Kinematic bodies
- **Gray wireframes**: Static bodies
- **Red vectors**: Velocity
- **Yellow spheres**: Contact points
- **Blue arrows**: Surface normals

### Console Commands

```lua
-- List all physics bodies
ctx.console:execute("physics.list_bodies")

-- Set gravity (useful for testing)
ctx.console:execute("physics.gravity 0 -5 0")

-- Enable/disable physics
ctx.console:execute("physics.pause true")
ctx.console:execute("physics.pause false")

-- Show performance stats
ctx.console:execute("physics.stats")
```

### Frame-by-Frame Stepping

In play mode, use this script to step physics one frame at a time:

**res://scripts/physics_debugger.lua**

```lua
local M = {}

function M:on_start(ctx)
    self.is_paused = false
    self.step_requested = false
end

function M:on_update(ctx, dt)
    if ctx.input:pressed("DebugPause") then
        self.is_paused = not self.is_paused
        ctx.console:execute("physics.pause " .. tostring(self.is_paused))
    end

    if ctx.input:pressed("DebugStep") then
        self.step_requested = true
        -- Step one frame manually via Lua loops
    end

    -- Display current pause state
    if self.is_paused then
        ctx:log("PAUSED - Press F10 to step, F9 to resume")
    end
end

return M
```

### Profiling Physics

Enable Tracy profiler (Phase 1 feature):

```cpp
// In physics step
PROFILE_SCOPE("Physics::Step");
world.jph->Update(PHYSICS_DT, ...);
```

View in Tracy UI: File → Open → `profile.tracy`

Look for:
- `Physics::Step` wall time (should be ~2–3ms @ 60 Hz)
- Constraint solver time (most expensive part)
- Body sleeping time (should be high if many bodies at rest)

---

## Performance Optimization

### Profiling Your Scene

1. **Enable Physics Stats**:
   ```lua
   ctx.console:execute("physics.stats")
   ```
   Outputs: body count, active bodies, constraint count, solve time

2. **Identify Bottlenecks**:
   - Too many bodies? → Use LOD, combine static meshes
   - Slow constraint solving? → Reduce substeps, use simpler shapes
   - High memory? → Reduce body pool size

### Optimization Strategies

**Reduce body count**:
```ini
; Instead of 100 small boxes, use 1 large compound
[node id=100 type=Node3D name="RubbleHole" parent=1]
collider = "box"
rigid_body_mass = 0          ; static (immobile)
scale = [20, 5, 20]
```

**Use simpler colliders**:
```lua
-- Bad: convex mesh (expensive)
function create_complex_obj(ctx, mesh_asset)
    local obj = ctx.scene:spawn_node("obj", Vec3(0, 0, 0))
    obj.collider = "mesh"
    obj.mesh = mesh_asset
end

-- Good: approximated with box + sphere
function create_simple_obj(ctx)
    local obj = ctx.scene:spawn_node("obj", Vec3(0, 0, 0))
    obj.collider = "box"        -- faster
    obj.collider_half_extents = [1, 1, 1]
end
```

**Sleep inactive bodies**:
```ini
[.uzproj]
[physics]
sleep_velocity_threshold = 0.1     ; m/s below this = can sleep
sleep_angular_threshold = 0.1      ; rad/s below this = can sleep
```

**LOD physics**:
```ini
[.uzproj]
[physics]
physics_lod_distance = 50          ; beyond this, simplify collision
physics_sleep_distance = 200       ; beyond this, disable simulation
```

### Debris Optimization

```cpp
// Limit active debris
DestructibleManager::const uint32_t MAX_DEBRIS = 100;

// Cleanup old debris
debris_pool.set_max_lifetime(3.0f);  // 3 seconds
```

---

## Common Issues

### Vehicle Spinning Out

**Problem**: Car uncontrollably spins when turning.

**Causes**:
- Steering angle too high
- Center of mass too high (unstable)
- Insufficient friction

**Solutions**:
```ini
; Reduce max steering
max_steer_angle = 0.35      ; was 0.52

; Lower center of mass
center_of_mass = [0, -0.4, 0]  ; was -0.3

; Increase friction
lateral_friction = 1.3
longitudinal_friction = 1.2

; Add downforce
downforce_coefficient = -0.1
```

### Cloth Clipping Through Body

**Problem**: Cloth penetrates character mesh.

**Causes**:
- Too large time step
- Insufficient constraint strength
- Missing collision capsules

**Solutions**:
```ini
; Increase stiffness
stiffness = 0.95       ; was 0.8

; Add collision capsules
[Collision]
environment_collision = true
self_collision_margin = 0.01  ; smaller margin = tighter fit
```

### Destructible Not Breaking

**Problem**: Impulse exceeds threshold but object doesn't break.

**Causes**:
- Threshold too high
- Impulse not detected (collision disabled)
- Fracture asset not loaded

**Solutions**:
```lua
-- Lower threshold for testing
ctx.destructible:break_at(destructible, impact_pos, impulse)

-- Check collision layer
[.uzproj]
[physics]
destructible_layer = 2
[physics.collision_matrix]
Projectile = ["Default", "Destructible"]  ; must allow collision
```

### High Physics Frame Time

**Problem**: Physics step takes >5ms (should be ~2–3ms).

**Debugging**:
```lua
ctx.console:execute("physics.stats")
-- Check: active bodies, constraint count, solve iterations
```

**Solutions**:
1. **Reduce bodies**: Remove dynamic objects beyond LOD distance
2. **Simplify shapes**: Replace convex meshes with boxes/spheres
3. **Reduce substeps**: Lower `max_substeps` in `.uzproj` (default: 4)
4. **Combine static objects**: Instead of 100 static boxes, make 1 compound

---

## Advanced Techniques

### Dynamic Ragdoll

When a character dies, switch from animation to ragdoll physics:

```lua
function M:die(ctx)
    -- Switch to full physics
    ctx.cloth:set_blend(self, 1.0)

    -- Disable animation
    self.animation_enabled = false

    -- Wait for settling
    ctx:schedule_after(3.0, function()
        -- Remove corpse
        ctx.scene:remove_node(self)
    end)
end
```

### Wind Zones for Gameplay

Create wind zones that affect cloth and particles:

```ini
[node id=200 type=Node3D name="StormZone" parent=1]
position = [0, 10, 0]
scale = [50, 20, 50]
trigger = true
wind_zone = true
wind_direction = [1, -0.2, 0]
wind_strength = 10.0
wind_turbulence = 0.5
```

### Rope Bridges

String rope constraints between posts to create swaying bridges:

```lua
-- Attach rope segment to post
ctx.cloth:pin_group(rope, "end_segments", "Post_Bridge.End")

-- Wind affects rope sway
ctx.cloth:set_wind_zone(rope, {
    direction = Vec3(0, 0, 1):normalized(),
    strength = 5.0,
    turbulence = 0.4
})
```

---

## Related Documentation

- [specs/physics-system.md](../specs/physics-system.md) — Core physics reference
- [specs/vehicle-physics.md](../specs/vehicle-physics.md) — Vehicle system detailed spec
- [specs/cloth-simulation.md](../specs/cloth-simulation.md) — Cloth system detailed spec
- [specs/destructibles.md](../specs/destructibles.md) — Destructibles detailed spec
- [specs/animation-system.md](../specs/animation-system.md) — Character animation blending
- [guides/lua-scripting-guide.md](lua-scripting-guide.md) — Lua API reference
- [guides/editor-workflow.md](editor-workflow.md) — Editor playtest and debugging

---

## Troubleshooting Checklist

Before filing a bug, verify:

- [ ] Physics debug draw enabled; can see bodies/colliders
- [ ] Collision layer matrix allows bodies to interact
- [ ] Rigid body mass > 0 (not static)
- [ ] Collider offset is appropriate for mesh
- [ ] Physics timestep is fixed (60 Hz)
- [ ] No scripts blocking physics callbacks
- [ ] Performance profiler shows physics time < 5ms

If still stuck:
1. Simplify to minimal scene (1 vehicle, 1 cloth, etc.)
2. Use console commands to inspect state
3. Enable physics stats and check body/constraint counts
4. Check error log for Lua exceptions in physics callbacks

---

## Tips & Tricks

### Quick Vehicle Tuning

Start with default values, then adjust one parameter at a time:

```lua
-- Adjust stiffness in real-time
if ctx.input:pressed("IncreaseStiffness") then
    ctx.console:execute("vehicle.suspension_stiffness 30000")
end
```

### Cloth Blending Curves

Use easing functions for smooth blend transitions:

```lua
local function ease_in_cubic(t)
    return t * t * t
end

function M:set_blend_with_easing(ctx, target_blend, duration)
    self.blend_start = ctx.time
    self.blend_target = target_blend
    self.blend_duration = duration
end

function M:on_update(ctx, dt)
    if self.blend_duration then
        local elapsed = ctx.time - self.blend_start
        local t = math.min(elapsed / self.blend_duration, 1.0)
        local eased_t = ease_in_cubic(t)
        local current_blend = lerp(self.blend_start_value, self.blend_target, eased_t)
        ctx.cloth:set_blend(self, current_blend)
    end
end
```

### Debugging Cloth Wind

Visualize wind force with particle effect:

```lua
function M:visualize_wind(ctx)
    local wind_force = ctx.cloth:get_wind_force(self)
    if wind_force > 0 then
        -- Spawn wind particles at cloth position
        local vfx = ctx.scene:spawn_node("wind_vfx", self.position)
        vfx.particle_count = math.floor(wind_force * 10)  -- scale by force
    end
end
```

