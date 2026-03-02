# VFX & Particle System Editor

## 1. Overview & Goals

The VFX & Particle System Editor is a dedicated tool within UzEngine's VSCode-based editor for authoring, previewing, and tuning GPU-accelerated particle effects. It provides a real-time, interactive environment comparable to Unreal Engine's Niagara or Unity's VFX Graph, but designed for UzEngine's AI-native philosophy: every effect is stored as a plain-text `.uzfx` file that both humans and LLMs can author and modify.

### Design Goals

| Goal | Description |
|------|-------------|
| GPU compute simulation | Up to 65,536 particles per emitter, zero CPU readback |
| Real-time preview | Live simulation in a 3D viewport with environment context |
| Module-based authoring | Spawn, Update, and Render phases composed from reusable modules |
| Optional node graph | Visual graph for complex behaviors (alternative to the module stack) |
| Curve-driven animation | RGBA color, size, rotation, opacity over particle lifetime |
| AI-friendly persistence | `.uzfx` INI-like format, fully editable by text or LLM |
| Multi-emitter composition | Complex effects built from multiple emitters with timing offsets |
| LOD support | Distance-based quality reduction for scalable performance |

### Target Users

- **VFX Artists**: Primary users. Need visual tools for rapid iteration on fire, smoke, sparks, magic, weather.
- **Technical Artists**: Power users who need the node graph, custom modules, and direct compute shader access.
- **AI Agents**: LLMs that generate `.uzfx` files from text descriptions (via `ai.generate_vfx` MCP tool).

### Integration Points

- Reads/writes `.uzfx` files (see Section 10)
- Uses the existing `VFXSystem` from `particle-system.md` for simulation
- Renders via the DX12 Forward+ pipeline (transparent pass) from `renderer.md`
- Particle emitter nodes are placed in `.uzscene` files per `scene-system.md`
- Shader variants use the `PARTICLE` define per `shader-variants.md`
- Preview streams via the existing JPEG+WebSocket pipeline

---

## 2. Editor Layout

The VFX Editor opens as a dedicated editor tab group in VSCode when a `.uzfx` file is double-clicked or when a `ParticleEmitter` node is selected and "Open in VFX Editor" is chosen.

### Full Layout Mockup

```
+-------------------------------------------------------------------------------------------+
|  VFX Editor - campfire.uzfx                                                    [x]        |
+-------------------------------------------------------------------------------------------+
|                                                                                           |
| EMITTER STACK  | MODULE STACK             |  3D VIEWPORT                | PROPERTIES      |
| (left panel)   | (center-left)            |  (center-large)             | (right panel)   |
|                |                          |                             |                 |
| [+ Add Emitter]| Flames > Spawn Phase    |  +-------------------------+| >> Module:      |
|                |                          |  |          ,              || SpawnRate       |
| [*] Flames     |  [SpawnRate        ]     |  |        ,;|;,            ||                 |
|     playing    |  [SpawnShape: Cone ]     |  |       ,;|||;,           || Rate    [40   ] |
|                |  [InitialVelocity  ]     |  |      ';|||;'            || Burst   [0    ] |
| [ ] Embers     |  [InitialLifetime  ]     |  |       '|||'             || Duration[0.0  ] |
|     playing    |  [InitialColor     ]     |  |    ....'''....          || Looping [x]     |
|                |  [InitialSize      ]     |  |   ..  ;||;  ..         ||                 |
| [ ] Smoke      |                          |  |  .   ;|~~|;   .        || --------        |
|     playing    | Flames > Update Phase    |  |  . --|Grid|--  .       || Presets:        |
|                |                          |  |  .   '~~~~'   .        || [Steady]        |
| [Reorder: ^ v] |  [Gravity: -0.1   ]     |  |   ..        ..         || [Burst ]        |
|                |  [Drag: 0.4        ]     |  |    ''......''          || [Pulse ]        |
|                |  [ColorOverLifetime]     |  |                        ||                 |
|                |  [SizeOverLifetime ]     |  | Particles: 847         ||                 |
|                |  [NoiseVelocity    ]     |  | GPU: 0.12ms            ||                 |
|                |                          |  | Draw Calls: 3          ||                 |
|                | Flames > Render Phase    |  | Emitters: 3/256        ||                 |
|                |                          |  +-------------------------+|                 |
|                |  [SpriteRenderer   ]     |                             |                 |
|                |  [BlendMode: Add   ]     |  [|<] [<] [>>] [>|] [O]    |                 |
|                |  [SortMode: None   ]     |  Speed: [0.1x|0.25|0.5|1x|2x]               |
|                |                          |                             |                 |
+-------------------------------------------------------------------------------------------+
| CURVE EDITOR                                        | NODE GRAPH (optional, toggle)       |
|                                                     |                                     |
|  Color Over Lifetime (Flames)                       |  [SpawnRate]-->[Cone]-->[InitVel]   |
|  1.0 |  R                                           |       |                    |        |
|      | RR                                           |       v                    v        |
|  0.5 |  RRRGGGG                                     |  [Lifetime]            [Color]      |
|      |      GGGBB                                   |       |                    |        |
|  0.0 |__________BBBB____                            |       v                    v        |
|      0.0    0.5    1.0   age                        |  [Gravity]-->[Noise]-->[Sprite]     |
|                                                     |                                     |
|  Size Over Lifetime (Flames)                        |  [+ Add Module]  [Auto Layout]      |
|  1.0 |   xxxxxxxx                                   |                                     |
|      |  x        xx                                 |                                     |
|  0.5 | x           xxx                              |                                     |
|      |x               xxxx                           |                                     |
|  0.0 |____________________x                          |                                     |
|      0.0    0.5    1.0   age                        |                                     |
+-------------------------------------------------------------------------------------------+
| ASSET BROWSER (bottom)                                                                    |
|  [Sprites]  [Meshes]  [Materials]  [Textures]  [Presets]                                  |
|  smoke.png | fire_01.png | spark.png | dust.png | rain_drop.png | magic_orb.glb          |
+-------------------------------------------------------------------------------------------+
```

### Panel Descriptions

| Panel | VSCode API | Purpose |
|-------|-----------|---------|
| 3D Viewport | `WebviewPanel` (canvas, JPEG stream) | Live particle simulation with environment |
| Emitter Stack | `WebviewPanel` (HTML list) | Add/remove/reorder/toggle emitters |
| Module Stack | `WebviewPanel` (HTML list) | Per-emitter module list, grouped by phase |
| Node Graph | `WebviewPanel` (SVG/canvas) | Optional visual graph for complex behaviors |
| Curve Editor | `WebviewPanel` (canvas 2D) | Edit lifetime curves for color, size, rotation |
| Properties Inspector | `WebviewPanel` (Webview UI Toolkit) | Edit selected module parameters |
| Asset Browser | `TreeDataProvider` | Sprites, meshes, materials for particle rendering |

### 3D Viewport Details

The viewport renders the particle effect in an environment context to help artists evaluate the effect in-situ.

**Environment Options:**

| Feature | Description | Default |
|---------|-------------|---------|
| Ground Plane | Infinite grid plane with shadow reception | Enabled, Y=0 |
| Background Scene | Load a `.uzscene` as backdrop (e.g., a room, outdoor) | None (dark gradient) |
| Wind Indicator | Arrow glyph showing active wind vector | Hidden |
| Skybox | HDR environment cubemap | Default grey gradient |
| Reference Objects | Toggleable 1m cube, human silhouette for scale | Hidden |

**Stats Overlay** (always visible, bottom-left of viewport):

```
Particles: 1,247 / 65,536
GPU Time:  0.14 ms (sim) + 0.08 ms (render)
Draw Calls: 3
Emitters:  3 / 256 active slots
Memory:    12.0 MB (3 x 4 MB buffers)
```

---

## 3. Emitter System

Each `.uzfx` effect contains one or more **emitters**. An emitter is a self-contained particle generator with its own spawn rules, update behaviors, and renderer.

### Emitter Types

| Type | Description | Render Method | Use Cases |
|------|-------------|---------------|-----------|
| **Sprite** | Camera-facing billboard quads | `DrawIndexedInstanced` (6 indices x N) | Fire, smoke, sparks, dust, magic |
| **Mesh** | 3D mesh replicated per particle | Instanced mesh draw | Debris, leaves, shrapnel, coins |
| **Ribbon** | Connected strip along emission order | Triangle strip from consecutive particles | Sword trails, light streaks, wires |
| **Beam** | Straight or curved line between two points | Triangle strip with tangent computation | Lasers, lightning, energy tethers |
| **Light** | Dynamic point light per particle | Light list injection (no geometry) | Fireflies, magic sparks, explosions |

### Emitter Lifecycle

```
                        +-------------------------------------------+
                        |              Emitter Lifetime              |
                        +-------------------------------------------+
                        |                                           |
  t=0    Activation --> | SPAWN PHASE --> UPDATE PHASE --> RENDER   |
                        |   (birth)        (simulate)     (draw)    |
                        |                                           |
                        | Each frame:                               |
                        |   1. Spawn new particles (rate + burst)   |
                        |   2. Update all alive particles           |
                        |   3. Kill expired particles               |
                        |   4. Build draw args                      |
                        |   5. Render alive particles                |
                        |                                           |
  t=duration            | DEACTIVATION (stop spawning)              |
                        | Remaining particles live out lifetime     |
                        |                                           |
  last particle dies -> | COMPLETE (slot freed)                     |
                        +-------------------------------------------+
```

