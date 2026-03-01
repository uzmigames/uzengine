# Particle System Spec

## Overview

The particle system simulates and renders up to 65,536 particles per emitter using GPU compute shaders. Each emitter references a `.uzfx` plain-text effect definition file. Simulation runs entirely on the GPU — no CPU readback — and particles are rendered as billboard quads via `DrawIndexedInstanced`. The system is AI-friendly: a designer or LLM can author effects by editing `.uzfx` text files directly.

---

## 1. Effect File Format (`.uzfx`)

`.uzfx` is an INI-like plain-text format. Each file describes one particle emitter: its shape, particle properties, optional behavior modules, and renderer settings.

### Minimal Example

```ini
[uzfx version=1 name="Campfire"]

; Emitter shape
emitter_type      = cone
cone_angle        = 15.0          ; degrees half-angle
cone_radius       = 0.1           ; base radius (meters)

; Emission rate
emission_rate     = 40            ; particles per second (continuous)
burst_count       = 0             ; particles spawned at t=0 (one-shot burst)
duration          = 0.0           ; 0 = infinite loop
looping           = true

; Particle lifetime
lifetime_min      = 1.2
lifetime_max      = 2.0

; Size
start_size_min    = 0.05
start_size_max    = 0.12
end_size          = 0.0           ; shrink to nothing

; Color (linear RGB + alpha)
start_color       = [1.0, 0.55, 0.1, 1.0]
end_color         = [0.3, 0.08, 0.0, 0.0]

; Velocity
start_velocity_dir = [0.0, 1.0, 0.0]   ; local-space direction (normalized)
start_speed_min    = 0.8
start_speed_max    = 1.5
gravity_scale      = -0.1               ; negative = particles float upward
drag               = 0.4                ; velocity damping per second (0..1)

; Renderer
texture            = "res://textures/vfx/smoke.png"
blend_mode         = additive           ; additive | alpha | premultiplied
sort_order         = 0                  ; draw order among emitters (lower = first)
cast_shadow        = false
receive_lighting   = false
```

### Full Property Reference

#### `[uzfx]` Header

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `version` | int | 1 | File format version |
| `name` | string | — | Human-readable name |

#### Emitter Shape

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `emitter_type` | enum | `point` | `point`, `sphere`, `cone`, `box`, `mesh_surface` |
| `sphere_radius` | float | 0.5 | Spawn radius (sphere type) |
| `cone_angle` | float | 25.0 | Half-angle in degrees (cone type) |
| `cone_radius` | float | 0.0 | Base radius offset (cone type) |
| `box_half_extents` | float3 | [0.5, 0.5, 0.5] | Box half-sizes (box type) |
| `mesh_surface` | string | — | `res://` path to mesh asset (mesh_surface type) |
| `emission_rate` | float | 10.0 | Particles per second (steady emission) |
| `burst_count` | int | 0 | Particles emitted at effect start |
| `duration` | float | 0.0 | Effect duration in seconds; 0 = infinite |
| `looping` | bool | true | Restart after duration ends |

#### Particle Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `lifetime_min` | float | 1.0 | Minimum particle lifetime (seconds) |
| `lifetime_max` | float | 2.0 | Maximum particle lifetime (seconds) |
| `start_size_min` | float | 0.1 | Minimum initial size (meters, world-space) |
| `start_size_max` | float | 0.1 | Maximum initial size |
| `end_size` | float | 0.0 | Size at end of lifetime (linear interpolation) |
| `start_color` | float4 | [1,1,1,1] | Initial color RGBA linear (used if no color module) |
| `end_color` | float4 | [1,1,1,0] | Final color RGBA linear |
| `start_velocity_dir` | float3 | [0,1,0] | Initial velocity direction (local space, normalized) |
| `start_speed_min` | float | 1.0 | Minimum initial speed (m/s) |
| `start_speed_max` | float | 1.0 | Maximum initial speed (m/s) |
| `gravity_scale` | float | 1.0 | Gravity multiplier (1 = full gravity, 0 = weightless) |
| `drag` | float | 0.0 | Velocity reduction per second (0 = none, 1 = instant stop) |
| `rotation_speed_min` | float | 0.0 | Billboard rotation speed (deg/s) |
| `rotation_speed_max` | float | 0.0 | Billboard rotation speed (deg/s) |

