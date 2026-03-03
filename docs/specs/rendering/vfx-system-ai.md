# VFX Graph System — AI-Driven Authoring Pipeline

## Overview

The VFX Graph System is the Phase 4 evolution of the `.uzfx` particle engine. It upgrades
the Phase 3 INI-based emitter format into a full directed-graph authoring pipeline — modeled
conceptually after Unreal's Niagara — with three defining capabilities that set it apart:

1. **AI generation**: a natural-language description produces a complete effect (graph JSON +
   custom HLSL modules + sprite textures) via the `vfx_generate` MCP tool.
2. **Visual node editor**: a VSCode Webview panel renders the emitter graph as interactive
   nodes. Artists edit parameters live; changes hot-reload into the engine viewport in under
   150 ms.
3. **HLSL-extensible simulation**: any module slot in the graph can be replaced with a
   user-authored `CustomHLSL` compute shader that conforms to the `ParticleState` interface,
   enabling arbitrary per-particle simulation logic without modifying engine code.

The `.uzfx` v2 JSON format is a strict superset of v1 INI. The loader detects the format by
inspecting the first non-whitespace character: `[` means INI (v1), `{` means JSON (v2).
All v1 effects continue to load without changes; the runtime auto-converts them to the
internal graph representation.

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  Authoring Layer                                                             │
│                                                                             │
│   VSCode VFX Node Editor (Webview)                                          │
│   ├── Node graph canvas (React + reactflow)                                 │
│   ├── Module palette / parameter Inspector                                  │
│   ├── "Generate with AI" → prompt → vfx_generate MCP call                  │
│   └── "Refine with AI"   → screenshot + description → vfx_refine MCP call  │
│                                                                             │
│   MCP Server (TypeScript)                                                   │
│   ├── vfx_generate(description, style?, color_palette?, reference_image?)   │
│   ├── vfx_refine(effect_path, feedback_description, screenshot_base64)      │
│   └── vfx_list_modules()                                                    │
│         │                                                                   │
│         ▼                                                                   │
│   AI Generation Pipeline                                                    │
│   ├── Step 1 — Claude API  → .uzfx JSON graph                               │
│   ├── Step 2 — Claude API  → custom .hlsl module code (if needed)           │
│   ├── Step 3 — Nano Banana API → sprite PNGs / flipbook sheets              │
│   ├── Step 4 — IPC hot-reload → engine ingests new assets                  │
│   └── Step 5 — screenshot feedback → optional refinement loop              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                     │ IPC (WebSocket / named pipe)
                                     ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  Engine Runtime (C++23 / DX12)                                              │
│                                                                             │
│   VFXGraphSystem                                                            │
│   ├── VFXGraphLoader   — parses .uzfx v2 JSON into EmitterGraph              │
│   ├── ModuleCompiler   — generates permuted compute shaders from modules    │
│   ├── EmitterPool      — 256 GPU emitter slots (unchanged from Phase 3)     │
│   ├── ParticleRenderer — billboard / mesh / ribbon / beam render paths      │
│   └── VFXHotReloader   — FileWatcher → recompile → swap GPU resources       │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Data Flow Summary

```
.uzfx v2 JSON
  → VFXGraphLoader parses emitters[] and modules[]
  → ModuleCompiler concatenates HLSL includes for active modules
  → DXC compiles permuted simulate.hlsl and emit.hlsl CSO blobs
  → EmitterSlot owns: ParticleBuffer, DeadList, AliveList, IndirectArgs
  → Each frame: Simulate dispatch → Emit dispatch → Render (billboard/mesh/ribbon)
```

---

## 2. Enhanced `.uzfx` v2 Graph Format

### 2.1 Format Detection and Backwards Compatibility

The `VFXGraphLoader` inspects the first non-whitespace byte of the file:

- `{` → JSON v2 path: parse `EmitterGraph` from the JSON object.
- `[` → INI v1 path: invoke the Phase 3 INI parser, then convert the resulting `LegacyEffect`
  struct into an equivalent single-emitter `EmitterGraph`. The converted graph is not written
  back to disk; the original `.uzfx` file is left untouched.

No migration tool is required. Artists can incrementally upgrade individual files by
re-saving them through the VFX Node Editor, which always writes v2 JSON.

### 2.2 Root Object Schema