### Emitter Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `name` | string | "Emitter" | Human-readable identifier |
| `type` | enum | `sprite` | `sprite`, `mesh`, `ribbon`, `beam`, `light` |
| `enabled` | bool | true | Toggle emitter on/off without removing |
| `delay` | float | 0.0 | Seconds to wait before first spawn |
| `duration` | float | 0.0 | Emission duration (0 = infinite) |
| `looping` | bool | true | Restart after duration ends |
| `max_particles` | uint | 65536 | Maximum alive particles for this emitter |
| `simulation_space` | enum | `local` | `local` (follows parent transform) or `world` |
| `gpu_sim` | bool | true | GPU compute simulation (false = CPU fallback) |
| `random_seed` | uint | 0 | Fixed seed for deterministic playback (0 = random) |

### Emitter Inheritance

Emitters can inherit from a **base template** and override specific properties. This enables studio-wide consistency (e.g., all fire effects share the same base color ramp, but individual fires override size and rate).

```ini
[emitter name="CampfireFlames" base="res://fx/templates/fire_base.uzfx:Flames"]
; Only overridden properties are listed
emission_rate = 40
cone_angle    = 15.0
```

Resolution order: emitter properties > base template properties > system defaults.

### GPU vs CPU Toggle

| Mode | Implementation | When to Use |
|------|---------------|-------------|
| **GPU** (default) | `CS_ParticleSpawn.hlsl` + `CS_ParticleUpdate.hlsl` | 99% of cases. Required for >1000 particles. |
| **CPU** | C++ `CPUParticleEmitter::update()` in game.dll | Complex game-logic-driven particles (homing missiles, AI-aware) |

CPU mode is limited to 4,096 particles per emitter. CPU particles are uploaded to a dynamic `StructuredBuffer` each frame for rendering via the same draw path.

---

## 4. Module System (Spawn Phase)

Spawn modules execute once per particle at the moment of birth. They initialize all particle attributes. Modules are applied in stack order (top to bottom in the Module Stack panel).

### Module: SpawnRate

Controls how many particles are born per frame.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `rate` | float | 10.0 | Continuous emission rate (particles/second) |
| `burst_count` | int | 0 | Particles spawned instantly at emitter start |
| `burst_cycles` | int | 1 | Number of burst repetitions (0 = infinite) |
| `burst_interval` | float | 0.0 | Seconds between burst cycles |

**Rate modes:**

```
Steady:     ||||||||||||||||||||||||||||     (constant rate)
Burst:      |||||||                          (all at once, then stop)
Pulse:      |||||     |||||     |||||        (burst + interval + repeat)
Curve:      ||  ||||  ||||||  ||||  ||       (rate driven by lifetime curve)
```

### Module: SpawnShape

Defines the spatial distribution of particle birth positions.

| Shape | Parameters | Diagram |
|-------|-----------|---------|
| **Point** | (none) | All particles spawn at emitter origin |
| **Box** | `half_extents: float3` | Random position within axis-aligned box |
| **Sphere** | `radius: float`, `surface_only: bool` | Random within sphere or on surface shell |
| **Ring** | `radius: float`, `width: float` | Torus cross-section (donut) |
| **Cone** | `angle: float`, `radius: float`, `length: float` | Cone frustum, spawn along length |
| **Mesh Surface** | `mesh: res:// path`, `distribution: enum` | Random point on mesh triangles (area-weighted) |

```
  Point          Box          Sphere         Ring           Cone
    *         +------+         ____          .---.        .'   '.
              |  * * |       .' *  '.       /  *  \      /  * *  \
              | *  * |      ( *  *   )     (  *  * )    /  *    *  \
              +------+       '.____.'       \ * * /    '___________'
                                             '---'
```

**Mesh Surface distribution modes:**

| Mode | Description |
|------|-------------|
| `uniform` | Area-weighted random sampling (larger triangles get more particles) |
| `vertex` | Spawn only at mesh vertices |
| `edge` | Spawn along mesh edges |

### Module: InitialVelocity

Sets the initial velocity vector for each spawned particle.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `mode` | enum | `directional` | `directional`, `random_cone`, `outward`, `inherit` |
| `direction` | float3 | [0,1,0] | Direction vector (local space, normalized) |
| `speed_min` | float | 1.0 | Minimum initial speed (m/s) |
| `speed_max` | float | 1.0 | Maximum initial speed (m/s) |
| `cone_angle` | float | 15.0 | Half-angle for `random_cone` mode (degrees) |
| `inherit_factor` | float | 0.0 | Fraction of parent emitter velocity to inherit |

### Module: InitialLifetime

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `min` | float | 1.0 | Minimum lifetime in seconds |
| `max` | float | 2.0 | Maximum lifetime in seconds |

### Module: InitialSize

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `mode` | enum | `uniform` | `uniform` (single float) or `non_uniform` (float2, width x height) |
| `min` | float | 0.1 | Minimum initial size (meters) |
| `max` | float | 0.1 | Maximum initial size (meters) |

### Module: InitialColor

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `mode` | enum | `constant` | `constant`, `random_range`, `random_gradient` |
| `color` | float4 | [1,1,1,1] | Constant color (linear RGBA) |
| `color_min` | float4 | [1,1,1,1] | Min color for `random_range` |
| `color_max` | float4 | [1,1,1,1] | Max color for `random_range` |
| `gradient` | string | "" | Named gradient from curve editor for `random_gradient` |

### Module: InitialRotation

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `min` | float | 0.0 | Minimum initial rotation (degrees) |
| `max` | float | 360.0 | Maximum initial rotation (degrees) |
| `align_to_velocity` | bool | false | Override rotation to face velocity direction |

---

## 5. Module System (Update Phase)

Update modules execute every frame for every alive particle. They modify particle state (position, velocity, color, size, rotation). Modules are applied in stack order.

### Module: Gravity

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `scale` | float | 1.0 | Multiplier on world gravity [0,-9.81,0] |

Implementation: `p.velocity += gravity * scale * dt`

### Module: Wind

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `direction` | float3 | [1,0,0] | Wind direction (world space, normalized) |
| `strength` | float | 1.0 | Wind force (m/s^2) |
| `turbulence` | float | 0.0 | Random variation in wind direction (0..1) |
| `drag_coefficient` | float | 0.0 | How much particles are affected by wind based on size |

### Module: Drag

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `coefficient` | float | 0.0 | Velocity damping per second (0 = none, 1 = instant stop) |

Implementation: `p.velocity *= (1.0 - drag * dt)`

### Module: PointAttractor

Pulls particles toward (or pushes away from) a point in space.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `position` | float3 | [0,0,0] | Attractor position (local or world space) |
| `strength` | float | 1.0 | Force magnitude (negative = repulse) |
| `radius` | float | 5.0 | Maximum influence radius (falloff beyond) |
| `falloff` | enum | `inverse_square` | `constant`, `linear`, `inverse_square` |
| `kill_on_reach` | bool | false | Destroy particle when it reaches the center |

### Module: Vortex

Applies rotational force around an axis.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `axis` | float3 | [0,1,0] | Vortex rotation axis |
| `strength` | float | 1.0 | Rotational speed (rad/s) |
| `pull_strength` | float | 0.0 | Inward pull toward axis (creates spiral) |

### Module: Orbit

Particles orbit around the emitter origin.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `speed` | float3 | [0,1,0] | Orbital speed per axis (rad/s) |
| `offset` | float3 | [1,0,0] | Orbital radius offset |
| `phase_randomize` | bool | true | Randomize starting phase per particle |

### Module: Collision

Particles collide with geometry.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `mode` | enum | `plane` | `plane`, `scene_mesh`, `depth_buffer` |
| `plane_normal` | float3 | [0,1,0] | Collision plane normal (for `plane` mode) |
| `plane_offset` | float | 0.0 | Plane distance from origin |
| `bounce` | float | 0.3 | Restitution coefficient (0..1) |
| `friction` | float | 0.5 | Friction coefficient (0..1) |
| `lifetime_loss` | float | 0.0 | Fraction of remaining lifetime lost on collision |
| `on_collide` | enum | `bounce` | `bounce`, `stick`, `kill`, `spawn_sub_emitter` |

**Depth buffer collision** (GPU-only): reads the scene depth buffer to approximate world geometry without explicit collision meshes. Cheap but imprecise.

### Module: ColorOverLifetime

Overrides particle color using a multi-stop gradient sampled by normalized age.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `gradient` | CurveRGBA | white->transparent | Up to 8 color stops (see Section 7) |

### Module: SizeOverLifetime

Overrides particle size using a curve sampled by normalized age.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `curve` | CurveFloat | 1.0->0.0 | Size multiplier curve (see Section 7) |

### Module: RotationOverLifetime

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `speed_min` | float | 0.0 | Minimum angular velocity (deg/s) |
| `speed_max` | float | 0.0 | Maximum angular velocity (deg/s) |

### Module: NoiseVelocity (Turbulence)

Adds GPU simplex noise displacement to particle trajectories.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `strength` | float | 0.5 | Noise displacement magnitude (m/s) |
| `frequency` | float | 1.0 | Noise frequency (higher = tighter swirls) |
| `scroll_speed` | float | 0.3 | How fast the noise field evolves (m/s) |
| `octaves` | int | 2 | Noise octaves (1..4, more = more detail, more ALU) |
| `mode` | enum | `curl` | `curl` (divergence-free) or `simplex` (raw displacement) |

Curl noise produces swirling, smoke-like motion. Simplex noise produces more chaotic displacement.