#### Renderer

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `texture` | string | — | `res://` path to texture (PNG/DDS) |
| `blend_mode` | enum | `alpha` | `alpha`, `additive`, `premultiplied` |
| `sort_order` | int | 0 | Render order among simultaneous emitters |
| `cast_shadow` | bool | false | Particles cast shadows (expensive) |
| `receive_lighting` | bool | false | Particles lit by scene lights |
| `sheet_columns` | int | 1 | Flipbook texture columns |
| `sheet_rows` | int | 1 | Flipbook texture rows |
| `sheet_fps` | float | 0.0 | Flipbook animation frames per second |

---

## 2. Behavior Modules

Modules are optional INI sections appended after the base properties. They are applied in a stack — each module modifies particle state each simulation step. All modules are optional.

### `[module.velocity_over_lifetime]`

Applies an additional velocity vector that scales over normalized particle age [0..1].

```ini
[module.velocity_over_lifetime]
; velocity added each second, scaled by curve (linear for now)
velocity_start = [0.0, 0.0, 0.0]
velocity_end   = [0.2, 0.0, 0.0]   ; drift right over lifetime
space          = world              ; world | local
```

### `[module.color_over_lifetime]`

Overrides the base `start_color` → `end_color` linear interpolation with a multi-stop gradient.

```ini
[module.color_over_lifetime]
; up to 8 color stops (time 0..1 normalized particle age)
stop_0 = [0.0,  1.0, 0.6, 0.1, 1.0]   ; time=0.0, RGBA
stop_1 = [0.3,  1.0, 0.3, 0.0, 0.9]
stop_2 = [0.7,  0.5, 0.1, 0.0, 0.4]
stop_3 = [1.0,  0.0, 0.0, 0.0, 0.0]
stop_count = 4
```

### `[module.size_over_lifetime]`

Overrides the base `start_size` → `end_size` linear interpolation.

```ini
[module.size_over_lifetime]
; size as fraction of start_size
stop_0 = [0.0, 0.0]     ; start at 0% (burst in)
stop_1 = [0.1, 1.0]     ; reach full size at 10% lifetime
stop_2 = [0.8, 1.0]     ; hold full size
stop_3 = [1.0, 0.0]     ; shrink to zero
stop_count = 4
```

### `[module.rotation_over_lifetime]`

Applies a constant angular velocity to the billboard, spinning over time.

```ini
[module.rotation_over_lifetime]
rotation_speed_min = -90.0   ; deg/s
rotation_speed_max =  90.0   ; each particle gets random value in range
```

### `[module.noise_velocity]`

Adds curl-noise-based turbulence to particle trajectories.

```ini
[module.noise_velocity]
strength    = 0.5    ; noise displacement magnitude (m/s)
frequency   = 1.0    ; noise frequency (higher = tighter swirls)
scroll_speed = 0.3   ; how fast noise field scrolls (world-space, m/s)
octaves     = 2      ; noise octave count (1..4, higher = more detail, more expensive)
```

---

## 3. GPU Simulation Architecture

### StructuredBuffer Layout

Each particle occupies one element in a GPU `StructuredBuffer<Particle>`:

```hlsl
struct Particle {
    float3 position;         // world-space position
    float  lifetime_remain;  // remaining lifetime (seconds); < 0 = dead

    float3 velocity;         // current velocity (m/s)
    float  lifetime_total;   // total lifetime for age normalization

    float4 color;            // current RGBA (linear)

    float  size;             // current world-space size (m)
    float  rotation;         // billboard rotation (radians)
    uint   seed;             // per-particle random seed
    float  _pad;
};
```

Total size: 64 bytes per particle. One emitter with 65,536 particles = 4 MB GPU buffer.

### Compute Pass Layout

Each frame, two compute dispatches are executed per active emitter:

1. **Simulate dispatch** (`simulate.hlsl`) — updates live particles, handles death.
2. **Emit dispatch** (`emit.hlsl`) — spawns new particles up to `emission_rate * dt` budget.

Both dispatches use an `AppendStructuredBuffer`/`ConsumeStructuredBuffer` dead-list pattern to avoid GPU-side atomic pressure:

```
DeadList   — UAV counter buffer: list of free particle indices
AliveList  — UAV counter buffer: list of live indices (rebuilt each frame)
ParticleBuffer — main particle data (read/write)
```

### Simulate Dispatch

```hlsl
// Thread group: [64, 1, 1]  —  dispatch ceil(max_particles / 64) groups
[numthreads(64, 1, 1)]
void CS_Simulate(uint3 id : SV_DispatchThreadID)
{
    uint idx = id.x;
    Particle p = ParticleBuffer[idx];

    if (p.lifetime_remain <= 0) return;  // already dead

    p.lifetime_remain -= g_DeltaTime;

    if (p.lifetime_remain <= 0) {
        DeadList.Append(idx);            // return to free pool
        ParticleBuffer[idx].lifetime_remain = -1;
        return;
    }

    // Physics integration
    p.velocity += g_Gravity * g_ParticleGravityScale * g_DeltaTime;
    p.velocity *= (1.0 - g_Drag * g_DeltaTime);
    p.position += p.velocity * g_DeltaTime;

    // Module stack — applied in order
    ApplyVelocityOverLifetime(p);
    ApplyNoiseVelocity(p);

    // Age-based interpolation
    float age_t = 1.0 - (p.lifetime_remain / p.lifetime_total);
    p.color = SampleColorOverLifetime(age_t);
    p.size  = SampleSizeOverLifetime(age_t);
    p.rotation += g_RotationSpeed * g_DeltaTime;

    ParticleBuffer[idx] = p;
    AliveList.Append(idx);
}
```

### Render Pass

Alive particles are rendered as billboard quads without a vertex buffer:

```hlsl
// VS generates quad corners from SV_VertexID % 4
// SV_InstanceID indexes into AliveList → reads Particle from StructuredBuffer
// Geometry: 2 triangles (indices 0,1,2 + 0,2,3) via DrawIndexedInstanced
DrawIndexedInstanced(
    /*index_count_per_instance*/ 6,
    /*instance_count*/           alive_count,  // from indirect args buffer
    0, 0, 0
);
```

Billboard alignment: particles face the camera view plane (`CameraRight`, `CameraUp` extracted from view matrix). Optional `velocity_aligned` mode (trails) aligns the quad along the velocity vector.

---

## 4. ECS Integration

### Component

```cpp
struct CParticleEmitter {
    ResourceHandle   effect;           // .uzfx asset handle
    bool             playing = true;   // running or stopped
    float            playback_time = 0.0f;
    float            emit_accumulator = 0.0f;  // fractional carry between frames

    // Runtime GPU state (not serialized)
    uint32_t         gpu_emitter_index = UINT32_MAX;  // slot in EmitterPool
};
```

### Scene Format Integration

```ini
[node id=7 type=ParticleEmitter name="CampfireSmoke" parent=6]
effect    = "res://fx/campfire_smoke.uzfx"
playing   = true
position  = [0.0, 0.3, 0.0]
```

### EmitterPool

The `VFXSystem` maintains a fixed pool of 256 GPU emitter slots. Each slot owns:
- A `ParticleBuffer` (StructuredBuffer, 65,536 entries, 4 MB)
- A `DeadList` counter buffer
- An `AliveList` counter buffer
- An indirect args buffer (populated by `Simulate` pass)

Emitters beyond 256 are queued and activated as slots become free.

---

## 5. Lua API

```lua
-- Spawn a one-shot VFX effect at a world position
-- Returns a handle that can be used to stop or query the effect
local handle = ctx.scene:spawn_vfx("res://fx/explosion.uzfx", position)

-- Spawn with optional parameters
local handle = ctx.scene:spawn_vfx("res://fx/explosion.uzfx", position, {
    scale = 1.5,         -- uniform scale applied to effect
    rotation = vec3(0, 45, 0),  -- rotation offset (Euler degrees)
    parent = some_node_id  -- attach to a node (follows transform)
})

-- Stop an effect (triggers any stop-burst and fades out)
ctx.vfx:stop(handle)

-- Immediately kill all particles (no fade)
ctx.vfx:stop_immediate(handle)

-- Change the emission rate at runtime (useful for rain intensity, etc.)
ctx.vfx:set_rate(handle, rate)  -- particles per second; 0 = pause emission

-- Query state
local alive = ctx.vfx:is_alive(handle)  -- false if effect finished and handle is stale
local count = ctx.vfx:get_particle_count(handle)  -- current live particle count
```