```json
{
  "version": 2,
  "name": "MagicPortal",
  "emitters": [ /* EmitterNode[] */ ],
  "parameters": { /* named float/float4/texture parameters exposed to Lua */ },
  "asset_refs": [ /* all res:// paths used, for dependency tracking */ ]
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `version` | int | yes | Must be `2` for JSON format |
| `name` | string | yes | Human-readable effect name |
| `emitters` | EmitterNode[] | yes | One or more emitter definitions |
| `parameters` | object | no | Named parameters overridable at runtime via `ctx.vfx:set_param` |
| `asset_refs` | string[] | no | Explicit dependency list for hot-reload and pak bundling |

### 2.3 EmitterNode Schema

```json
{
  "id": "sparks",
  "enabled": true,
  "max_particles": 65536,
  "spawn": {
    "type": "burst",
    "count": 200,
    "rate": 0,
    "duration": 0.0,
    "looping": false
  },
  "modules": [ /* ModuleNode[] — ordered execution stack */ ],
  "renderer": { /* RendererNode */ },
  "lod": {
    "max_distance": 100.0,
    "scale_distance": 50.0,
    "min_particle_fraction": 0.1
  }
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `id` | string | — | Unique emitter identifier within this effect |
| `enabled` | bool | true | Can be toggled at runtime via `ctx.vfx:set_emitter_enabled` |
| `max_particles` | int | 65536 | GPU buffer allocation; must be a power of two |
| `spawn` | SpawnConfig | — | Emission mode and rate parameters |
| `modules` | ModuleNode[] | [] | Ordered simulation module stack |
| `renderer` | RendererNode | — | Rendering mode and texture reference |
| `lod` | LodConfig | — | Optional LOD override for this emitter |

### 2.4 SpawnConfig Schema

```json
{
  "type": "continuous",
  "rate": 40.0,
  "burst_count": 0,
  "duration": 0.0,
  "looping": true,
  "shape": {
    "type": "cone",
    "cone_angle": 15.0,
    "cone_radius": 0.1
  }
}
```

| `type` | Behavior |
|--------|----------|
| `continuous` | `rate` particles per second, forever (or `duration` seconds) |
| `burst` | `count` particles at t=0, then stop |
| `burst_loop` | `count` particles every `rate` seconds |
| `sub_emitter` | Spawns N particles when a parent particle dies |

Shape types: `point`, `sphere`, `hemisphere`, `cone`, `box`, `cylinder`, `mesh_surface`,
`spline`. Each shape type has its own parameter sub-object.

### 2.5 ModuleNode Schema

```json
{
  "type": "ColorOverLife",
  "enabled": true,
  "gradient": [
    [0.0,  "#ffffff"],
    [0.5,  "#8080ff"],
    [1.0,  "#0000ff00"]
  ]
}
```

The `type` field selects the built-in module or `"CustomHLSL"` for user-authored code. All
other fields are module-specific parameters. Unknown fields are silently ignored by the
loader (forwards compatibility).

### 2.6 RendererNode Schema

```json
{
  "type": "Billboard",
  "texture": "res://textures/vfx/spark.png",
  "blend": "additive",
  "cast_shadow": false,
  "receive_shadow": false,
  "sort_mode": "back_to_front",
  "velocity_aligned": false,
  "flipbook": {
    "columns": 4,
    "rows": 2,
    "fps": 12.0,
    "random_start_frame": true
  }
}
```

Renderer types: `Billboard`, `Mesh`, `Ribbon`, `Beam`, `Flipbook`.

### 2.7 Complete Example: MagicPortal

```json
{
  "version": 2,
  "name": "MagicPortal",
  "emitters": [
    {
      "id": "sparks",
      "max_particles": 2048,
      "spawn": {
        "type": "continuous",
        "rate": 80.0,
        "shape": { "type": "cone", "cone_angle": 20.0, "cone_radius": 0.3 }
      },
      "modules": [
        { "type": "InitVelocityRadial",  "speed_min": 2.0, "speed_max": 8.0 },
        { "type": "InitLifetime",        "min": 0.8, "max": 1.6 },
        { "type": "InitSize",            "min": 0.04, "max": 0.12 },
        { "type": "CustomHLSL",          "shader": "res://vfx/portal_spiral.hlsl" },
        { "type": "ColorOverLife",
          "gradient": [[0,"#fff"],[0.5,"#8080ff"],[1,"#0000ff00"]] },
        { "type": "SizeOverLife",
          "curve": [[0,0.0],[0.1,1.0],[0.8,1.0],[1.0,0.0]] },
        { "type": "LightingPBR",
          "receives_shadows": true, "emissive_boost": 3.0 }
      ],
      "renderer": {
        "type": "Billboard",
        "texture": "res://textures/vfx/spark.png",
        "blend": "additive"
      }
    },
    {
      "id": "smoke_ring",
      "max_particles": 256,
      "spawn": {
        "type": "burst_loop",
        "count": 16,
        "rate": 0.5,
        "shape": { "type": "sphere", "radius": 0.8 }
      },
      "modules": [
        { "type": "InitVelocityOutward", "speed_min": 0.1, "speed_max": 0.4 },
        { "type": "InitLifetime",        "min": 2.0, "max": 3.0 },
        { "type": "InitSize",            "min": 0.3, "max": 0.6 },
        { "type": "SizeOverLife",
          "curve": [[0,0.5],[0.3,1.0],[1.0,2.0]] },
        { "type": "ColorOverLife",
          "gradient": [[0,"#6060ff80"],[0.7,"#303090"],[1,"#00000000"]] },
        { "type": "SubsurfaceScatter",
          "scattering_color": "#4040ff", "thickness": 0.2 },
        { "type": "Gravity", "scale": -0.1 },
        { "type": "LinearDrag", "drag": 0.6 }
      ],
      "renderer": {
        "type": "Billboard",
        "texture": "res://textures/vfx/ai_generated/magic_portal/smoke.png",
        "blend": "alpha"
      }
    }
  ],
  "parameters": {
    "color_tint": { "type": "float4", "default": [1,1,1,1] },
    "intensity":  { "type": "float",  "default": 1.0 }
  }
}
```

---

## 3. AI Generation Pipeline

### 3.1 MCP Tool: `vfx_generate`

```typescript
interface VfxGenerateArgs {
  description:        string;          // natural language: "purple fire tornado"
  style?:             string;          // "stylized" | "realistic" | "cartoon" | "sci-fi"
  color_palette?:     string[];        // hex colors to bias palette generation
  reference_image?:   string;          // base64-encoded PNG for visual reference
  max_emitters?:      number;          // default 4; limit graph complexity
  budget_particles?:  number;          // default 65536; total across all emitters
}

interface VfxGenerateResult {
  effect_path:        string;          // res:// path where .uzfx was written
  textures:           string[];        // res:// paths of generated sprites
  hlsl_modules:       string[];        // res:// paths of generated .hlsl files
  description:        string;          // AI's description of what it built
  token_count:        number;
}
```

**Execution Steps (server-side):**

1. Build a system prompt that includes:
   - The full `vfx_list_modules()` output (all built-in module schemas)
   - The `.uzfx` v2 JSON schema
   - The `CustomHLSL` module interface
   - The `ParticleState` HLSL struct definition
   - Style and color palette hints from the request

2. Call Claude API (`claude-opus-4-6`) with the system prompt and the user `description`.
   Request JSON output matching the root `.uzfx` schema.

3. Validate the returned JSON against the schema. If validation fails, send an error message
   back to the model and request a corrected version (max 2 retries).

4. For each module with `type: "CustomHLSL"`, extract the `shader_source` field from the
   AI response, save it to `res://vfx/{effect_name}/{module_id}.hlsl`, and replace
   `shader_source` with a `shader` file reference.

5. For each renderer texture slot that references `"ai://generate/{prompt}"`:
   - Call `nanobanana.generate_sprite(prompt, style, 512)` or
     `nanobanana.generate_flipbook(prompt, frames=8, rows=2, cols=4)` as appropriate.
   - Save the result to `res://textures/vfx/ai_generated/{effect_name}/{slot}.png`.
   - Replace the `"ai://generate/..."` URL with the `res://` path.

6. Write the finalized `.uzfx` JSON to `res://vfx/{effect_name}.uzfx`.

7. Send an IPC `vfx.hotReload` message to the engine, which rebuilds the compute shader
   permutations for the new emitter graph.

8. Wait up to 2 seconds for the engine to confirm hot-reload completion (`vfx.hotReloadDone`).

9. Return `VfxGenerateResult` to the caller.

### 3.2 MCP Tool: `vfx_refine`

```typescript
interface VfxRefineArgs {
  effect_path:          string;      // res:// path to existing .uzfx
  feedback_description: string;      // "make the sparks faster and more blue"
  screenshot_base64?:   string;      // optional: current viewport screenshot
}

interface VfxRefineResult {
  effect_path:          string;
  changes_description:  string;      // what was changed and why
}
```

**Execution Steps:**

1. Load the existing `.uzfx` JSON from `effect_path`.
2. Build a prompt that includes: the current JSON, the feedback description, and (if provided)
   the screenshot encoded in the message.
3. Ask Claude to return a modified JSON (diff only, or full graph — model decides).
4. Merge changes into the loaded graph, validate, write back to disk.
5. Hot-reload and return the result.

The feedback loop enables iterative refinement without starting from scratch. The VSCode VFX
Node Editor's "Refine with AI" button captures the current viewport screenshot automatically
before calling `vfx_refine`.

### 3.3 MCP Tool: `vfx_list_modules`

```typescript
interface ModuleSchema {
  type:        string;
  category:    "spawn" | "init" | "update" | "render" | "lighting";
  description: string;
  params:      Record<string, { type: string; default: unknown; description: string }>;
}

// Returns all built-in modules with their full parameter schemas.
// Used as context injection when calling Claude for vfx_generate.
function vfx_list_modules(): ModuleSchema[]
```

### 3.4 Screenshot Feedback Loop

The MCP server maintains a `VfxSession` object per active generation. After the initial
`vfx_generate` call returns, the session records:

- The original description
- The generated effect path
- The AI's internal reasoning about the design

If `vfx_refine` is called within the same session (within 10 minutes), the prior reasoning
is injected as additional context, enabling the model to make coherent incremental changes
rather than regenerating from scratch.

---

## 4. Built-in Simulation Modules

All built-in modules are implemented as HLSL include files in `src/vfx/modules/`.
The `ModuleCompiler` concatenates the relevant includes and compiles a single
`simulate.hlsl` permutation per emitter. A compile-time `#define` flag enables each module.

### 4.1 Spawn Modules

| Module Type | Description | Key Parameters |
|-------------|-------------|----------------|
| `SpawnPoint` | All particles spawn at emitter origin | — |
| `SpawnSphere` | Uniform random on sphere surface or volume | `radius`, `surface_only` |
| `SpawnHemisphere` | Spawn on upper hemisphere | `radius`, `up_axis` |
| `SpawnCone` | Spawn in a cone frustum | `cone_angle`, `cone_radius`, `length` |
| `SpawnBox` | Spawn within an AABB | `half_extents`, `surface_only` |
| `SpawnCylinder` | Spawn in a cylinder | `radius`, `height`, `surface_only` |
| `SpawnMeshSurface` | Spawn on a mesh's triangles, area-weighted | `mesh_path` |
| `SpawnSpline` | Spawn along a Bezier spline | `spline_path`, `jitter_radius` |

### 4.2 Init Modules

| Module Type | Description | Key Parameters |
|-------------|-------------|----------------|
| `InitPosition` | Offset from spawn shape | `offset`, `random_spread` |
| `InitVelocityDirectional` | Uniform direction with spread | `direction`, `speed_min/max`, `cone_spread` |
| `InitVelocityRadial` | Outward from emitter center | `speed_min`, `speed_max` |
| `InitVelocityOutward` | Normal to spawn surface | `speed_min`, `speed_max`, `normal_blend` |
| `InitVelocityRandom` | Fully random unit direction | `speed_min`, `speed_max` |
| `InitColor` | Set initial RGBA from range | `color_min`, `color_max` |
| `InitSize` | Set initial size from range | `min`, `max` |
| `InitRotation` | Set initial billboard rotation | `min_deg`, `max_deg` |
| `InitLifetime` | Override lifetime from range | `min`, `max` |
| `InitFromParent` | Inherit parent particle state (sub-emitters) | `inherit_velocity_fraction` |

### 4.3 Update Modules

| Module Type | Description | Key Parameters |
|-------------|-------------|----------------|
| `Gravity` | Apply gravity vector | `scale` (default 1.0) |
| `LinearDrag` | Velocity damping | `drag` (0..1 per second) |
| `CurlNoise` | Divergence-free turbulence | `strength`, `frequency`, `scroll_speed`, `octaves` |
| `VortexAttractor` | Spin particles around an axis | `axis`, `radius`, `strength`, `pull_to_axis` |
| `PointAttractor` | Pull / repel toward a world point | `position`, `strength`, `falloff_radius` |
| `TurbulenceNoise` | Non-divergence-free noise force | `strength`, `frequency`, `octaves` |
| `SpringToTarget` | Pull toward a moving target | `target_param`, `spring_k`, `damping` |
| `DepthCollision` | Screen-space depth buffer collision | `bounce`, `friction`, `kill_on_collide` |
| `SphereCollision` | Collision against N sphere colliders | `collider_list_param`, `bounce`, `friction` |
| `ColorOverLife` | Multi-stop gradient over age | `gradient` (array of [t, hex]) |
| `SizeOverLife` | Curve-based size over age | `curve` (array of [t, scale]) |
| `RotationOverLife` | Spin rate over age | `rotation_speed_min/max` |
| `VelocityOverLife` | Add velocity over age | `velocity_start`, `velocity_end`, `space` |
| `OrbitAroundAxis` | Orbit emitter axis | `axis`, `angular_speed_min/max` |
| `SubEmitter` | Spawn a child emitter when particle dies | `effect_path`, `inherit_velocity` |
| `KillZone` | Kill particles outside a volume | `volume_type`, `volume_params`, `invert` |

### 4.4 Render Modules

| Module Type | Description | Key Parameters |
|-------------|-------------|----------------|
| `Billboard` | Camera-facing quad | `texture`, `blend`, `velocity_aligned` |
| `Mesh` | Oriented mesh per particle | `mesh_path`, `scale_by_size` |
| `Ribbon` | Continuous strip connecting ordered particles | `width`, `texture`, `uv_tile` |
| `Beam` | Straight beam between two points | `start_param`, `end_param`, `segments` |
| `Flipbook` | Animated sprite sheet | `columns`, `rows`, `fps`, `random_start_frame` |

### 4.5 Lighting Modules

| Module Type | Description | Key Parameters |
|-------------|-------------|----------------|
| `LightingPBR` | Write to GBuffer (opaque) or use probe-based ambient (transparent) | `receives_shadows`, `emissive_boost`, `roughness`, `metallic` |
| `SubsurfaceScatter` | Volumetric SSS approximation for smoke/fire | `scattering_color`, `thickness`, `phase_function` |
| `EmissiveSelf` | Particle contributes emissive to scene (no GBuffer) | `emissive_scale`, `color` |
| `CParticleLight` | Each particle spawns a dynamic point light | `radius`, `intensity`, `color_from_particle` |
| `ReceiveShadow` | Particle samples shadow maps | `shadow_cascade_max` |

---

## 5. CustomHLSL Module

### 5.1 Required Entry Point

Users provide an HLSL file containing a function with this exact signature:

```hlsl
// res://vfx/my_custom_module.hlsl
#include "res://shaders/include/vfx_particle_state.hlsli"
#include "res://shaders/include/vfx_math.hlsli"

// Optional: declare custom parameters via cbuffer
cbuffer CustomParams : register(b4)
{
    float4 spiral_params;  // x=frequency, y=amplitude, z=phase, w=unused
    float4 attractor_pos;
};

// Required entry point — called once per live particle per simulation tick
void CustomUpdate(inout ParticleState p, float dt, float3 emitter_pos, uint particle_seed)
{
    // Example: logarithmic spiral orbit
    float age_t   = 1.0 - (p.lifetime_remain / p.lifetime_total);
    float angle   = age_t * spiral_params.x * TWO_PI + spiral_params.z;
    float radius  = (1.0 - age_t) * spiral_params.y;

    float3 target = emitter_pos + float3(cos(angle) * radius, 0, sin(angle) * radius);
    p.position   += (target - p.position) * min(dt * 8.0, 1.0);
}
```

### 5.2 `ParticleState` HLSL Struct

Located in `res://shaders/include/vfx_particle_state.hlsli`:

```hlsl
struct ParticleState {
    float3 position;          // world-space position
    float  lifetime_remain;   // remaining lifetime (s); < 0 = dead

    float3 velocity;          // current velocity (m/s)
    float  lifetime_total;    // total lifetime for age normalization

    float4 color;             // current RGBA linear

    float  size;              // world-space billboard size (m)
    float  rotation;          // billboard rotation (radians)
    float  custom0;           // per-particle custom float (general purpose)
    float  custom1;           // per-particle custom float (general purpose)

    uint   seed;              // per-particle random seed
    uint   emitter_id;        // source emitter slot index
    uint2  _pad;
};
// Total: 80 bytes per particle. One emitter at 65536 particles = 5.1 MB.
```

### 5.3 Custom Parameter Binding

The `modules[]` entry for a `CustomHLSL` module may include a `params` object whose values
are uploaded to a per-emitter cbuffer at slot `b4`:

```json
{
  "type": "CustomHLSL",
  "shader": "res://vfx/portal_spiral.hlsl",
  "params": {
    "spiral_params": [6.28, 1.5, 0.0, 0.0],
    "attractor_pos": [0.0, 1.0, 0.0, 0.0]
  }
}
```

Parameters declared in `parameters{}` at the effect root level can be referenced here with
a `"@param_name"` string; the runtime substitutes the live value.

### 5.4 Hot-Reload

The `VFXHotReloader` watches all `.hlsl` paths referenced by active effects. When a file
changes on disk:

1. Recompile only the affected permuted shader.
2. Upload the new CSO blob to the GPU.
3. Swap the PSO on the next frame boundary (no mid-frame swap).

End-to-end hot-reload latency target: < 150 ms for single-emitter effects.

### 5.5 AI-Generated CustomHLSL

When `vfx_generate` determines that a requested behavior cannot be expressed with built-in
modules (e.g., "particles orbit in a Lissajous figure"), it generates an HLSL file instead
of an unsupported module. The system prompt instructs the model to:

- Conform to the `CustomUpdate` signature.
- Use only `ParticleState` fields and the injected includes.
- Avoid texture reads (available from `b4` cbuffer only).
- Keep the function under 100 HLSL lines to ensure low compile latency.

---

## 6. Physically-Based Particle Lighting

### 6.1 Rendering Tiers

Particles are sorted into three rendering tiers, selected per emitter by the modules present:

| Tier | Module Required | Render Path | Lighting |
|------|----------------|-------------|---------|
| Unlit | none / `EmissiveSelf` | Forward, after opaque pass | None / self-emissive |
| Lit Transparent | `LightingPBR` | Forward+, after GBuffer | Probe ambient + N nearest lights |
| Opaque PBR | `LightingPBR` + `blend: opaque` | GBuffer write | Full deferred PBR + shadows |

### 6.2 Opaque Particles (GBuffer Write)

When `renderer.blend == "opaque"` and `LightingPBR` is present, the billboard vertex shader
writes to the G-Buffer exactly like a static mesh surface. This tier is appropriate for rock
fragments, leaves, and similar geometry-like particles. The full deferred lighting pass
applies automatically.

### 6.3 Transparent Particles — Particle Lighting Cache

Transparent particles cannot write to the G-Buffer. Instead, the `VFXSystem` maintains a
**Particle Lighting Cache**: a low-frequency (every 4 frames) compute pass that samples:

- The SH probe grid (IBL ambient) at the emitter world origin.
- The 4 nearest dynamic lights by world-space distance from the emitter center.

The resulting compact lighting struct (`ParticleLightingData`, 64 bytes) is uploaded to a
cbuffer consumed during the particle render pass. All particles in the emitter share this
single lighting sample. This approximation is acceptable because particles rarely need
per-particle shadowing at rendering scales below 0.5 m.

```hlsl
cbuffer ParticleLightingData : register(b5)
{
    float4 ambient_sh[7];          // L1 SH (3 bands, 7 coefficients × float4)
    float4 light_pos_radius[4];    // xyz=position, w=radius
    float4 light_color_intensity[4]; // xyz=color, w=intensity
};
```

### 6.4 Volumetric Particles — Froxel Injection

When the `SubsurfaceScatter` module is active (typically for smoke, fire, or fog), the
particle system writes density and scattering color into the volumetric froxel grid
(see `docs/specs/rendering/screen-space-effects.md` — Volumetric Fog section). Each particle
injects:

- `extinction`: a scalar proportional to particle size and `thickness` parameter.
- `scattering_albedo`: the `scattering_color` parameter.
- `phase`: the Henyey-Greenstein asymmetry factor from `phase_function`.

This allows smoke and fire particles to participate in the global volumetric lighting pass
(including god rays and volumetric shadows) without a separate render path.

### 6.5 `CParticleLight` — Per-Particle Dynamic Lights

When the `CParticleLight` module is present, once per frame the system executes a compact
GPU scan over the AliveList, writing the world positions and colors of the N brightest
particles (by emissive intensity) into a secondary light list appended to the scene's
dynamic light buffer. N is capped at `64` per emitter and `256` globally across all emitters
to bound light-list cost.

This makes fire-particle lights, spell-impact flashes, and muzzle-flash lighting essentially
free — they piggyback on the existing clustered shading infrastructure.

### 6.6 Shadow Casting

Particles can cast shadows when `renderer.cast_shadow = true`. The shadow pass renders
billboard quads into a dedicated particle shadow atlas (1024×1024 per cascade) using an
alpha-threshold cutout render. This is expensive and should be enabled only for
high-importance large-particle effects (rolling smoke columns, large debris).

---

## 7. VSCode VFX Node Editor Webview

### 7.1 Architecture

```
VSCode Extension (TypeScript)
└── VFXEditorPanel (WebviewPanel)
    ├── React application bundled with esbuild
    ├── reactflow for node graph rendering
    ├── Communicates with extension host via postMessage / acquireVsCodeApi
    └── Extension host relays IPC calls to the engine and MCP calls to the MCP server
```

The panel is opened by:
- Clicking a `.uzfx` file in the Asset Browser → opens VFX Node Editor with that file loaded.
- Right-clicking a ParticleEmitter node in the Scene Tree → "Open in VFX Editor".
- Command palette: `UzEngine: Open VFX Editor`.

### 7.2 Canvas Layout

```
┌──────────────────────────────────────────────────────────────────┐
│  Toolbar: [New Emitter] [Add Module ▾] [Generate ✦] [Refine ✦]  │
│           [Save] [Revert] [Preview ▷] [Timeline ◄►]              │
├────────────────────────┬─────────────────────────────────────────┤
│  Node Canvas           │  Inspector Panel                        │
│                        │                                         │
│  ┌──[Emitter: sparks]  │  Selected: ColorOverLife                │
│  │  Spawn: cone        │  ─────────────────────────              │
│  │  Modules:           │  Gradient stops:                        │
│  │  ├─ InitVelocity    │  ● t=0.00  #ffffff ─────────────────   │
│  │  ├─ CustomHLSL ✎   │  ● t=0.50  #8080ff ─────────────────   │
│  │  ├─ ColorOverLife ● │  ● t=1.00  #0000ff00 ───────────────   │
│  │  └─ LightingPBR     │  [+ Add Stop]                           │
│  │  Renderer: Billboard│                                         │
│  │  Texture: [thumb]   │  [Apply] [Reset]                        │
│  └──────────────────   │                                         │
│                        │  AI Suggestion:                         │
│  ┌──[Emitter: smoke]   │  "Increase frequency for tighter spiral"│
│  │  ...                │  [Apply Suggestion]                     │
└──────────────────────────────────────────────────────────────────┘
│  Status: 1,248 particles live | Hot-reload: OK (112 ms)          │
└──────────────────────────────────────────────────────────────────┘
```

### 7.3 Node Representations

Each `.uzfx` emitter becomes a **vertical module stack card** (resembling Niagara's emitter
stack rather than a generic node graph). Each module inside the emitter is a row in the
stack. Connections between emitters (sub-emitter relationships) are drawn as arrows between
cards.

The visual design prioritizes vertical scanability over arbitrary graph freedom because VFX
emitters have a fixed execution order (spawn → init → update → render), not arbitrary
dataflow.

### 7.4 Module Palette

Clicking "Add Module" opens a searchable palette grouped by category (Spawn, Init, Update,
Render, Lighting, Custom). Each entry shows:

- Module name and brief description
- Parameter count
- A small icon indicating the category
- "AI Describe" action: type a description, AI suggests which built-in module fits best.

### 7.5 AI Generation UI

**"Generate with AI" flow:**
1. User clicks the "Generate ✦" toolbar button.
2. A modal opens with fields: Description (textarea), Style (dropdown), Color Palette
   (color pickers), Reference Image (file drop).
3. User clicks "Generate". The Webview sends a `vfx_generate` request to the extension host.
4. A progress indicator shows the three phases: "Writing graph...", "Generating shaders...",
   "Creating textures...".
5. When complete, the node canvas re-renders with the new graph. The engine viewport shows
   the live effect. The inspector shows the AI's description of what was built.

**"Refine with AI" flow:**
1. User clicks "Refine ✦".
2. A panel slides in with a single "Feedback" textarea. A "Capture Viewport" button takes a
   screenshot of the current engine render.
3. User types feedback (e.g., "the smoke rises too slowly, needs more blue in the gradient")
   and clicks "Refine".
4. On completion, the graph updates and hot-reloads. A diff summary is shown in the inspector:
   "Changed: smoke_ring.modules[LinearDrag].drag 0.6 → 0.3, smoke_ring.modules[ColorOverLife]
   gradient stop 1 #303090 → #1010ff".

### 7.6 Real-Time Preview and Timeline

The Webview maintains a persistent connection to the engine via the extension host IPC
channel. Any parameter change in the Inspector triggers:

1. A JSON patch serialized from the diff between the current graph state and the previous
   saved state.
2. An IPC `vfx.patchParam` message sent to the engine.
3. The engine applies the patch without recompiling shaders (for parameter-only changes) or
   triggers a full hot-reload (for structural changes like adding/removing modules).

The **Timeline Scrubber** at the bottom of the canvas sends `vfx.setPlaybackTime(t)` IPC
messages, allowing preview of one-shot burst effects at arbitrary time offsets.

### 7.7 Texture Slot Thumbnails

Each renderer node in the graph displays a 48×48 thumbnail of its texture:

- For `res://` textures: loaded from disk via the extension's file system access.
- For AI-generated textures: shown immediately after the Nano Banana call completes,
  before the engine hot-reload finishes.
- Clicking the thumbnail opens a "Change Texture" dialog with a search field that queries
  the Asset Browser's texture index, plus a "Generate with AI" sub-action.

### 7.8 Webview Message Protocol

```typescript
// Webview → Extension Host
type WebviewToHost =
  | { type: "save"; graph: VfxGraph }
  | { type: "vfx_generate"; args: VfxGenerateArgs }
  | { type: "vfx_refine"; args: VfxRefineArgs }
  | { type: "param_patch"; effectPath: string; patch: JsonPatch[] }
  | { type: "set_playback_time"; t: number }
  | { type: "capture_screenshot" }
  | { type: "ready" };

// Extension Host → Webview
type HostToWebview =
  | { type: "graph_loaded"; graph: VfxGraph; effectPath: string }
  | { type: "generate_progress"; phase: string; pct: number }
  | { type: "generate_done"; result: VfxGenerateResult }
  | { type: "refine_done"; result: VfxRefineResult }
  | { type: "hot_reload_done"; latency_ms: number }
  | { type: "particle_stats"; counts: Record<string, number> }
  | { type: "screenshot"; base64: string };
```

---

## 8. Nano Banana Integration

### 8.1 API Overview

Nano Banana is a high-quality AI sprite and texture generation service. The MCP server calls
it during `vfx_generate` to produce sprites for each texture slot that requests AI generation.

```typescript
interface NanoBananaClient {
  generate_sprite(
    prompt:     string,
    style:      "stylized" | "realistic" | "cartoon" | "sci-fi",
    resolution: 256 | 512 | 1024
  ): Promise<Buffer>;  // PNG

  generate_flipbook(
    prompt:  string,
    frames:  number,    // default 8
    rows:    number,    // default 2
    cols:    number,    // default 4
    style:   string
  ): Promise<Buffer>;  // PNG sprite sheet

  generate_normal_map(
    prompt:     string,
    resolution: number
  ): Promise<Buffer>;  // normal map PNG for PBR smoke/debris
}
```

### 8.2 Integration in `vfx_generate`

During Step 3 of the generation pipeline, the MCP server scans the AI-generated `.uzfx`
graph for texture values matching the pattern `"ai://generate/{url_encoded_prompt}"`. For
each such value:

1. Decode the prompt from the URL.
2. Determine whether the effect needs a static sprite or a flipbook by checking the
   renderer's `flipbook` sub-object.
3. Call the appropriate Nano Banana method.
4. Save the PNG to `res://textures/vfx/ai_generated/{effect_name}/{slot}.png`.
5. If the effect uses `LightingPBR`, optionally generate a matching normal map at the same
   prompt with a "normal map texture" suffix.
6. Trigger asset import (mip chain generation, optional BC7 compression for DDS output).

### 8.3 Prompt Construction

The MCP server constructs Nano Banana prompts from two sources:

- The `texture_hint` field that Claude may include in the generated `.uzfx` for each texture
  slot (e.g., `"texture_hint": "wispy purple smoke particle, transparent background"`).
- A fallback constructed from the renderer type + effect name if no hint is present
  (e.g., `"smoke particle sprite for MagicPortal VFX effect, transparent background, top
  view, stylized"`).

Style is inherited from the `style` parameter passed to `vfx_generate`. If no style is
specified, the MCP server asks Claude to infer the style from the description.

### 8.4 Storage Layout

```
res://textures/vfx/ai_generated/
└── {effect_name}/
    ├── {emitter_id}_color.png        -- primary sprite (RGBA)
    ├── {emitter_id}_normal.png       -- optional normal map
    ├── {emitter_id}_flipbook.png     -- sprite sheet (if flipbook renderer)
    └── metadata.json                 -- generation params for reproducibility
```

`metadata.json` records the Nano Banana prompts, style, and seed used, enabling artists to
re-generate with slight prompt modifications without going through `vfx_generate` again.

### 8.5 Import into Asset Pipeline

Generated PNGs are automatically imported by the `AssetPipeline` watcher:

- Mip chain generated via `DirectXTex`.
- BC7 compression applied (lossy, ~8:1 compression ratio for a 512×512 sprite).
- Output written as `.dds` alongside the original `.png` (engine loads `.dds` at runtime,
  VSCode thumbnails use `.png`).
- Added to the `.uzpak` manifest for the current project.

---

## 9. Effect Library and Presets

### 9.1 Built-in Presets

The following preset `.uzfx` files ship with the engine in `res://vfx/presets/`:

| Preset Name | Description | Emitter Count | Approx. Particle Budget |
|-------------|-------------|---------------|------------------------|
| `fire_small` | Small campfire or candle | 2 | 4,096 |
| `fire_large` | Large bonfire with embers | 4 | 16,384 |
| `smoke_thin` | Thin wispy smoke column | 1 | 2,048 |
| `smoke_dense` | Dense rolling smoke | 3 | 12,288 |
| `explosion_small` | Grenade-scale explosion | 5 | 8,192 |
| `explosion_large` | Building-demolition scale | 8 | 32,768 |
| `magic_portal` | Swirling mystical portal | 3 | 6,144 |
| `sparks_impact` | Metal-on-metal sparks | 1 | 1,024 |
| `rain` | Rainfall (area emitter) | 1 | 65,536 |
| `snow` | Snowfall (area emitter) | 1 | 16,384 |
| `blood_splatter` | Impact splatter + drip | 3 | 2,048 |
| `dust_kick` | Ground dust from footstep | 1 | 512 |
| `electric_arc` | Tesla-coil style arc | 2 | 1,024 |
| `healing_burst` | Golden healing light | 3 | 4,096 |
| `poison_cloud` | Toxic gas lingering cloud | 2 | 8,192 |

### 9.2 Preset Metadata

Each preset ships with a sibling `.uzfx.meta` JSON file:

```json
{
  "name":        "fire_small",
  "tags":        ["fire", "environment", "loop", "pbr"],
  "preview_gif": "res://vfx/presets/thumbnails/fire_small.gif",
  "author":      "AI-generated",
  "phase":       4,
  "budget":      { "particles": 4096, "emitters": 2 },
  "lighting":    "LightingPBR + CParticleLight"
}
```

### 9.3 VSCode Asset Browser Integration

The Asset Browser panel displays a "VFX" section under the Assets tree. Each `.uzfx` entry:

- Shows the animated GIF preview thumbnail on hover.
- Can be dragged into the Scene Tree to create a `ParticleEmitter` node.
- Has a right-click context menu: "Open in VFX Editor", "Duplicate", "Generate Variant with
  AI" (pre-fills the `vfx_generate` dialog with the preset's tags as style hints).

### 9.4 Indexing AI-Generated Effects

All effects written by `vfx_generate` into `res://vfx/` are scanned at project open by the
`VFXIndex` service. For each `.uzfx` file, it extracts:

- Effect name, emitter count, module types used, renderer types.
- Tags inferred from the original generation description (stored in a `"_ai_metadata"` root
  key in the JSON).
- A thumbnail request (sent asynchronously to the engine via IPC `vfx.renderThumbnail`).

The index is written to `res://.uzindex/vfx_index.json` and used by the Asset Browser search.

---

## 10. Runtime Lua API (Extended)

The Phase 4 Lua API extends the Phase 3 `ctx.vfx` table with new methods:

```lua
-- -------------------------------------------------------------------------
-- Spawn and control (inherited from Phase 3, unchanged)
-- -------------------------------------------------------------------------
local handle = ctx.vfx:spawn("res://vfx/explosion.uzfx", pos, rot)
ctx.vfx:stop(handle)
ctx.vfx:stop_immediate(handle)
ctx.vfx:set_rate(handle, rate)

-- -------------------------------------------------------------------------
-- Phase 4 additions
-- -------------------------------------------------------------------------

-- Override a named parameter declared in the effect's "parameters" block
ctx.vfx:set_param(handle, "color_tint", {1, 0, 0, 1})
ctx.vfx:set_param(handle, "intensity",  2.5)

-- Enable/disable a specific emitter by id within a multi-emitter effect
ctx.vfx:set_emitter_enabled(handle, "sparks", true)
ctx.vfx:set_emitter_enabled(handle, "smoke_ring", false)

-- Attach to an entity (effect follows entity transform)
ctx.vfx:attach(handle, entity_id)
ctx.vfx:detach(handle)

-- Async AI generation at runtime (for procedural games)
-- Calls vfx_generate MCP tool; callback fires when effect is ready
local handle = ctx.vfx:generate_async(
    "a golden healing burst with swirling light",
    function(h, err)
        if err then return end
        ctx.vfx:spawn_at(h, player_pos)
    end
)

-- Query
local alive = ctx.vfx:is_alive(handle)
local count = ctx.vfx:get_particle_count(handle)
local stats = ctx.vfx:get_stats(handle)
-- stats = { emitters=2, particles_alive=1248, particles_max=4096, gpu_ms=0.12 }
```

### Example: Dynamic Effect Generation

```lua
-- In a boss fight script: generate a unique death effect based on boss type
function on_boss_death(boss_type, world_pos)
    local description = string.format(
        "dramatic death explosion for a %s boss, epic scale, dark energy",
        boss_type
    )
    ctx.vfx:generate_async(description, function(handle, err)
        if err then
            -- Fallback to a preset if AI generation fails
            ctx.vfx:spawn("res://vfx/presets/explosion_large.uzfx", world_pos)
            return
        end
        ctx.vfx:spawn_at(handle, world_pos)
    end)
end
```

---

## 11. IPC Handlers

The following IPC messages are added for the VFX Graph System:

```
vfx.loadGraph        { path }                          → { graph: VfxGraph }
vfx.saveGraph        { path, graph: VfxGraph }         → { ok }
vfx.hotReload        { path }                          → { ok }
vfx.hotReloadDone    (event) { path, latency_ms }
vfx.patchParam       { handle, name, value }           → { ok }
vfx.setEmitterEnabled { handle, emitter_id, enabled } → { ok }
vfx.renderThumbnail  { path, time_t, width, height }   → { png_base64 }
vfx.getStats         { handle }                        → { emitters, particles_alive, particles_max, gpu_ms }
vfx.listPresets      {}                                → { presets: PresetMeta[] }
```

---

## 12. C++ System API (Extended)

```cpp
class VFXGraphSystem : public VFXSystem {
public:
    // Load or reload a .uzfx v2 graph; auto-detects v1 INI and converts
    EffectHandle load_effect(const std::string& path);

    // Hot-reload: recompile permuted shaders for changed emitters only
    void hot_reload(const std::string& path);

    // Patch a named parameter without recompiling
    void set_param(VFXHandle h, std::string_view name, std::span<const float> value);

    // Enable/disable emitter by id
    void set_emitter_enabled(VFXHandle h, std::string_view emitter_id, bool enabled);

    // Render a still thumbnail at time t for the VSCode Asset Browser
    // Renders to an offscreen target and returns a JPEG
    std::vector<uint8_t> render_thumbnail(const std::string& path,
                                           float t, uint32_t w, uint32_t h);

    // Called by VFXIndex to get stats for a handle
    VFXStats get_stats(VFXHandle h) const;
};
```

---

## 13. Limits and Performance Budget

| Constraint | Value |
|------------|-------|
| Max particles per emitter | 65,536 |
| Max simultaneous emitters per scene | 256 |
| Max emitters per `.uzfx` effect | 16 |
| Max modules per emitter | 16 |
| Max custom HLSL modules per emitter | 4 |
| Max `CParticleLight` entries per emitter | 64 |
| Max `CParticleLight` entries globally | 256 |
| GPU buffer per emitter | 5.1 MB (80 B × 65,536) |
| Hot-reload latency target (parameter-only) | < 16 ms (1 frame) |
| Hot-reload latency target (HLSL module change) | < 150 ms |
| `vfx_generate` end-to-end latency target | < 8 seconds (network-dependent) |
| Nano Banana API call timeout | 30 seconds |

### GPU Performance Targets

| Configuration | Target GPU frame time |
|---------------|-----------------------|
| 4 emitters, 64K particles each, no `SubsurfaceScatter` | < 1.0 ms |
| 8 emitters, 32K particles each, with `LightingPBR` | < 2.0 ms |
| 4 emitters with `SubsurfaceScatter` + froxel injection | < 3.0 ms |
| 256 `CParticleLight` entries feeding clustered shading | < 0.5 ms additional |

---

## 14. File Layout

```
src/
├── vfx/
│   ├── VFXGraphSystem.h / .cpp       — main C++ system (extends VFXSystem)
│   ├── VFXGraphLoader.h / .cpp       — .uzfx v2 JSON + v1 INI parsing
│   ├── ModuleCompiler.h / .cpp       — HLSL permutation builder + DXC wrapper
│   ├── VFXHotReloader.h / .cpp       — FileWatcher + PSO hot-swap
│   ├── VFXIndex.h / .cpp             — effect indexer for Asset Browser
│   └── modules/                      — per-module HLSL includes
│       ├── spawn/   (SpawnSphere.hlsli, SpawnCone.hlsli, ...)
│       ├── init/    (InitVelocityRadial.hlsli, InitColor.hlsli, ...)
│       ├── update/  (Gravity.hlsli, CurlNoise.hlsli, DepthCollision.hlsli, ...)
│       ├── render/  (Billboard.hlsli, Ribbon.hlsli, Beam.hlsli, ...)
│       └── lighting/(LightingPBR.hlsli, SubsurfaceScatter.hlsli, CParticleLight.hlsli)
res/
├── vfx/
│   ├── presets/                      — 15 built-in preset .uzfx files + .meta sidecars
│   └── (user-created effects)
├── textures/vfx/
│   ├── ai_generated/                 — Nano Banana output organized by effect name
│   └── builtin/                      — hand-authored particle sprite atlases
└── shaders/include/
    ├── vfx_particle_state.hlsli      — ParticleState struct + helpers
    └── vfx_math.hlsli                — noise functions, curve sampling, random utilities
vscode-extension/
└── src/
    ├── vfxEditorPanel.ts             — WebviewPanel host
    ├── vfxMcpBridge.ts              — wires vfx_generate/refine to MCP server
    └── webview/vfx-editor/           — React app (reactflow-based node canvas)
        ├── App.tsx
        ├── EmitterStackCard.tsx
        ├── ModulePalette.tsx
        ├── InspectorPanel.tsx
        ├── AiGenerateModal.tsx
        └── TimelineScrubber.tsx
mcp-server/
└── src/tools/
    ├── vfx_generate.ts
    ├── vfx_refine.ts
    └── vfx_list_modules.ts
```

---

## 15. Definition of Done

- [ ] `VFXGraphLoader` parses `.uzfx` v2 JSON; v1 INI files auto-convert with no data loss.
- [ ] All 15 built-in preset effects load and simulate at target GPU frame times.
- [ ] `CustomHLSL` module: custom `portal_spiral.hlsl` compiles, runs, and hot-reloads in
  < 150 ms.
- [ ] `vfx_generate` MCP tool: given "purple fire tornado", produces a playable `.uzfx` with
  at least 2 emitters, 1 Nano Banana sprite, and no schema validation errors.
- [ ] `vfx_refine` MCP tool: given feedback on an existing effect, produces a measurable
  change in the graph within 10 seconds.
- [ ] `LightingPBR` module: particles receive ambient SH and nearest-4 lights visually
  consistent with scene lighting.
- [ ] `SubsurfaceScatter` module: smoke emitter visible in the volumetric froxel pass.
- [ ] `CParticleLight`: 64 particle lights appear in clustered shading within 0.5 ms budget.
- [ ] VSCode VFX Node Editor: opens a `.uzfx` file, renders the graph, allows parameter
  editing, and hot-reloads the engine within 2 frames of a parameter change.
- [ ] "Generate with AI" button: completes the full pipeline end-to-end in < 15 seconds on a
  standard internet connection.
- [ ] Phase 3 Lua API (`ctx.vfx:spawn`, `stop`, `set_rate`) unchanged and passing all
  existing tests.
- [ ] New Phase 4 Lua API (`set_param`, `generate_async`, `attach`) covered by unit tests.
- [ ] GPU memory: 256 emitter slots at 5.1 MB each = 1,305.6 MB; must stay within the 1.5 GB
  VFX budget with all other systems active.