### Module: CurlNoise

Specialized curl noise that produces divergence-free vector fields, ideal for fluid-like motion.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `strength` | float | 1.0 | Force magnitude |
| `scale` | float | 1.0 | Spatial scale of the noise field |
| `speed` | float | 0.5 | Temporal evolution speed |

### Module: VectorFieldSampling

Samples a 3D vector field texture to drive particle motion.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `field` | string | "" | `res://` path to `.uzvf` (3D texture, R16G16B16A16_FLOAT) |
| `strength` | float | 1.0 | Velocity scale multiplier |
| `bounds` | float3 | [2,2,2] | World-space bounds of the vector field volume |
| `tiling` | bool | false | Tile the field at volume boundaries |

### Module: SubEmitter

Triggers a child emitter when a particle event occurs.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `trigger` | enum | `death` | `death`, `collision`, `lifetime_threshold` |
| `effect` | string | "" | `res://` path to `.uzfx` file to spawn |
| `inherit_velocity` | float | 0.5 | Fraction of parent velocity inherited by sub-emitter |
| `inherit_color` | bool | false | Sub-emitter inherits parent particle color |
| `inherit_size` | float | 0.5 | Sub-emitter size scale relative to parent |
| `lifetime_threshold` | float | 0.5 | Normalized age trigger (for `lifetime_threshold` mode) |
| `max_children` | int | 8 | Max sub-emitter instances per parent particle |

**Sub-emitter depth limit**: Maximum 2 levels deep (emitter -> sub-emitter -> sub-sub-emitter). Prevents runaway particle counts.

---

## 6. Module System (Render Phase)

Render modules define how particles are drawn. Each emitter has exactly one render module.

### Module: SpriteRenderer

Camera-facing billboard quads. The default and most common renderer.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `texture` | string | "" | `res://` path to sprite texture (PNG/DDS) |
| `alignment` | enum | `camera_facing` | `camera_facing`, `velocity_aligned`, `fixed_axis`, `custom` |
| `fixed_axis` | float3 | [0,1,0] | Axis for `fixed_axis` alignment |
| `blend_mode` | enum | `alpha` | `alpha`, `additive`, `premultiplied`, `multiply` |
| `sort_mode` | enum | `none` | `none`, `front_to_back`, `back_to_front`, `by_age` |
| `cast_shadow` | bool | false | Particles cast shadows (expensive, use sparingly) |
| `receive_lighting` | bool | false | Particles receive scene lighting |
| `soft_particles` | bool | false | Fade at depth buffer intersections |
| `soft_distance` | float | 0.5 | Fade distance for soft particles (meters) |
| `sheet_columns` | int | 1 | Flipbook animation columns |
| `sheet_rows` | int | 1 | Flipbook animation rows |
| `sheet_fps` | float | 0.0 | Flipbook animation speed |
| `sheet_mode` | enum | `linear` | `linear` (age-based) or `random` (random start frame) |

**Billboard alignment modes:**

```
camera_facing         velocity_aligned       fixed_axis (Y-up)

     +--+                  /                    +--+
     |  |  <-- always     / / <-- stretched     |  |  <-- rotates
     |  |     faces       / /     along          |  |     around Y
     +--+     camera     /       velocity        +--+     axis only
```

### Module: MeshRenderer

Renders a 3D mesh at each particle position.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `mesh` | string | "" | `res://` path to mesh asset (.glb, .fbx) |
| `material` | string | "" | `res://` path to .uzmat (uses PARTICLE variant) |
| `scale_mode` | enum | `uniform` | `uniform` (size -> scale) or `per_axis` |
| `alignment` | enum | `velocity` | `velocity`, `world`, `random` |
| `lod_distance_0` | float | 10.0 | Distance to switch to LOD1 |
| `lod_distance_1` | float | 30.0 | Distance to switch to LOD2 |
| `cast_shadow` | bool | false | Mesh particles cast shadows |

Implementation: uses GPU instancing via `StructuredBuffer<float4x4>` for transform matrices, one `DrawIndexedInstanced` call per LOD level.

### Module: RibbonRenderer

Connects consecutive particles in emission order with a triangle strip, creating trails.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `texture` | string | "" | `res://` path to trail texture |
| `texture_mode` | enum | `stretch` | `stretch` (along full trail) or `tile` (repeat per segment) |
| `tile_distance` | float | 1.0 | Texture repeat distance for `tile` mode (meters) |
| `width` | float | 0.1 | Trail width (meters) |
| `width_curve` | CurveFloat | 1.0->0.0 | Width multiplier over particle lifetime |
| `face_camera` | bool | true | Trail normals face camera (vs. emission direction) |
| `max_trail_length` | int | 64 | Maximum segments in one trail |
| `min_segment_distance` | float | 0.05 | Minimum distance to add a new trail segment |
| `blend_mode` | enum | `alpha` | Same blend modes as SpriteRenderer |

```
Ribbon Topology:

  v0---v2---v4---v6---v8      <-- Top vertices (position + width/2)
  | \  | \  | \  | \  |
  |  \ |  \ |  \ |  \ |       <-- Triangle strip
  |   \|   \|   \|   \|
  v1---v3---v5---v7---v9      <-- Bottom vertices (position - width/2)

  ^    ^    ^    ^    ^
  p0   p1   p2   p3   p4      <-- Particle positions (emission order)
```

### Module: BeamRenderer

Renders a beam (straight or curved) between two endpoints.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `source` | enum | `emitter` | `emitter` (origin) or `node` (scene node) |
| `target` | enum | `direction` | `direction` (fixed vector) or `node` (scene node) |
| `target_node` | string | "" | Node name if target mode is `node` |
| `width` | float | 0.1 | Beam width (meters) |
| `segments` | int | 16 | Number of beam segments (more = smoother curves) |
| `noise` | float | 0.0 | Per-segment displacement (lightning effect) |
| `noise_speed` | float | 5.0 | How fast noise changes (flicker speed) |
| `texture` | string | "" | `res://` path to beam texture |
| `scroll_speed` | float | 0.0 | Texture scroll speed along beam (m/s) |

### Material Binding

All particle renderers use `.uzmat` materials with the `PARTICLE` shader variant define. The particle system passes per-particle data via `StructuredBuffer<Particle>` bound at `t8` in the root signature.

```ini
; materials/particle_fire.uzmat
[uzmaterial version=1]
shader = "res://shaders/builtin/particle.hlsl"
albedo_texture = "res://textures/vfx/fire_sprite.png"
blend_mode = additive

[variants]
PARTICLE = 1
```

### Sort Modes

| Mode | Description | Performance | Use Case |
|------|-------------|-------------|----------|
| `none` | Draw in buffer order | Fastest | Additive blend (order-independent) |
| `front_to_back` | Sort by distance, near first | O(N log N) GPU bitonic sort | Opaque particles, early-Z optimization |
| `back_to_front` | Sort by distance, far first | O(N log N) GPU bitonic sort | Alpha-blended particles (correct transparency) |
| `by_age` | Sort by remaining lifetime | O(N log N) GPU bitonic sort | Consistent layering for layered effects |

Sorting is performed in a separate compute dispatch (`CS_ParticleSort.hlsl`) using a GPU bitonic merge sort on the `AliveList` indices.

---

## 7. Curve Editor

The Curve Editor is a dedicated panel for defining how particle properties change over their lifetime. It is the primary tool for fine-tuning the look and feel of effects.

### Curve Types

| Curve Type | Data | Description |
|------------|------|-------------|
| `CurveFloat` | float value at N keyframes | Size, opacity, speed, width |
| `CurveFloat2` | float2 value at N keyframes | Non-uniform size (width, height) |
| `CurveRGBA` | float4 color at N keyframes | Color gradient over lifetime |
| `CurveAngle` | float degrees at N keyframes | Rotation speed over lifetime |

### Curve Editor Mockup

```
+------------------------------------------------------------------+
|  Curve: ColorOverLifetime (Flames)                [Preset v]     |
+------------------------------------------------------------------+
|  Channel: [R] [G] [B] [A]  [All]        Interpolation: [Smooth] |
+------------------------------------------------------------------+
|                                                                  |
|  1.0 +                                                           |
|      |  R*                                                       |
|      |  R  *                                                     |
|  0.8 +  R    *                                                   |
|      |  R      *       G*                                        |
|      |  R        *   G    *                                      |
|  0.6 +  R          *G       *                                    |
|      |              *  G      *                                  |
|      |            G  R    *     *                                |
|  0.4 +          G      R    *     *                              |
|      |        G          R     *    *                            |
|      |      G              R     *    *                          |
|  0.2 +    G                  R     *    *                        |
|      |  G                      R     B    *                      |
|      |G            A-----------A-R-----B----*---A                |
|  0.0 +----A---------------------------------B-----*--A-----     |
|      |                                              B            |
|      +---+---+---+---+---+---+---+---+---+---+---+---+---+-     |
|      0.0     0.1     0.2     0.3     0.4     0.5    ...   1.0   |
|                        Particle Age (normalized)                 |
+------------------------------------------------------------------+
|  Selected Key: age=0.30  R=1.0  G=0.3  B=0.0  A=0.9             |
|  Tangent: [Auto] [Linear] [Step] [Free]                         |
+------------------------------------------------------------------+
```

### Keyframe Properties