### Example: Footstep Dust Script

```lua
-- Attached to player node; fires footstep dust on AnimNotify
function on_anim_event(event_name, params)
    if event_name == "footstep_left" then
        local foot_pos = ctx.scene:get_bone_world_pos(self.id, "foot_l")
        ctx.scene:spawn_vfx("res://fx/footstep_dust.uzfx", foot_pos)
    end
end
```

---

## 6. AnimNotify Integration

In `.uzanim` notify sections, VFX can be spawned at a specific frame using the `spawn_vfx` event:

```ini
[notify time=0.42 event=spawn_vfx]
asset       = "res://fx/footstep.uzfx"
attach_bone = "foot_l"
offset      = [0.0, 0.0, 0.0]    ; local offset from bone
```

The animation system's `EventBus` routes `spawn_vfx` events to `VFXSystem::spawn_at_bone()`.

---

## 7. Limits and LOD

### Limits

| Constraint | Value |
|------------|-------|
| Max particles per emitter | 65,536 |
| Max simultaneous emitters per scene | 256 |
| GPU buffer per emitter | 4 MB |
| Max behavior modules per effect | 8 (any combination) |
| Max color stops per gradient | 8 |

### Distance-Based LOD

The `VFXSystem` reduces simulation cost at distance:

| Distance from camera | Sim rate | Render |
|----------------------|----------|--------|
| 0 – 10 m | 100% (every frame) | Full |
| 10 – 50 m | 100% | Full |
| > 50 m | 25% (every 4 frames) | Full |
| > 100 m | Simulation paused | Invisible |

At > 100 m the emitter stops simulating but retains its GPU slot. Simulation resumes when the camera moves within 80 m (hysteresis band avoids flickering).

### Particle-World Collision (Optional)

When `collision = true` is set on an emitter, particles test against a low-resolution simplified collision mesh (convex hulls baked by `UzBuild`):

```ini
; Optional collision in .uzfx
collision         = true
collision_bounce  = 0.3    ; restitution coefficient (0 = absorb, 1 = perfect bounce)
collision_friction = 0.5
```

Collision is implemented in the compute shader as a sphere-vs-triangle sweep. Maximum 4 triangles tested per particle per frame (closest by AABB overlap). Disabled by default — only enable for particles where it is visually significant (rain drops, sparks).

---

## 8. Editor Integration

- When a `ParticleEmitter` node is selected in the Scene Tree, the effect plays (or loops) in the viewport.
- A **VFX Preview** button in the Inspector restarts the effect from t=0.
- Particle count is shown live in the Inspector status bar.
- Effect file path can be set via drag-drop from the Asset Browser.
- Stopping the engine (Play Mode stop) calls `VFXSystem::stop_all()`.

---

## 9. IPC Handlers

```
vfx.spawn      { path, position, scale?, rotation?, parent_id? }  → { handle }
vfx.stop       { handle }
vfx.setRate    { handle, rate }
vfx.getState   { handle }  → { alive, particle_count, playback_time }
vfx.stopAll    {}
```

---

## 10. C++ System API

```cpp
class VFXSystem {
public:
    // Called by SceneManager when a ParticleEmitter node is added/removed
    void register_emitter(NodeId node, CParticleEmitter& comp);
    void unregister_emitter(NodeId node);

    // Called each frame — simulate + build indirect args + render
    void update(float dt, const Camera& cam);
    void render(ID3D12GraphicsCommandList* cmd, const FrameContext& frame);

    // Spawn a transient VFX (not tied to a scene node)
    VFXHandle spawn(const std::string& path, glm::vec3 pos,
                    float scale = 1.0f, NodeId parent = {});
    void stop(VFXHandle handle, bool immediate = false);
    void stop_all();
    void set_rate(VFXHandle handle, float rate);

    // Called by AnimNotify system
    void spawn_at_bone(const std::string& path, const Skeleton& sk,
                       uint16_t bone_idx, glm::vec3 offset);
};
```