| Property | Type | Description |
|----------|------|-------------|
| `time` | float | Normalized particle age (0.0 = birth, 1.0 = death) |
| `value` | float or float4 | Value at this keyframe |
| `interpolation` | enum | `linear`, `smooth` (Catmull-Rom), `step` (instant), `bezier` |
| `tangent_in` | float | Incoming tangent handle (for `bezier` mode) |
| `tangent_out` | float | Outgoing tangent handle (for `bezier` mode) |

### Distribution Types

Instead of a fixed curve, parameters can use statistical distributions:

| Distribution | Description | Diagram |
|-------------|-------------|---------|
| **Constant** | Single fixed value | `------` (flat line) |
| **Curve** | Single curve over lifetime | `--/\--` (one line) |
| **Random Between Two Constants** | Uniform random in [min, max] | `===` (shaded band) |
| **Random Between Two Curves** | Random interpolation between two curves | Shaded area between curves |
| **Random From Gradient** | Random pick from a color gradient | Gradient bar |

```
Random Between Two Curves:

  1.0 |
      |     /\  upper curve
      |    /##\
  0.5 |   /####\       <-- particles get random values in shaded region
      |  /##/\##\
      | /##/  \##\
  0.0 |/--/    \--\    lower curve
      +-----------+
      0.0        1.0  age
```

### Curve Presets Library

Built-in presets for common patterns:

| Preset | Shape | Use Case |
|--------|-------|----------|
| `linear_fade` | 1.0 -> 0.0 linear | Generic fade-out |
| `ease_in` | slow start, fast finish | Acceleration |
| `ease_out` | fast start, slow finish | Deceleration |
| `ease_in_out` | slow-fast-slow | Smooth transition |
| `bell` | 0 -> 1 -> 0 bell curve | Pulsing effects |
| `fire_color` | yellow -> orange -> red -> black | Fire color ramp |
| `smoke_color` | white -> grey -> transparent | Smoke color ramp |
| `spark_size` | 1 -> 0 exponential decay | Sparks shrinking quickly |
| `pop_in` | 0 -> 1.2 -> 1.0 overshoot | Bouncy scale-in |
| `flutter` | sine wave, 3 cycles | Flickering/pulsing |

---

## 8. Effect Composition

A single `.uzfx` file can contain multiple emitters that together form a complex visual effect. The editor provides tools for orchestrating emitter timing and LOD behavior.

### Multi-Emitter Orchestration

```
Time ->  0s        0.5s       1.0s       1.5s       2.0s
         |          |          |          |          |

Flash:   |||                                              (burst, 0.1s)
         ^^^
Fireball:         |||||||||||||||||||||||||                (0.2s delay, 1.5s)
                  ^^^^^^^^^^^^^^^^^^^^^^^^
Smoke:                    |||||||||||||||||||||||||||      (0.5s delay, 2.0s, looping)
                          ^^^^^^^^^^^^^^^^^^^^^^^^^
Sparks:           ||| ||| ||| ||| |||                     (0.2s delay, burst pulses)
                  ^^^ ^^^ ^^^ ^^^ ^^^
Debris:           ||||||||                                (0.2s delay, 0.5s burst)
                  ^^^^^^^^
```

### Emitter Timing Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `delay` | float | 0.0 | Seconds after effect starts before this emitter activates |
| `duration` | float | 0.0 | Emission duration (0 = matches effect duration) |
| `looping` | bool | true | Whether this emitter loops independently |
| `pre_warm` | float | 0.0 | Seconds of simulation to run at spawn (instant fill) |

### LOD Levels

Each effect defines up to 3 LOD levels based on camera distance:

| LOD | Distance | Behavior |
|-----|----------|----------|
| **LOD0** | 0 - 20m | Full quality: all emitters, full particle count |
| **LOD1** | 20 - 50m | Reduced: 50% emission rate, disable secondary emitters |
| **LOD2** | 50 - 100m | Minimal: 25% emission rate, disable all sub-emitters, simplified modules |
| **Culled** | > 100m | Simulation paused, invisible (with 80m hysteresis re-enable) |

LOD configuration in `.uzfx`:

```ini
[lod]
lod1_distance    = 20.0
lod1_rate_scale  = 0.5
lod1_disable     = ["Sparks", "Debris"]    ; emitter names to disable at LOD1

lod2_distance    = 50.0
lod2_rate_scale  = 0.25
lod2_disable     = ["Sparks", "Debris", "Smoke"]

cull_distance    = 100.0
hysteresis       = 20.0    ; re-enable at 80m (cull_distance - hysteresis)
```

### Effect-Level Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `name` | string | "" | Effect display name |
| `duration` | float | 0.0 | Total effect duration (0 = infinite / driven by emitters) |
| `looping` | bool | true | Loop entire effect |
| `warmup_time` | float | 0.0 | Pre-simulate this many seconds at spawn |
| `bounding_radius` | float | 5.0 | Radius for frustum culling and LOD distance |

---

## 9. Preview & Simulation Controls

The preview toolbar sits below the 3D viewport and provides full playback control.

### Toolbar Mockup

```
+----------------------------------------------------------------------+
| [|<]  [<]  [>>]  [>|]  [O]  |  Speed: [0.1x] [0.25x] [0.5x] [1x] [2x]  |  t: 1.342s  |
+----------------------------------------------------------------------+
  ^      ^    ^      ^     ^
  |      |    |      |     |
  |      |    |      |     Loop toggle (O = loop, - = one-shot)
  |      |    |      Step forward one frame
  |      |    Play / Pause toggle
  |      Step backward one frame (rewind + replay to t-1)
  Restart from t=0
```

### Playback Controls

| Button | Shortcut | Action |
|--------|----------|--------|
| Restart | `Home` | Reset simulation to t=0, respawn all particles |
| Step Back | `[` | Rewind 1 frame (restart + replay to t-1 frame) |
| Play/Pause | `Space` | Toggle simulation running/frozen |
| Step Forward | `]` | Advance exactly 1 frame (at current speed) |
| Loop Toggle | `L` | Toggle between infinite loop and one-shot playback |

### Simulation Speed

| Speed | Shortcut | Use Case |
|-------|----------|----------|
| 0.1x | `1` | Extreme slow motion for timing precision |
| 0.25x | `2` | Slow motion for detail work |
| 0.5x | `3` | Half speed for animation review |
| 1.0x | `4` | Real-time (default) |
| 2.0x | `5` | Fast forward to check looping transitions |

### Freeze at Time

Right-click the timeline scrubber to freeze at a specific time. The simulation runs from t=0 to the freeze point and holds. Useful for inspecting a specific moment in the effect.

### Environment Toggles

| Toggle | Shortcut | Description |
|--------|----------|-------------|
| Ground Plane | `G` | Toggle collision ground plane (Y=0) |
| Grid | `Shift+G` | Toggle reference grid overlay |
| Wind Preview | `W` | Toggle wind vector indicator and wind force |
| Bounds | `B` | Show emitter bounding sphere |
| Velocity Arrows | `V` | Draw velocity vectors on particles (debug) |
| Wireframe | `F3` | Show particle quads as wireframe |

### Wind Preview

When wind preview is active, a visual indicator and adjustable widget appear:

```
          Wind: 2.0 m/s
            ===>
           /    \
         /   *   \      <-- wind affects particles in real-time
        * *   * *  *
       *    *    *
```

The wind direction and strength can be adjusted by dragging the wind arrow in the viewport.

---

## 10. File Format (.uzfx)

### Format Specification

`.uzfx` is an INI-like plain-text format consistent with UzEngine's other file formats (`.uzscene`, `.uzmat`). It is designed to be human-readable, AI-editable, and version-control-friendly.

### Grammar

```
file        = header emitter+
header      = "[uzfx" kvpairs "]" newline
emitter     = emitter_header property* module*
emitter_header = "[emitter" kvpairs "]" newline
module      = module_header property*
module_header = "[module." module_name "]" newline
kvpairs     = (key "=" value)*
property    = key whitespace* "=" whitespace* value newline
key         = [a-z_][a-z0-9_]*
value       = string | number | bool | array | curve
string      = '"' [^"]* '"'
number      = float | int
bool        = "true" | "false"
array       = "[" value ("," value)* "]"
curve       = "curve(" keyframe (";" keyframe)* ")"
keyframe    = time ":" value [ "/" tangent_in "/" tangent_out ]
comment     = ";" [^\n]* newline
```

### Curve Serialization

Curves are serialized inline using a compact notation:

```ini
; Float curve: time:value pairs separated by semicolons
size_curve = curve(0.0:0.0; 0.1:1.0; 0.8:1.0; 1.0:0.0)

; Float curve with tangent handles (bezier): time:value/tangent_in/tangent_out
size_curve = curve(0.0:0.0/0.0/0.5; 0.5:1.0/0.3/0.3; 1.0:0.0/0.5/0.0)

; RGBA color curve: time:[R,G,B,A]
color_curve = curve(0.0:[1.0,0.6,0.1,1.0]; 0.3:[1.0,0.3,0.0,0.9]; 0.7:[0.5,0.1,0.0,0.4]; 1.0:[0.0,0.0,0.0,0.0])

; Random between two curves
size_range = range(curve(0.0:0.5; 1.0:0.0), curve(0.0:1.0; 1.0:0.2))
```

### Full Example: Campfire Effect (3 Emitters)

```ini
[uzfx version=1 name="Campfire"]
; A campfire effect with flames, embers, and smoke.
; Duration: infinite (looping)
; Bounding radius: 3m

duration        = 0.0
looping         = true
bounding_radius = 3.0

[lod]
lod1_distance   = 20.0
lod1_rate_scale = 0.5
lod1_disable    = ["Embers"]
lod2_distance   = 50.0
lod2_rate_scale = 0.25
lod2_disable    = ["Embers", "Smoke"]
cull_distance   = 100.0
hysteresis      = 20.0

; ============================================================
; EMITTER 1: Flames
; Core fire — upward-moving, additive-blended sprites
; ============================================================

[emitter name="Flames" type=sprite enabled=true]
delay               = 0.0
max_particles       = 512
simulation_space    = local

[module.spawn_rate]
rate                = 40
burst_count         = 0

[module.spawn_shape]
shape               = cone
cone_angle          = 15.0
cone_radius         = 0.1

[module.initial_velocity]
mode                = random_cone
direction           = [0.0, 1.0, 0.0]
speed_min           = 0.8
speed_max           = 1.5
cone_angle          = 10.0

[module.initial_lifetime]
min                 = 1.2
max                 = 2.0

[module.initial_size]
min                 = 0.05
max                 = 0.12

[module.initial_rotation]
min                 = 0.0
max                 = 360.0

[module.gravity]
scale               = -0.1

[module.drag]
coefficient         = 0.4

[module.noise_velocity]
strength            = 0.3
frequency           = 2.0
scroll_speed        = 0.5
octaves             = 2
mode                = curl

[module.color_over_lifetime]
gradient            = curve(0.0:[1.0,0.6,0.1,1.0]; 0.3:[1.0,0.3,0.0,0.9]; 0.7:[0.5,0.1,0.0,0.4]; 1.0:[0.0,0.0,0.0,0.0])

[module.size_over_lifetime]
curve               = curve(0.0:0.0; 0.1:1.0; 0.8:1.0; 1.0:0.0)

[module.sprite_renderer]
texture             = "res://textures/vfx/fire_sprite.png"
alignment           = camera_facing
blend_mode          = additive
sort_mode           = none
soft_particles      = true
soft_distance       = 0.3
sheet_columns       = 4
sheet_rows          = 4
sheet_fps           = 30.0

; ============================================================
; EMITTER 2: Embers
; Small bright particles drifting upward, with collision bounce
; ============================================================

[emitter name="Embers" type=sprite enabled=true]
delay               = 0.5
max_particles       = 256
simulation_space    = world

[module.spawn_rate]
rate                = 15
burst_count         = 0

[module.spawn_shape]
shape               = sphere
sphere_radius       = 0.15

[module.initial_velocity]
mode                = random_cone
direction           = [0.0, 1.0, 0.0]
speed_min           = 1.5
speed_max           = 3.0
cone_angle          = 25.0

[module.initial_lifetime]
min                 = 2.0
max                 = 4.0

[module.initial_size]
min                 = 0.01
max                 = 0.03

[module.initial_color]
mode                = random_range
color_min           = [1.0, 0.4, 0.0, 1.0]
color_max           = [1.0, 0.8, 0.2, 1.0]

[module.gravity]
scale               = -0.05

[module.drag]
coefficient         = 0.2

[module.noise_velocity]
strength            = 0.8
frequency           = 1.5
scroll_speed        = 0.2
octaves             = 1
mode                = curl

[module.color_over_lifetime]
gradient            = curve(0.0:[1.0,0.6,0.1,1.0]; 0.5:[1.0,0.3,0.0,0.8]; 1.0:[0.2,0.05,0.0,0.0])

[module.size_over_lifetime]
curve               = curve(0.0:1.0; 0.7:0.8; 1.0:0.0)

[module.sprite_renderer]
texture             = "res://textures/vfx/ember.png"
alignment           = camera_facing
blend_mode          = additive
sort_mode           = none

; ============================================================
; EMITTER 3: Smoke
; Slow-rising, alpha-blended smoke that billows and fades
; ============================================================

[emitter name="Smoke" type=sprite enabled=true]
delay               = 1.0
max_particles       = 128
simulation_space    = world

[module.spawn_rate]
rate                = 8

[module.spawn_shape]
shape               = sphere
sphere_radius       = 0.2

[module.initial_velocity]
mode                = random_cone
direction           = [0.0, 1.0, 0.0]
speed_min           = 0.3
speed_max           = 0.6
cone_angle          = 20.0

[module.initial_lifetime]
min                 = 3.0
max                 = 5.0

[module.initial_size]
min                 = 0.15
max                 = 0.25

[module.initial_rotation]
min                 = 0.0
max                 = 360.0

[module.gravity]
scale               = -0.03

[module.drag]
coefficient         = 0.6

[module.rotation_over_lifetime]
speed_min           = -15.0
speed_max           = 15.0

[module.noise_velocity]
strength            = 0.15
frequency           = 0.5
scroll_speed        = 0.1
octaves             = 2
mode                = simplex

[module.color_over_lifetime]
gradient            = curve(0.0:[0.3,0.3,0.3,0.0]; 0.1:[0.3,0.3,0.3,0.6]; 0.5:[0.2,0.2,0.2,0.4]; 1.0:[0.15,0.15,0.15,0.0])

[module.size_over_lifetime]
curve               = curve(0.0:0.5; 0.3:1.0; 1.0:2.0)

[module.sprite_renderer]
texture             = "res://textures/vfx/smoke.png"
alignment           = camera_facing
blend_mode          = alpha
sort_mode           = back_to_front
soft_particles      = true
soft_distance       = 0.5
```

---

## 11. Lua API

The Lua API allows game scripts to spawn, control, and query particle effects at runtime. All functions are accessible through the `ctx` object passed to script callbacks.

### Spawning Effects

```lua
-- Spawn a one-shot VFX at a world position
-- Returns an FXHandle for later control
local fx = ctx:spawn_fx("res://fx/explosion.uzfx", vec3(10, 0, -5))

-- Spawn with full options
local fx = ctx:spawn_fx("res://fx/explosion.uzfx", vec3(10, 0, -5), {
    rotation  = quat.from_euler(0, 45, 0),  -- rotation offset
    scale     = 2.0,                        -- uniform scale
    speed     = 1.5,                        -- playback speed multiplier
    looping   = false,                      -- override looping behavior
    seed      = 42,                         -- deterministic random seed
})

-- Spawn attached to a scene node (follows its transform)
local fx = ctx:spawn_fx_attached(player_entity, "socket_muzzle", "res://fx/muzzle_flash.uzfx")

-- Spawn attached with options
local fx = ctx:spawn_fx_attached(torch_entity, "socket_flame", "res://fx/campfire.uzfx", {
    offset   = vec3(0, 0.1, 0),            -- local offset from attach point
    rotation = quat.identity(),
    scale    = 0.5,
})
```

### Controlling Effects

```lua
-- Graceful stop: stops spawning, existing particles live out their lifetime
fx:stop()

-- Immediate kill: all particles destroyed instantly
fx:stop_immediate()

-- Set a runtime parameter on the effect
-- Supported parameters: rate_scale, speed, color_tint, size_scale
fx:set_param("rate_scale", 0.5)      -- halve emission rate
fx:set_param("speed", 2.0)           -- double playback speed
fx:set_param("color_tint", vec4(1, 0, 0, 1))  -- tint all particles red
fx:set_param("size_scale", 1.5)      -- 50% larger particles

-- Enable/disable specific emitters within the effect
fx:set_emitter_enabled("Smoke", false)   -- disable the Smoke emitter
fx:set_emitter_enabled("Flames", true)   -- re-enable the Flames emitter

-- Change the emission rate of a specific emitter
fx:set_emitter_rate("Flames", 80)   -- double the flame emission
```

### Querying State

```lua
-- Check if the effect is still alive (not finished and not stopped)
if fx:is_alive() then
    -- ...
end

-- Get the current total particle count across all emitters
local count = fx:get_particle_count()

-- Get the current playback time
local time = fx:get_time()

-- Get per-emitter info
local info = fx:get_emitter_info("Flames")
-- info.particle_count  (int)
-- info.alive           (bool)
-- info.rate            (float, current emission rate)
```

### Event Integration

```lua
-- Spawn VFX on event bus signal
ctx.events:on("enemy_death", function(data)
    ctx:spawn_fx("res://fx/death_explosion.uzfx", data.position, {
        scale = data.enemy_size * 0.5,
    })
end)

-- Spawn VFX on animation notify
function on_anim_event(event_name, params)
    if event_name == "footstep" then
        local foot_pos = ctx.scene:get_bone_world_pos(self.id, params.bone)
        local surface = ctx.physics:raycast_down(foot_pos, 0.5)
        local fx_path = surface.material == "dirt"
            and "res://fx/footstep_dirt.uzfx"
            or  "res://fx/footstep_stone.uzfx"
        ctx:spawn_fx(fx_path, foot_pos)
    end
end

-- Spawn VFX on collision event
ctx.events:on("collision", function(data)
    if data.impulse > 5.0 then
        ctx:spawn_fx("res://fx/impact_sparks.uzfx", data.point, {
            rotation = quat.look_at(data.normal),
            scale    = math.min(data.impulse * 0.1, 3.0),
        })
    end
end)
```

### Convenience Functions

```lua
-- Stop all active effects in the scene
ctx.vfx:stop_all()

-- Stop all effects with immediate kill
ctx.vfx:stop_all_immediate()

-- Get global particle count (all emitters, all effects)
local total = ctx.vfx:get_total_particle_count()

-- Set global wind (affects all emitters with Wind module)
ctx.vfx:set_global_wind(vec3(2, 0, 0))

-- Preload an effect (warm up GPU buffers without spawning)
ctx.vfx:preload("res://fx/big_explosion.uzfx")
```

---

## 12. Implementation Notes

### C++ Architecture

```
src/engine/vfx/
    particle_editor.h/cpp          -- Editor tool orchestration
    emitter_stack.h/cpp            -- Emitter list management, serialization
    module_registry.h/cpp          -- Registry of all spawn/update/render modules
    module_base.h                  -- Module interface (ISpawnModule, IUpdateModule, IRenderModule)
    curve_evaluator.h/cpp          -- CPU-side curve sampling for editor preview / CPU fallback
    effect_asset.h/cpp             -- .uzfx parser and in-memory representation
    lod_controller.h/cpp           -- Distance-based LOD transitions

src/engine/vfx/modules/
    spawn_rate.h/cpp               -- SpawnRate module
    spawn_shape.h/cpp              -- SpawnShape module (point, box, sphere, cone, mesh)
    initial_velocity.h/cpp         -- InitialVelocity module
    initial_lifetime.h/cpp         -- InitialLifetime module
    initial_size.h/cpp             -- InitialSize module
    initial_color.h/cpp            -- InitialColor module
    initial_rotation.h/cpp         -- InitialRotation module
    gravity.h/cpp                  -- Gravity module
    wind.h/cpp                     -- Wind module
    drag.h/cpp                     -- Drag module
    point_attractor.h/cpp          -- PointAttractor module
    vortex.h/cpp                   -- Vortex module
    orbit.h/cpp                    -- Orbit module
    collision.h/cpp                -- Collision module
    color_over_lifetime.h/cpp      -- ColorOverLifetime module
    size_over_lifetime.h/cpp       -- SizeOverLifetime module
    rotation_over_lifetime.h/cpp   -- RotationOverLifetime module
    noise_velocity.h/cpp           -- NoiseVelocity / CurlNoise module
    vector_field.h/cpp             -- VectorFieldSampling module
    sub_emitter.h/cpp              -- SubEmitter module
    sprite_renderer.h/cpp          -- SpriteRenderer module
    mesh_renderer.h/cpp            -- MeshRenderer module
    ribbon_renderer.h/cpp          -- RibbonRenderer module
    beam_renderer.h/cpp            -- BeamRenderer module

src/game/systems/
    vfx_system.h/cpp               -- VFXSystem (owns EmitterPool, drives GPU sim+render)
```

### GPU Particle Data Struct

```hlsl
// shaders/vfx/particle_data.hlsli
// Shared between CS_ParticleSpawn, CS_ParticleUpdate, VS_Particle, PS_Particle

struct Particle {
    float3 position;            // World-space position
    float  lifetime_remain;     // Seconds remaining (< 0 = dead)

    float3 velocity;            // Current velocity (m/s)
    float  lifetime_total;      // Total lifetime for age normalization

    float4 color;               // Current RGBA (linear, pre-multiplied by module stack)

    float2 size;                // Current size (width, height) in meters
    float  rotation;            // Billboard rotation (radians)
    uint   seed;                // Per-particle random seed (for noise, sub-emitter ID)

    float3 initial_position;    // Birth position (for ribbon/orbit calculations)
    float  age;                 // Normalized age [0..1] = 1 - (remain / total)

    float3 prev_position;       // Previous frame position (for velocity-aligned + motion blur)
    uint   emitter_id;          // Which emitter spawned this particle

    // 96 bytes total (aligned to 16-byte boundary)
    // One emitter with 65,536 particles = 6 MB GPU buffer
};
```

### GPU Buffer Layout

```
Per-Emitter GPU Resources:
+-------------------------------------------------+
| ParticleBuffer  | RWStructuredBuffer<Particle>   |  65,536 entries, 6 MB
| DeadList        | AppendStructuredBuffer<uint>   |  65,536 entries, 256 KB
| AliveList       | AppendStructuredBuffer<uint>   |  65,536 entries, 256 KB
| AliveListSorted | RWStructuredBuffer<uint>       |  65,536 entries, 256 KB (sort output)
| IndirectArgs    | RWByteAddressBuffer            |  20 bytes (DrawIndexedInstancedIndirect args)
| EmitterCB       | ConstantBuffer<EmitterParams>  |  256 bytes (all module parameters)
+-------------------------------------------------+
Total per emitter: ~6.75 MB
Total pool (256 emitters max): ~1.7 GB (but only allocated on demand)
```

### HLSL Compute Shader: Spawn

```hlsl
// shaders/vfx/CS_ParticleSpawn.hlsl
// Dispatched once per frame per active emitter.
// Thread group: [64, 1, 1]
// Dispatch: ceil(spawn_count / 64) groups

#include "particle_data.hlsli"
#include "noise.hlsli"

cbuffer EmitterParams : register(b0) {
    float4x4 g_EmitterTransform;        // Emitter world transform
    float    g_DeltaTime;
    float    g_TotalTime;
    uint     g_SpawnCount;              // Particles to spawn this frame
    uint     g_MaxParticles;
    float    g_LifetimeMin;
    float    g_LifetimeMax;
    float2   g_SizeMin;
    float2   g_SizeMax;
    float4   g_ColorMin;
    float4   g_ColorMax;
    float3   g_VelocityDir;
    float    g_SpeedMin;
    float    g_SpeedMax;
    float    g_ConeAngle;              // Radians
    float3   g_ShapeParams;            // Shape-specific (radius, half_extents, etc.)
    uint     g_ShapeType;              // 0=point, 1=sphere, 2=cone, 3=box, 4=mesh
    uint     g_EmitterSeed;
    uint     g_FrameIndex;
    float    g_RotationMin;
    float    g_RotationMax;
    float2   _pad0;
};

RWStructuredBuffer<Particle>          g_Particles     : register(u0);
ConsumeStructuredBuffer<uint>         g_DeadList      : register(u1);
RWByteAddressBuffer                   g_IndirectArgs  : register(u2);

// PCG hash for per-particle random
uint pcg_hash(uint input) {
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand_float(inout uint seed) {
    seed = pcg_hash(seed);
    return float(seed) / 4294967295.0;
}

float3 random_in_cone(float half_angle, float3 dir, inout uint seed) {
    float cos_angle = cos(half_angle);
    float z = lerp(cos_angle, 1.0, rand_float(seed));
    float phi = rand_float(seed) * 6.28318530718;
    float sin_theta = sqrt(1.0 - z * z);
    float3 local = float3(sin_theta * cos(phi), sin_theta * sin(phi), z);

    // Build rotation from +Z to dir
    float3 up = abs(dir.y) < 0.999 ? float3(0,1,0) : float3(1,0,0);
    float3 right = normalize(cross(up, dir));
    up = cross(dir, right);

    return right * local.x + up * local.y + dir * local.z;
}

float3 random_in_sphere(float radius, inout uint seed) {
    float u = rand_float(seed);
    float v = rand_float(seed);
    float theta = 2.0 * 3.14159265 * u;
    float phi = acos(2.0 * v - 1.0);
    float r = radius * pow(rand_float(seed), 1.0 / 3.0);
    return float3(
        r * sin(phi) * cos(theta),
        r * sin(phi) * sin(theta),
        r * cos(phi)
    );
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= g_SpawnCount) return;

    // Consume a free slot from the dead list
    uint idx = g_DeadList.Consume();

    // Per-particle random seed
    uint seed = pcg_hash(g_EmitterSeed ^ (g_FrameIndex * 65537u + dtid.x));

    Particle p = (Particle)0;

    // --- Position (shape-dependent) ---
    float3 local_pos = float3(0, 0, 0);
    if (g_ShapeType == 0) {
        // Point
        local_pos = float3(0, 0, 0);
    } else if (g_ShapeType == 1) {
        // Sphere
        local_pos = random_in_sphere(g_ShapeParams.x, seed);
    } else if (g_ShapeType == 2) {
        // Cone
        float t = rand_float(seed);
        float angle = g_ConeAngle * sqrt(rand_float(seed));
        float phi = rand_float(seed) * 6.28318530718;
        float r = g_ShapeParams.x + t * tan(angle);
        local_pos = float3(r * cos(phi), t * g_ShapeParams.y, r * sin(phi));
    } else if (g_ShapeType == 3) {
        // Box
        local_pos = float3(
            (rand_float(seed) * 2.0 - 1.0) * g_ShapeParams.x,
            (rand_float(seed) * 2.0 - 1.0) * g_ShapeParams.y,
            (rand_float(seed) * 2.0 - 1.0) * g_ShapeParams.z
        );
    }

    p.position = mul(g_EmitterTransform, float4(local_pos, 1.0)).xyz;
    p.initial_position = p.position;
    p.prev_position = p.position;

    // --- Velocity ---
    float speed = lerp(g_SpeedMin, g_SpeedMax, rand_float(seed));
    float3 dir = random_in_cone(g_ConeAngle, g_VelocityDir, seed);
    dir = mul((float3x3)g_EmitterTransform, dir);
    p.velocity = dir * speed;

    // --- Lifetime ---
    p.lifetime_total = lerp(g_LifetimeMin, g_LifetimeMax, rand_float(seed));
    p.lifetime_remain = p.lifetime_total;
    p.age = 0.0;

    // --- Size ---
    p.size = lerp(g_SizeMin, g_SizeMax, rand_float(seed));

    // --- Color ---
    float color_t = rand_float(seed);
    p.color = lerp(g_ColorMin, g_ColorMax, color_t);

    // --- Rotation ---
    p.rotation = lerp(g_RotationMin, g_RotationMax, rand_float(seed));

    // --- Metadata ---
    p.seed = seed;
    p.emitter_id = g_EmitterSeed; // used as emitter identifier

    g_Particles[idx] = p;
}
```

### HLSL Compute Shader: Update (Simulate)

```hlsl
// shaders/vfx/CS_ParticleUpdate.hlsl
// Dispatched once per frame per active emitter.
// Thread group: [64, 1, 1]
// Dispatch: ceil(max_particles / 64) groups

#include "particle_data.hlsli"
#include "noise.hlsli"
#include "curve_lut.hlsli"

cbuffer SimParams : register(b0) {
    float    g_DeltaTime;
    float    g_TotalTime;
    float3   g_Gravity;                 // World gravity (typically [0, -9.81, 0])
    float    g_GravityScale;
    float    g_Drag;
    float3   g_WindDirection;
    float    g_WindStrength;
    float    g_WindTurbulence;

    // Noise module params
    float    g_NoiseStrength;
    float    g_NoiseFrequency;
    float    g_NoiseScrollSpeed;
    uint     g_NoiseOctaves;
    uint     g_NoiseMode;              // 0=curl, 1=simplex

    // Attractor params
    float3   g_AttractorPos;
    float    g_AttractorStrength;
    float    g_AttractorRadius;

    // Collision params
    uint     g_CollisionEnabled;
    float3   g_CollisionPlaneNormal;
    float    g_CollisionPlaneOffset;
    float    g_CollisionBounce;
    float    g_CollisionFriction;

    uint     g_MaxParticles;
    float    _pad;
};

RWStructuredBuffer<Particle>          g_Particles     : register(u0);
AppendStructuredBuffer<uint>          g_DeadList      : register(u1);
AppendStructuredBuffer<uint>          g_AliveList     : register(u2);
RWByteAddressBuffer                   g_IndirectArgs  : register(u3);

// Curve LUTs (256-entry 1D textures uploaded from CPU)
Texture1D<float4>  g_ColorCurveLUT   : register(t0);   // RGBA over lifetime
Texture1D<float>   g_SizeCurveLUT    : register(t1);   // Size multiplier over lifetime
SamplerState       g_LinearSampler   : register(s0);

// --- Noise functions (defined in noise.hlsli) ---
// float3 curl_noise_3d(float3 pos, float freq, float time);
// float3 simplex_noise_3d(float3 pos, float freq, float time);

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    uint idx = dtid.x;
    if (idx >= g_MaxParticles) return;

    Particle p = g_Particles[idx];

    // Skip dead particles
    if (p.lifetime_remain <= 0.0) return;

    // Store previous position (for velocity-aligned rendering, motion blur)
    p.prev_position = p.position;

    // --- Age ---
    p.lifetime_remain -= g_DeltaTime;
    if (p.lifetime_remain <= 0.0) {
        p.lifetime_remain = -1.0;
        g_DeadList.Append(idx);
        g_Particles[idx] = p;
        return;
    }
    p.age = 1.0 - (p.lifetime_remain / p.lifetime_total);

    // --- Forces ---

    // Gravity
    p.velocity += g_Gravity * g_GravityScale * g_DeltaTime;

    // Wind
    if (g_WindStrength > 0.0) {
        float3 wind = g_WindDirection * g_WindStrength;
        if (g_WindTurbulence > 0.0) {
            float3 turb = simplex_noise_3d(p.position * 0.5, 1.0, g_TotalTime) * g_WindTurbulence;
            wind += turb;
        }
        p.velocity += wind * g_DeltaTime;
    }

    // Point attractor
    if (g_AttractorStrength != 0.0) {
        float3 to_attractor = g_AttractorPos - p.position;
        float dist = length(to_attractor);
        if (dist < g_AttractorRadius && dist > 0.001) {
            float falloff = 1.0 / (dist * dist + 0.01);  // inverse square
            p.velocity += normalize(to_attractor) * g_AttractorStrength * falloff * g_DeltaTime;
        }
    }

    // Noise / turbulence
    if (g_NoiseStrength > 0.0) {
        float3 noise_vel;
        float noise_time = g_TotalTime * g_NoiseScrollSpeed;
        if (g_NoiseMode == 0) {
            noise_vel = curl_noise_3d(p.position, g_NoiseFrequency, noise_time);
        } else {
            noise_vel = simplex_noise_3d(p.position, g_NoiseFrequency, noise_time);
        }
        // Multi-octave
        float amplitude = g_NoiseStrength;
        float freq = g_NoiseFrequency;
        float3 total_noise = noise_vel * amplitude;
        for (uint oct = 1; oct < g_NoiseOctaves; oct++) {
            freq *= 2.0;
            amplitude *= 0.5;
            if (g_NoiseMode == 0) {
                total_noise += curl_noise_3d(p.position, freq, noise_time) * amplitude;
            } else {
                total_noise += simplex_noise_3d(p.position, freq, noise_time) * amplitude;
            }
        }
        p.velocity += total_noise * g_DeltaTime;
    }

    // Drag
    p.velocity *= (1.0 - g_Drag * g_DeltaTime);

    // --- Integration ---
    p.position += p.velocity * g_DeltaTime;

    // --- Collision ---
    if (g_CollisionEnabled) {
        float dist_to_plane = dot(p.position, g_CollisionPlaneNormal) - g_CollisionPlaneOffset;
        if (dist_to_plane < 0.0) {
            // Reflect position
            p.position -= g_CollisionPlaneNormal * dist_to_plane * 2.0;
            // Reflect velocity
            float vn = dot(p.velocity, g_CollisionPlaneNormal);
            p.velocity -= g_CollisionPlaneNormal * vn * (1.0 + g_CollisionBounce);
            // Apply friction to tangential component
            float3 tangent_vel = p.velocity - g_CollisionPlaneNormal * dot(p.velocity, g_CollisionPlaneNormal);
            p.velocity -= tangent_vel * g_CollisionFriction;
        }
    }

    // --- Curve-driven properties ---
    // Sample color curve LUT
    p.color = g_ColorCurveLUT.SampleLevel(g_LinearSampler, p.age, 0);

    // Sample size curve LUT (multiplied by initial size stored in seed-derived value)
    float size_mult = g_SizeCurveLUT.SampleLevel(g_LinearSampler, p.age, 0);
    p.size *= size_mult;

    // --- Write back ---
    g_Particles[idx] = p;
    g_AliveList.Append(idx);

    // Atomically increment the indirect draw instance count
    g_IndirectArgs.InterlockedAdd(4, 1);  // InstanceCount at byte offset 4
}
```

### HLSL Vertex/Pixel Shader: Billboard Rendering

```hlsl
// shaders/vfx/VS_Particle.hlsl

#include "particle_data.hlsli"

cbuffer FrameParams : register(b1) {
    float4x4 g_View;
    float4x4 g_Proj;
    float4x4 g_ViewProj;
    float3   g_CameraPos;
    float3   g_CameraRight;
    float3   g_CameraUp;
    float2   g_ScreenSize;
};

StructuredBuffer<Particle>  g_Particles : register(t0);
StructuredBuffer<uint>      g_AliveList : register(t1);

struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
    float  depth    : TEXCOORD1;
};

// Quad corners in [-0.5, 0.5] space
static const float2 g_QuadCorners[4] = {
    float2(-0.5, -0.5),   // bottom-left
    float2( 0.5, -0.5),   // bottom-right
    float2( 0.5,  0.5),   // top-right
    float2(-0.5,  0.5),   // top-left
};

static const float2 g_QuadUVs[4] = {
    float2(0, 1),
    float2(1, 1),
    float2(1, 0),
    float2(0, 0),
};

VSOutput VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    VSOutput output = (VSOutput)0;

    uint corner = vertexID % 4;
    uint alive_idx = g_AliveList[instanceID];
    Particle p = g_Particles[alive_idx];

    // Billboard rotation
    float cos_r = cos(p.rotation);
    float sin_r = sin(p.rotation);
    float2 local = g_QuadCorners[corner];
    float2 rotated = float2(
        local.x * cos_r - local.y * sin_r,
        local.x * sin_r + local.y * cos_r
    );

    // Scale by particle size
    float3 world_offset = g_CameraRight * (rotated.x * p.size.x)
                        + g_CameraUp    * (rotated.y * p.size.y);

    float3 world_pos = p.position + world_offset;

    output.position = mul(g_ViewProj, float4(world_pos, 1.0));
    output.uv = g_QuadUVs[corner];
    output.color = p.color;
    output.depth = output.position.z / output.position.w;

    return output;
}
```

```hlsl
// shaders/vfx/PS_Particle.hlsl

Texture2D    g_ParticleTexture : register(t2);
Texture2D    g_DepthBuffer     : register(t3);  // For soft particles
SamplerState g_Sampler         : register(s0);

cbuffer ParticleRenderParams : register(b2) {
    uint  g_SoftParticles;
    float g_SoftDistance;
    uint  g_SheetColumns;
    uint  g_SheetRows;
    float g_SheetFPS;
    float g_ParticleAge;         // not used here, age is per-particle
    float2 _pad;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
    float  depth    : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_TARGET {
    float2 uv = input.uv;

    // Flipbook animation (if sheet_columns > 1 or sheet_rows > 1)
    if (g_SheetColumns > 1 || g_SheetRows > 1) {
        // Frame index derived from particle age (would need per-particle data)
        // Simplified: use UV subdivision
        float2 frame_size = float2(1.0 / g_SheetColumns, 1.0 / g_SheetRows);
        // Per-particle frame selection would be encoded in Particle.seed
        uv *= frame_size;
    }

    float4 tex_color = g_ParticleTexture.Sample(g_Sampler, uv);
    float4 final_color = tex_color * input.color;

    // Soft particle fade (depth comparison)
    if (g_SoftParticles) {
        float2 screen_uv = input.position.xy / float2(1280.0, 720.0); // TODO: use g_ScreenSize
        float scene_depth = g_DepthBuffer.Sample(g_Sampler, screen_uv).r;
        float particle_depth = input.depth;
        float depth_diff = scene_depth - particle_depth;
        float fade = saturate(depth_diff / g_SoftDistance);
        final_color.a *= fade;
    }

    return final_color;
}
```

### IndirectDraw for GPU-Driven Particle Count

The particle count is never read back to the CPU. Instead, the simulate compute shader atomically builds the indirect args buffer:

```cpp
// C++ side: one-time setup
struct IndirectDrawArgs {
    uint32_t index_count_per_instance = 6;  // 2 triangles per quad
    uint32_t instance_count           = 0;  // filled by CS_ParticleUpdate
    uint32_t start_index_location     = 0;
    int32_t  base_vertex_location     = 0;
    uint32_t start_instance_location  = 0;
};

// Each frame before simulate dispatch:
// 1. Clear instance_count to 0 via CopyBufferRegion from a zero buffer
// 2. Dispatch CS_ParticleUpdate — each alive particle atomics instance_count++
// 3. ExecuteIndirect(DrawIndexedInstanced, indirect_args_buffer)
```

This keeps the entire particle pipeline on the GPU with zero CPU readback.

### VSCode WebView Architecture

The VFX Editor panels are implemented as VSCode WebView panels using the [Webview UI Toolkit](https://github.com/microsoft/vscode-webview-ui-toolkit):

| Panel | Technology | IPC |
|-------|-----------|-----|
| 3D Viewport | Canvas + JPEG stream via WebSocket | Same as main viewport |
| Emitter Stack | HTML + Webview UI Toolkit list | `vfx_editor.getEmitters`, `vfx_editor.addEmitter`, etc. |
| Module Stack | HTML + Webview UI Toolkit list + drag-drop | `vfx_editor.getModules`, `vfx_editor.addModule`, etc. |
| Node Graph | SVG canvas with custom node rendering | `vfx_editor.getGraph`, `vfx_editor.connectNodes`, etc. |
| Curve Editor | HTML5 Canvas 2D with custom drawing | `vfx_editor.getCurve`, `vfx_editor.setCurveKey`, etc. |
| Properties | Webview UI Toolkit form controls | `vfx_editor.getProperty`, `vfx_editor.setProperty` |

### IPC Handlers (vfx_editor.*)

```json
vfx_editor.open         { "path": "res://fx/campfire.uzfx" }
vfx_editor.save         { "path": "res://fx/campfire.uzfx" }
vfx_editor.getEmitters  {}  --> { "emitters": [...] }
vfx_editor.addEmitter   { "type": "sprite", "name": "NewEmitter" }
vfx_editor.removeEmitter { "index": 2 }
vfx_editor.reorderEmitter { "from": 2, "to": 0 }
vfx_editor.getModules   { "emitter": "Flames", "phase": "spawn" }
vfx_editor.addModule    { "emitter": "Flames", "phase": "update", "type": "noise_velocity" }
vfx_editor.removeModule { "emitter": "Flames", "module": "noise_velocity" }
vfx_editor.getProperty  { "emitter": "Flames", "module": "spawn_rate", "key": "rate" }
vfx_editor.setProperty  { "emitter": "Flames", "module": "spawn_rate", "key": "rate", "value": 80 }
vfx_editor.getCurve     { "emitter": "Flames", "module": "color_over_lifetime", "key": "gradient" }
vfx_editor.setCurveKey  { "emitter": "Flames", "module": "color_over_lifetime", "key": "gradient",
                          "index": 1, "time": 0.3, "value": [1.0, 0.3, 0.0, 0.9] }
vfx_editor.setPlayback  { "playing": true, "speed": 1.0, "looping": true }
vfx_editor.restart      {}
vfx_editor.setEnvironment { "ground": true, "wind": [1.0, 0, 0], "background": "res://scenes/preview.uzscene" }
vfx_editor.getStats     {}  --> { "particle_count": 1247, "gpu_time_ms": 0.14, "draw_calls": 3 }
```

### Performance Budget

| Metric | Budget | Notes |
|--------|--------|-------|
| GPU sim time (per emitter) | < 0.2 ms | 64K particles, RTX 3060 baseline |
| GPU render time (per emitter) | < 0.15 ms | Sprite billboards, no sort |
| GPU sort time (per emitter) | < 0.3 ms | Bitonic sort on 64K elements |
| CPU time (editor overhead) | < 0.5 ms | IPC, curve upload, indirect setup |
| Total budget (all VFX) | < 2.0 ms GPU | Across all active emitters in scene |
| VRAM per emitter slot | ~7 MB | Particle buffer + lists + args |
| Max VRAM (all 256 slots) | ~1.7 GB | Only allocated slots consume VRAM |

### Module Registration System

Modules are registered in a central `ModuleRegistry` that maps type names to factory functions. This enables the editor to enumerate available modules and the `.uzfx` parser to instantiate them by name.

```cpp
// src/engine/vfx/module_registry.h

class ModuleRegistry {
public:
    static ModuleRegistry& instance();

    // Register a module factory
    template<typename T>
    void register_module(const char* name, ModulePhase phase);

    // Create module by name
    std::unique_ptr<IModuleBase> create(const char* name) const;

    // Enumerate all registered modules
    struct ModuleInfo {
        const char*  name;
        ModulePhase  phase;      // SPAWN, UPDATE, RENDER
        const char*  description;
        const char*  category;   // "motion", "appearance", "collision", etc.
    };
    Span<const ModuleInfo> get_all() const;
    Span<const ModuleInfo> get_by_phase(ModulePhase phase) const;

private:
    HashMap<StringId, ModuleFactory> m_factories;
    Array<ModuleInfo>                m_info;
};

// Registration macro (used in each module .cpp file)
#define UZ_REGISTER_MODULE(Type, name, phase, desc, category) \
    static bool s_##Type##_registered = [] { \
        ModuleRegistry::instance().register_module<Type>(name, phase); \
        return true; \
    }();
```

### Editor <==> Engine Data Flow

```
+-----------------+       IPC (JSON)        +------------------+
|  VSCode         | <---------------------> |  UzEngine.exe    |
|  VFX Editor     |                         |  --editor        |
|  WebView panels |                         |                  |
+-----------------+                         +------------------+
        |                                           |
        | HTML/Canvas renders                       | Drives
        | module stack, curves,                     | VFXSystem
        | properties                                |
        |                                           |
        v                                           v
  User sees live                            GPU compute sim
  JPEG-streamed                             + billboard render
  3D viewport                               on DX12 pipeline
        ^                                           |
        |          WebSocket (JPEG frames)          |
        +-------------------------------------------+
```

---

## Appendix A: Module Quick Reference

| Phase | Module | Description |
|-------|--------|-------------|
| Spawn | `spawn_rate` | Emission rate, burst, pulse |
| Spawn | `spawn_shape` | Birth position distribution |
| Spawn | `initial_velocity` | Birth velocity direction + speed |
| Spawn | `initial_lifetime` | Random lifetime range |
| Spawn | `initial_size` | Random size range |
| Spawn | `initial_color` | Birth color (constant, random, gradient) |
| Spawn | `initial_rotation` | Random rotation offset |
| Update | `gravity` | World gravity with scale |
| Update | `wind` | Directional wind with turbulence |
| Update | `drag` | Velocity damping |
| Update | `point_attractor` | Attract/repel from a point |
| Update | `vortex` | Rotational force around axis |
| Update | `orbit` | Orbital motion around emitter |
| Update | `collision` | Plane/mesh/depth collision |
| Update | `color_over_lifetime` | Curve-driven color gradient |
| Update | `size_over_lifetime` | Curve-driven size |
| Update | `rotation_over_lifetime` | Angular velocity |
| Update | `noise_velocity` | Simplex/curl noise turbulence |
| Update | `curl_noise` | Divergence-free fluid motion |
| Update | `vector_field` | 3D vector field sampling |
| Update | `sub_emitter` | Child emitter on death/collision |
| Render | `sprite_renderer` | Camera-facing billboard quads |
| Render | `mesh_renderer` | Instanced 3D mesh per particle |
| Render | `ribbon_renderer` | Connected trail strip |
| Render | `beam_renderer` | Line/beam between two points |

## Appendix B: Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Space` | Play / Pause |
| `Home` | Restart from t=0 |
| `[` | Step back 1 frame |
| `]` | Step forward 1 frame |
| `1`-`5` | Set simulation speed (0.1x to 2x) |
| `L` | Toggle loop mode |
| `G` | Toggle ground plane |
| `Shift+G` | Toggle grid |
| `W` | Toggle wind preview |
| `B` | Toggle bounding sphere |
| `V` | Toggle velocity debug arrows |
| `F3` | Toggle wireframe |
| `F` | Frame effect (center camera on effect) |
| `Ctrl+S` | Save .uzfx |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `Delete` | Remove selected module/emitter |
| `Ctrl+D` | Duplicate selected emitter |
