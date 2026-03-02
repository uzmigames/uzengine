# Post-Process Pipeline Editor -- Tool Specification

> **Status**: Planned (Phase 2)
> **Owner**: `PostProcessPipeline` (C++ runtime) + VSCode extension (PP Editor WebView panel)
> **File format**: `.uzppv` (Post-Process Volume, INI-like, AI-friendly)
> **Dependencies**: Renderer, Screen-Space Effects, Shader System, Camera System, Scene System
> **Related specs**: [renderer.md](../specs/renderer.md), [screen-space-effects.md](../specs/screen-space-effects.md), [shader-system.md](../specs/shader-system.md), [camera-system.md](../specs/camera-system.md)

---

## 1. Overview & Goals

The Post-Process Pipeline Editor is the **single authoritative tool** where level designers, technical artists, and cinematographers configure every rendering decision that happens after geometry rasterization. It manages Post-Process Volumes (PPVs): trigger zones in the 3D scene that blend post-process settings based on camera proximity, priority, and artist-defined falloff curves.

This tool is analogous to Unreal Engine's Post-Process Volume system but designed from the ground up for UzEngine's AI-native workflow: all configuration lives in plain-text `.uzppv` files that humans, LLMs, and the VSCode extension can read and write.

### Design Principles

| Principle | Description |
|-----------|-------------|
| **Volume-driven** | Every post-process setting originates from a volume. No hardcoded defaults -- the global volume is just a volume with the lowest priority. |
| **Override-based** | Each volume only stores the settings it explicitly overrides. Un-overridden settings fall through to the next-lower-priority volume (or the global default). |
| **Blendable** | Overlapping volumes blend smoothly using artist-tunable falloff curves. Camera position relative to volume boundaries drives the blend weight. |
| **AI-friendly** | `.uzppv` files are INI-like plain text. An AI agent can generate a "horror ambiance" volume with a single MCP call. |
| **Live preview** | Every parameter change streams instantly to the DX12 pipeline via JPEG+WebSocket. No export or compile step. |
| **Non-destructive** | Volumes override rendering state temporarily. Removing a volume restores the previous state with no side effects. |

### Target Users

| Persona | Workflow |
|---------|----------|
| **Level Designer** | Place a volume in the dungeon entrance that shifts color grading to cold blue, raises fog density, and enables stronger vignette. Tune the blend radius so the transition feels gradual as the player walks in. |
| **Technical Artist** | Create the global volume with carefully calibrated ACES tone mapping, auto-exposure range, and HBAO+ settings. Define a library of reusable `.uzppv` presets (forest, desert, underwater, combat). |
| **Cinematographer** | Push a per-camera PPV override during a cutscene: shallow depth of field, high bloom, desaturated color grading. Pop it when the cutscene ends. |
| **AI Agent** | Generate a `.uzppv` file from a natural language prompt via `postprocess.generate` MCP tool: "create a warm sunset volume with orange tint, strong bloom, and lens flare". |

### Output

- `.uzppv` files stored under `res://volumes/` (or any project path)
- Applied at runtime by `PostProcessPipeline` (C++) or `ctx.postprocess` (Lua)
- Exportable as standalone presets for sharing across projects

---

## 2. Post-Process Volume System

### Volume Hierarchy

```
Scene
  |
  +-- Global Volume (always active, priority 0, provides scene defaults)
  |     - color_grading: neutral
  |     - tonemapping: ACES
  |     - bloom: intensity 0.5
  |     - ssao: medium quality
  |
  +-- Local Volume: "dungeon_entrance" (box, priority 10, blend radius 5m)
  |     - color_grading: temperature -1500K, saturation 0.7
  |     - fog_override: density 0.02, color [0.1, 0.12, 0.18]
  |     - vignette: intensity 0.6
  |
  +-- Local Volume: "boss_arena" (sphere, priority 20, blend radius 8m)
  |     - bloom: intensity 1.5, threshold 0.8
  |     - chromatic_aberration: intensity 0.15
  |     - motion_blur: shutter_angle 270
  |
  +-- Per-Camera Override: "cinematic_cam_01" (priority 1000, no blend)
        - dof: focal_distance 3.5, f_stop 1.4
        - color_grading: saturation 0.5, contrast 1.3
```

### Volume Types

| Type | Shape | Bounds Definition | Use Case |
|------|-------|-------------------|----------|
| **Global** | Infinite | No bounds -- always active everywhere | Scene defaults |
| **Box** | Axis-aligned or oriented box | Center + half-extents + rotation | Rooms, corridors, zones |
| **Sphere** | Spherical | Center + radius | Campfires, light sources, proximity effects |
| **Unbound** | Infinite (like global) | No bounds, but with priority > 0 | Weather overrides, time-of-day presets |

### Blending System

When the camera is inside multiple volumes, the engine computes a weighted blend of all active volume settings.

**Blend weight calculation for a local volume:**

```
distance = signed_distance(camera_pos, volume_bounds)

if distance <= 0:
    weight = 1.0                             # fully inside
elif distance < blend_radius:
    t = distance / blend_radius              # 0 at boundary, 1 at blend edge
    weight = evaluate_falloff_curve(1.0 - t) # artist-tuned curve
else:
    weight = 0.0                             # fully outside
```

**Final blend (priority-ordered, top-down):**

```
result = global_volume.settings  (base)

for volume in active_volumes sorted by priority ascending:
    w = volume.blend_weight
    for each overridden setting in volume:
        result[setting] = lerp(result[setting], volume[setting], w)
```

Higher-priority volumes override lower-priority ones. When two volumes have the same priority, the one with the smaller volume (by bounding area) wins (more specific = higher precedence).

### Falloff Curves

The blend falloff curve is a 1D cubic bezier with two control points. The editor provides presets and custom editing.

| Preset | Behavior | Control Points |
|--------|----------|----------------|
| Linear | Even transition | `(0.0, 0.0) -> (1.0, 1.0)` |
| Ease-In | Slow start, fast finish | `(0.0, 0.0), (0.42, 0.0), (1.0, 1.0), (1.0, 1.0)` |
| Ease-Out | Fast start, slow finish | `(0.0, 0.0), (0.0, 0.0), (0.58, 1.0), (1.0, 1.0)` |
| Ease-In-Out | Smooth S-curve (default) | `(0.0, 0.0), (0.42, 0.0), (0.58, 1.0), (1.0, 1.0)` |
| Step | Hard cut (no blend) | `(0.0, 0.0), (0.0, 1.0), (0.0, 1.0), (1.0, 1.0)` |
| Custom | Artist draws curve | 4 free-form control points |

### Per-Camera Override

A camera node can reference a `.uzppv` file that overrides all volume blending for that camera. This is used during cinematics and scripted sequences.

```lua
-- Lua: push a per-camera PPV override during a cutscene
ctx.postprocess:push_override("cinematic_cam_01", "res://volumes/cutscene_dramatic.uzppv", {
    blend_time = 0.5,  -- seconds to blend in
    priority = 1000    -- overrides everything
})

-- Pop when done
ctx.postprocess:pop_override("cinematic_cam_01", {
    blend_time = 1.0   -- fade back to normal
})
```

### Lua API

```lua
-- Volume management
ctx.postprocess:push_volume("res://volumes/combat.uzppv", {
    blend_time = 0.3,
    priority = 50
})
ctx.postprocess:pop_volume("res://volumes/combat.uzppv", { blend_time = 0.5 })

-- Per-camera override
ctx.postprocess:push_override(camera_id, ppv_path, { blend_time, priority })
ctx.postprocess:pop_override(camera_id, { blend_time })

-- Runtime parameter tweaks (override a single setting without a full PPV)
ctx.postprocess:set_param("bloom.intensity", 2.0, { blend_time = 0.2 })
ctx.postprocess:reset_param("bloom.intensity", { blend_time = 0.5 })

-- Query current blended state
local bloom_intensity = ctx.postprocess:get_param("bloom.intensity")
local active_volumes = ctx.postprocess:get_active_volumes()

-- Lock/unlock exposure (for cinematics)
ctx.postprocess:lock_exposure()
ctx.postprocess:unlock_exposure({ blend_time = 1.0 })
```

---

## 3. Editor Layout

The Post-Process Editor occupies the full VSCode panel as a single WebView.

### ASCII Mockup -- Full Editor

```
+=============================================================================+
|  [File v] [Edit v] [Volume v]  | Vol: "dungeon_entrance.uzppv" | [Save]    |
+=============================================================================+
|                              |                                              |
|     3D VIEWPORT (55%)        |     VOLUME PROPERTIES (45%, right-top)       |
|                              |                                              |
|  +------------------------+  |  Shape:    [Box v]   Unbound: [ ]            |
|  |                        |  |  Position: [12.0] [0.0] [8.5]               |
|  |  +---------+           |  |  Extents:  [10.0] [5.0] [6.0]              |
|  |  |  volume |           |  |  Rotation: [0]    [45]  [0]                 |
|  |  |  bounds |           |  |  Blend Radius: [==5.0m====]                 |
|  |  | (wire-  |           |  |  Priority: [10  ]                           |
|  |  |  frame) |           |  |  Enabled:  [x]                              |
|  |  |         |  cam -->X |  |                                              |
|  |  +---------+           |  |  Falloff:  [Ease-In-Out v]                  |
|  |                        |  |  +-----+  <-- curve editor miniature        |
|  |                        |  |  |  _/ |                                     |
|  +------------------------+  |  | /   |                                     |
|                              |  +-----+                                     |
|  [Wireframe] [Solid] [Heat]  |                                              |
|                              +----------------------------------------------+
+------------------------------+                                              |
|                              |  POST-PROCESS SETTINGS STACK (scrollable)    |
|  VOLUME LIST (left, 220px)   |                                              |
|                              |  [v] COLOR GRADING --------- [x] Override    |
|  > * Global          p:0     |  | Temperature: [==5500K====] Tint: [0]     |
|    * dungeon_entrance p:10   |  | Saturation:  [====0.7====]               |
|    * boss_arena       p:20   |  | Contrast:    [====1.0====]               |
|    * underwater       p:15   |  | Gamma:       [====1.0====]               |
|                              |  | Shadows:  ( ) Midtones: ( ) Highlights:( )|
|  [+ Add Volume]              |  | LUT: [---none---] [Import]               |
|  [  Delete   ]               |  +-------------------------------------------+
|                              |  [>] EXPOSURE --------------- [ ] Override   |
|                              |  [v] BLOOM ------------------ [x] Override   |
|                              |  | Method: [Gaussian v]                      |
|                              |  | Intensity: [====0.5====] Threshold: [1.0] |
|                              |  | Scatter:   [====0.7====]                  |
|                              |  | Dirt Mask: [---none---] [Browse]          |
|                              |  +-------------------------------------------+
|                              |  [>] DEPTH OF FIELD --------- [ ] Override   |
|                              |  [>] AMBIENT OCCLUSION ------ [ ] Override   |
|                              |  [>] SCREEN SPACE REFLECTIONS [ ] Override   |
|                              |  [>] TONE MAPPING ----------- [x] Override   |
|                              |  | Operator: [ACES Filmic v]                |
|                              |  | HDR Mode: [SDR 8-bit v]                  |
|                              |  +-------------------------------------------+
|                              |  [>] FILM GRAIN ------------- [ ] Override   |
|                              |  [>] VIGNETTE --------------- [x] Override   |
|                              |  | Intensity: [====0.6====]                  |
|                              |  | Roundness: [====0.5====]                  |
|                              |  +-------------------------------------------+
|                              |  [>] CHROMATIC ABERRATION --- [ ] Override   |
|                              |  [>] MOTION BLUR ------------ [ ] Override   |
|                              |  [>] FOG OVERRIDE ----------- [ ] Override   |
|                              |  [>] CUSTOM PP MATERIALS ---- [ ] Override   |
+------------------------------+----------------------------------------------+
|  [Before/After: OFF]  | Histogram: [OFF]  | Focus Debug: [OFF]  | FPS: 60  |
+=============================================================================+
```

### Panel Descriptions

#### 3D Viewport (top-left, 55% width)

Streams the live DX12 render with this volume's effects applied. Shows:

- **Volume wireframes**: box or sphere outlines for all volumes in the scene, color-coded by priority (green = active, gray = inactive, yellow = blending)
- **Blend radius visualization**: translucent shell around volume bounds showing the blend region
- **Camera frustum**: wireframe showing the active camera's view cone
- **Heatmap mode**: colors the scene by volume influence (red = high weight, blue = low weight)

View modes:

| Mode | Description |
|------|-------------|
| **Wireframe** | Volume bounds only, transparent scene |
| **Solid** | Full lit render with PP effects applied |
| **Heatmap** | Volume blend weight visualization (debug) |
| **Split** | Before/after comparison (vertical or horizontal split) |

#### Volume List (bottom-left, 220px)

Displays all PPV volumes in the scene, sorted by priority. Each row shows:

- Active star indicator (`*` = currently affecting camera)
- Volume name
- Priority number
- Enable/disable checkbox

Click to select and edit. The global volume is always first and cannot be deleted.

#### Volume Properties Panel (top-right)

Editable fields for the selected volume's spatial properties:

| Property | Widget | Description |
|----------|--------|-------------|
| Shape | Dropdown: Box / Sphere | Volume trigger shape |
| Position | float3 input | World-space center |
| Extents | float3 input (box) or float (sphere radius) | Volume size |
| Rotation | float3 input (Euler degrees) | Box orientation |
| Blend Radius | Slider (0 - 50m) | Soft boundary distance in meters |
| Priority | Integer input (0 - 9999) | Higher = wins overlap conflicts |
| Unbound | Checkbox | Infinite extent (ignores shape/position) |
| Enabled | Checkbox | Toggle volume on/off |
| Falloff | Dropdown + mini curve editor | Blend falloff shape |

#### Post-Process Settings Stack (center-right, scrollable)

The core of the editor. Collapsible sections grouped by effect category. Each section header has:

- **Collapse/expand arrow** `[v]` / `[>]`
- **Section name** (e.g., "COLOR GRADING")
- **Override checkbox** `[x]` -- when unchecked, this volume does not override this category (falls through to lower priority)

When a section is expanded, individual settings are shown. Each setting has:

- Parameter label
- Appropriate widget (slider, dropdown, color picker, texture picker)
- Value readout
- Reset-to-default button (right-click context menu)

Settings that differ from the global default are shown with a **blue override dot** next to the label (matching the Material Editor convention).

#### Status Bar (bottom)

| Element | Description |
|---------|-------------|
| Before/After toggle | Split-screen comparison |
| Histogram | Show/hide luminance histogram overlay |
| Focus Debug | Show/hide depth-of-field focus plane visualization |
| FPS counter | Current render frame rate |

---

## 4. Color Grading

Color grading is the most artist-facing section of the post-process stack. It operates in HDR space before tone mapping.

### White Balance

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `temperature` | float | `6500` | 1500 - 15000 | Color temperature in Kelvin. Lower = warm (orange), higher = cool (blue) |
| `tint` | float | `0.0` | -1.0 - 1.0 | Green-magenta shift. Negative = green, positive = magenta |

### Global Adjustments

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `saturation` | float | `1.0` | 0.0 - 2.0 | Color intensity. 0 = grayscale, 1 = unchanged, 2 = oversaturated |
| `contrast` | float | `1.0` | 0.0 - 2.0 | Midtone contrast. 1 = unchanged |
| `gamma` | float | `1.0` | 0.2 - 5.0 | Display gamma adjustment |
| `gain` | float | `1.0` | 0.0 - 4.0 | HDR brightness multiplier |
| `offset` | float | `0.0` | -1.0 - 1.0 | Brightness offset (added post-multiply) |

### Shadows / Midtones / Highlights (Lift / Gamma / Gain)

Three color wheels that operate on different luminance ranges:

```
+--SHADOWS (Lift)---+  +--MIDTONES (Gamma)-+  +--HIGHLIGHTS (Gain)+
|                   |  |                   |  |                   |
|     ___           |  |     ___           |  |     ___           |
|    /   \          |  |    /   \          |  |    /   \          |
|   |  .  |  <drag> |  |   |  .  |  <drag> |  |   |  .  |  <drag> |
|    \___/          |  |    \___/          |  |    \___/          |
|                   |  |                   |  |                   |
|  H:[0] S:[0]      |  |  H:[0] S:[0]      |  |  H:[0] S:[0]      |
|  Offset: [0.0]    |  |  Gamma:  [1.0]    |  |  Gain:   [1.0]    |
+-------------------+  +-------------------+  +-------------------+
```

Each wheel controls:

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `shadows_hue` | float | `0.0` | 0.0 - 360.0 | Hue shift in shadow range |
| `shadows_saturation` | float | `0.0` | 0.0 - 1.0 | Color intensity in shadow range |
| `shadows_offset` | float | `0.0` | -1.0 - 1.0 | Brightness lift in shadow range |
| `midtones_hue` | float | `0.0` | 0.0 - 360.0 | Hue shift in midtone range |
| `midtones_saturation` | float | `0.0` | 0.0 - 1.0 | Color intensity in midtone range |
| `midtones_gamma` | float | `1.0` | 0.2 - 5.0 | Gamma in midtone range |
| `highlights_hue` | float | `0.0` | 0.0 - 360.0 | Hue shift in highlight range |
| `highlights_saturation` | float | `0.0` | 0.0 - 1.0 | Color intensity in highlight range |
| `highlights_gain` | float | `1.0` | 0.0 - 4.0 | Brightness gain in highlight range |

### Per-Channel Curves

Four bezier curve editors (R, G, B, Luminance) that remap input intensity to output intensity:

```
  OUTPUT                          OUTPUT
  1.0|     ___...               1.0|  ___...
     |   ./                        | /
     |  /                          |/
     | /                           |
  0.0|/__________ INPUT         0.0+__________ INPUT
     0.0      1.0                  0.0      1.0

     [R] [G] [B] [L]  << tab selector
```

Each curve is defined by N control points (minimum 2: start and end). The default is identity (straight diagonal line). The curve data is stored as an array of `(x, y)` pairs:

```ini
color_curve_r = (0.0, 0.0), (0.25, 0.2), (0.75, 0.85), (1.0, 1.0)
color_curve_g = (0.0, 0.0), (1.0, 1.0)
color_curve_b = (0.0, 0.0), (1.0, 1.0)
color_curve_l = (0.0, 0.0), (1.0, 1.0)
```

Curves are baked to a 256-entry 1D LUT at edit time and uploaded as an `R8_UNORM` texture per channel.

### LUT (Look-Up Table) Support

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `lut_texture` | path | `""` | Path to a 3D LUT texture (`.png` strip or `.cube` file) |
| `lut_size` | enum | `16` | LUT grid resolution: 16 or 32 |
| `lut_intensity` | float | `1.0` | Blend factor: 0 = no LUT, 1 = full LUT |

LUT format: a horizontal strip `.png` where a 16x16x16 LUT is encoded as 16 tiles of 16x16 pixels (total 256x16). The engine loads this into a `Texture3D` (`RGBA8_SRGB`, 16x16x16 or 32x32x32).

Importing a `.cube` file (Adobe/DaVinci format) is supported -- the engine converts it to the internal strip format on import.

### HDR Calibration

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `hdr_min_nits` | float | `0.0` | 0.0 - 5.0 | Minimum display luminance (black level) |
| `hdr_max_nits` | float | `1000.0` | 100.0 - 10000.0 | Peak display luminance |
| `hdr_paper_white` | float | `200.0` | 80.0 - 500.0 | Reference white level (nits) |

These values are only used when the HDR output mode is set to HDR10 or scRGB (see Tone Mapping section).

### HLSL: Color Grading Pass

```hlsl
// shaders/post/color_grading.hlsl
//
// Applies white balance, lift/gamma/gain, saturation, contrast,
// per-channel curves, and 3D LUT to the HDR color buffer.
// Runs BEFORE tone mapping (operates in linear HDR space).

#include "common.hlsli"
#include "global_cbuffer.hlsli"

cbuffer ColorGradingParams : register(b2) {
    float  cg_temperature;        // Kelvin offset from 6500K (e.g., -1000 = warm)
    float  cg_tint;               // green-magenta shift [-1, 1]
    float  cg_saturation;         // global saturation multiplier
    float  cg_contrast;           // midtone contrast multiplier

    float  cg_gamma;              // display gamma
    float  cg_gain;               // HDR brightness multiplier
    float  cg_offset;             // brightness offset
    float  cg_lut_intensity;      // LUT blend factor [0, 1]

    // Lift (shadows)
    float3 cg_shadows_color;      // hue/sat encoded as RGB tint
    float  cg_shadows_offset;     // brightness lift

    // Gamma (midtones)
    float3 cg_midtones_color;     // hue/sat encoded as RGB tint
    float  cg_midtones_gamma;     // gamma power

    // Gain (highlights)
    float3 cg_highlights_color;   // hue/sat encoded as RGB tint
    float  cg_highlights_gain;    // brightness gain

    float2 cg_screen_size;
    float2 cg_texel_size;
};

Texture2D<float4> g_hdr_input     : register(t0);  // HDR scene color
Texture2D<float>  g_curve_r       : register(t1);  // 256x1 R curve LUT
Texture2D<float>  g_curve_g       : register(t2);  // 256x1 G curve LUT
Texture2D<float>  g_curve_b       : register(t3);  // 256x1 B curve LUT
Texture2D<float>  g_curve_l       : register(t4);  // 256x1 Luminance curve LUT
Texture3D<float4> g_lut           : register(t5);  // 3D color LUT (16^3 or 32^3)

SamplerState sampler_linear_clamp : register(s0);

RWTexture2D<float4> g_output : register(u0);

// Compute white balance multiplier from temperature offset
// Based on Planckian locus approximation (CIE 1931)
float3 white_balance_multiplier(float temp_kelvin, float tint) {
    // Approximate daylight illuminant chromaticity shift
    // temp_kelvin: target temperature (e.g., 5500 = warm, 7500 = cool)
    float t = temp_kelvin;
    float t2 = t * t;

    // CIE xy chromaticity approximation for daylight
    float x = (t <= 7000.0)
        ? -4.6070e9 / (t2 * t) + 2.9678e6 / t2 + 0.09911e3 / t + 0.244063
        : -2.0064e9 / (t2 * t) + 1.9018e6 / t2 + 0.24748e3 / t + 0.237040;

    float y = -3.0 * x * x + 2.87 * x - 0.275;

    // Apply tint (green-magenta shift along orthogonal axis)
    y += tint * 0.05;

    // Convert to linear RGB multiplier (relative to D65)
    float X = x / y;
    float Z = (1.0 - x - y) / y;

    // XYZ to linear sRGB (D65 reference white)
    float3 rgb;
    rgb.r =  3.2404542 * X - 1.5371385 - 0.4985314 * Z;
    rgb.g = -0.9692660 * X + 1.8760108 + 0.0415560 * Z;
    rgb.b =  0.0556434 * X - 0.2040259 + 1.0572252 * Z;

    return rgb / max(rgb.r, max(rgb.g, rgb.b));  // normalize
}

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float3 color = g_hdr_input[tid].rgb;

    // 1. White balance
    float3 wb = white_balance_multiplier(cg_temperature, cg_tint);
    color *= wb;

    // 2. Lift / Gamma / Gain (shadows / midtones / highlights)
    // Luminance-based weight splitting
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    float shadow_weight    = 1.0 - smoothstep(0.0, 0.33, lum);
    float highlight_weight = smoothstep(0.55, 1.0, lum);
    float midtone_weight   = 1.0 - shadow_weight - highlight_weight;

    // Apply shadow lift
    color += cg_shadows_color * shadow_weight * cg_shadows_offset;

    // Apply midtone gamma
    float3 midtone_tint = lerp(float3(1, 1, 1), cg_midtones_color, midtone_weight);
    color = pow(max(color, EPSILON), 1.0 / cg_midtones_gamma) * midtone_tint;

    // Apply highlight gain
    float3 highlight_tint = lerp(float3(1, 1, 1), cg_highlights_color, highlight_weight);
    color *= cg_highlights_gain * highlight_tint;

    // 3. Global adjustments
    color *= cg_gain;
    color += cg_offset;

    // Contrast (around midpoint 0.18 in linear, expanded to 0.5 for perceptual)
    float3 contrast_midpoint = float3(0.18, 0.18, 0.18);
    color = contrast_midpoint + (color - contrast_midpoint) * cg_contrast;

    // Saturation
    float gray = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(gray, gray, gray), color, cg_saturation);

    // Gamma
    color = pow(max(color, EPSILON), 1.0 / cg_gamma);

    // 4. Per-channel curves (sample from baked 1D LUTs)
    // Tonemap to [0,1] range temporarily for curve lookup
    float3 mapped = saturate(color / (color + 1.0));  // Reinhard for curve space
    mapped.r = g_curve_r.SampleLevel(sampler_linear_clamp, float2(mapped.r, 0.5), 0);
    mapped.g = g_curve_g.SampleLevel(sampler_linear_clamp, float2(mapped.g, 0.5), 0);
    mapped.b = g_curve_b.SampleLevel(sampler_linear_clamp, float2(mapped.b, 0.5), 0);

    // Luminance curve
    float lum_mapped = dot(mapped, float3(0.2126, 0.7152, 0.0722));
    float lum_curved = g_curve_l.SampleLevel(sampler_linear_clamp, float2(lum_mapped, 0.5), 0);
    mapped *= (lum_curved / max(lum_mapped, EPSILON));

    // Inverse Reinhard to return to HDR
    color = mapped / max(1.0 - mapped, EPSILON);

    // 5. 3D LUT application
    if (cg_lut_intensity > 0.0) {
        // LUT operates in [0,1] range (post-tonemap preview)
        float3 lut_uv = saturate(color / (color + 1.0));  // Reinhard for LUT space
        // Scale and offset for proper 3D texture lookup (half-texel offset)
        float lut_size = 16.0;  // or 32.0
        lut_uv = lut_uv * ((lut_size - 1.0) / lut_size) + 0.5 / lut_size;
        float3 lut_color = g_lut.SampleLevel(sampler_linear_clamp, lut_uv, 0).rgb;
        // Inverse Reinhard
        lut_color = lut_color / max(1.0 - lut_color, EPSILON);
        color = lerp(color, lut_color, cg_lut_intensity);
    }

    g_output[tid] = float4(max(color, 0.0), 1.0);
}
```

---

## 5. Exposure (Auto + Manual)

### Mode Selection

| Mode | Description |
|------|-------------|
| **Manual** | Fixed EV100 value set by the artist. No adaptation. |
| **Automatic** | Eye adaptation: exposure adjusts over time based on scene luminance. |

### Manual Exposure Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `exposure_mode` | enum | `Auto` | Manual / Auto | Exposure control mode |
| `ev100` | float | `9.0` | -4.0 - 14.0 | Exposure value at ISO 100. Outdoor sun ~14, indoor ~7, night ~0 |
| `compensation` | float | `0.0` | -5.0 - 5.0 | EV bias added on top of auto or manual value |

The exposure multiplier is: `exposure = 1.0 / (1.2 * pow(2.0, ev100))`

### Automatic Exposure Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `auto_min_ev100` | float | `2.0` | -4.0 - 14.0 | Minimum auto-exposure (darkest scene allowed) |
| `auto_max_ev100` | float | `14.0` | -4.0 - 14.0 | Maximum auto-exposure (brightest scene allowed) |
| `adapt_speed_up` | float | `3.0` | 0.1 - 20.0 | Speed of adaptation when scene gets brighter (EV/sec) |
| `adapt_speed_down` | float | `1.0` | 0.1 - 20.0 | Speed of adaptation when scene gets darker (EV/sec) |
| `metering_mode` | enum | `CenterWeighted` | Average / Spot / CenterWeighted / Histogram | How scene luminance is measured |
| `lock_exposure` | bool | `false` | - | Freeze auto-exposure at current value (cinematic use) |

### Metering Modes

| Mode | Description | Weight Distribution |
|------|-------------|---------------------|
| **Average** | Mean luminance of entire frame | Uniform |
| **Spot** | Small center region (5% of frame) | Gaussian, sigma = 2.5% screen |
| **CenterWeighted** | Full frame but center biased | Gaussian, sigma = 25% screen |
| **Histogram** | Log-space histogram, ignore top/bottom 5% | Percentile-based (robust to outliers) |

### Histogram Overlay

When enabled, the editor overlays a real-time luminance histogram on the viewport:

```
  +---LUMINANCE HISTOGRAM (overlay, bottom-left)---+
  |                                                 |
  |  100%|                                          |
  |      |    __                                    |
  |      |   /  \       _                           |
  |      |  /    \     / \                          |
  |      | /      \___/   \                         |
  |      |/                \____                    |
  |    0%+------|------|------|-----> EV             |
  |      -4     0      7     14                     |
  |                                                 |
  |  Current EV: 8.3  [LOCK]                        |
  +--min--[=====|==========]--max-------------------+
  |       2.0   ^          14.0                     |
  |          auto range                             |
  +-------------------------------------------------+
```

The histogram is computed via a compute shader that bins pixel luminance values into 64 buckets in log space (EV -4 to EV +14). The result is read back for display.

### HLSL: Luminance Histogram

```hlsl
// shaders/post/luminance_histogram.hlsl
//
// Compute shader that builds a log-space luminance histogram.
// Used by auto-exposure to determine target EV100.
// Output: 64-bin histogram in a structured buffer.

#include "common.hlsli"

cbuffer ExposureParams : register(b2) {
    float  exp_min_ev;       // minimum EV100 for histogram range
    float  exp_max_ev;       // maximum EV100 for histogram range
    float2 exp_screen_size;  // viewport dimensions
};

Texture2D<float4>          g_hdr_input  : register(t0);
RWStructuredBuffer<uint>   g_histogram  : register(u0);  // 64 bins

#define NUM_BINS 64
#define EPSILON_LUM 0.0001

groupshared uint shared_bins[NUM_BINS];

[numthreads(16, 16, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID, uint gidx : SV_GroupIndex) {
    // Clear shared bins
    if (gidx < NUM_BINS) {
        shared_bins[gidx] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    // Each thread processes one pixel
    if (all(tid < uint2(exp_screen_size))) {
        float3 color = g_hdr_input[tid].rgb;
        float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));

        if (luminance > EPSILON_LUM) {
            // Convert luminance to EV100: EV = log2(luminance * 100 / 12.5)
            float ev = log2(luminance * 100.0 / 12.5);

            // Map EV to bin index [0, NUM_BINS-1]
            float t = saturate((ev - exp_min_ev) / (exp_max_ev - exp_min_ev));
            uint bin = min(uint(t * float(NUM_BINS - 1)), NUM_BINS - 1);

            InterlockedAdd(shared_bins[bin], 1);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Merge shared bins into global buffer
    if (gidx < NUM_BINS) {
        InterlockedAdd(g_histogram[gidx], shared_bins[gidx]);
    }
}
```

---

## 6. Bloom

Bloom simulates light bleeding from bright regions of the image into surrounding areas, mimicking real camera lens behavior.

### Method Selection

| Method | Description | Cost (1080p) |
|--------|-------------|--------------|
| **Gaussian** | 6-pass progressive downsample + upsample with Gaussian kernels. Fast, standard look. | ~0.5ms |
| **Convolution** | FFT-based convolution with a custom kernel texture. Cinematic, supports shaped bokeh and lens flares. | ~1.5ms |

### Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `bloom_enabled` | bool | `true` | - | Toggle bloom |
| `bloom_method` | enum | `Gaussian` | Gaussian / Convolution | Algorithm selection |
| `bloom_intensity` | float | `0.5` | 0.0 - 5.0 | Overall bloom brightness |
| `bloom_threshold` | float | `1.0` | 0.0 - 10.0 | Luminance threshold for bloom contribution (HDR values above this glow) |
| `bloom_scatter` | float | `0.7` | 0.0 - 1.0 | Controls how much bloom spreads. 0 = tight glow, 1 = wide haze |

### Gaussian Bloom Kernels

6 progressive downsample levels, each with configurable width and tint:

| Kernel | Resolution | Default Width | Default Tint | Contribution |
|--------|------------|---------------|--------------|--------------|
| K0 | 1/2 | 1.0 | `[1.0, 1.0, 1.0]` | Fine glow |
| K1 | 1/4 | 1.0 | `[1.0, 1.0, 1.0]` | Small halo |
| K2 | 1/8 | 1.0 | `[1.0, 1.0, 1.0]` | Medium halo |
| K3 | 1/16 | 1.0 | `[1.0, 1.0, 1.0]` | Large halo |
| K4 | 1/32 | 1.0 | `[1.0, 1.0, 1.0]` | Very large |
| K5 | 1/64 | 1.0 | `[1.0, 0.95, 0.9]` | Atmospheric haze (warm tint default) |

Each kernel's parameters:

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `bloom_kernel_N_width` | float | `1.0` | 0.1 - 4.0 | Gaussian blur radius multiplier for kernel N |
| `bloom_kernel_N_tint` | float3 | `[1,1,1]` | 0.0 - 2.0 per channel | Color tint for this kernel level |

### Convolution Bloom

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `bloom_kernel_texture` | path | `""` | Custom convolution kernel image (`.png`, HDR recommended) |

The kernel texture is a small image (64x64 to 256x256) that defines the bloom shape. A star filter would use a cross pattern; a cinematic lens flare uses radial streaks.

### Dirt Mask

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `bloom_dirt_texture` | path | `""` | Lens dirt overlay texture (`res://textures/lens_dirt.png`) |
| `bloom_dirt_intensity` | float | `0.0` | Dirt mask multiplier. 0 = no dirt, 1 = full dirt |

The dirt texture is multiplied onto the bloom result before compositing, simulating smudges and particles on a camera lens.

---

## 7. Depth of Field

Depth of Field simulates camera lens focus, blurring objects that are too close or too far from the focal plane.

### Method Selection

| Method | Description | Cost (1080p) |
|--------|-------------|--------------|
| **Gaussian** | Simple separable blur weighted by Circle of Confusion. Fast, good for gameplay. | ~0.5ms |
| **Bokeh** | Physically-based scatter, models lens aperture shape. Cinematic quality. | ~1.5ms |

### Common Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `dof_enabled` | bool | `false` | - | Toggle DoF |
| `dof_method` | enum | `Gaussian` | Gaussian / Bokeh | Algorithm selection |
| `dof_focal_distance` | float | `10.0` | 0.1 - 1000.0 | Distance to focus plane in meters |
| `dof_focal_region` | float | `2.0` | 0.0 - 100.0 | Width of the in-focus region (meters, both sides of focal plane) |
| `dof_near_transition` | float | `3.0` | 0.1 - 50.0 | Near blur transition range in meters |
| `dof_far_transition` | float | `10.0` | 0.1 - 200.0 | Far blur transition range in meters |

### Bokeh-Specific Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `dof_f_stop` | float | `2.8` | 1.0 - 22.0 | Aperture f-number. Lower = more blur, shallower DoF |
| `dof_sensor_size` | float | `36.0` | 8.8 - 70.0 | Sensor width in mm (36mm = full-frame) |
| `dof_max_coc` | float | `16.0` | 4.0 - 64.0 | Maximum Circle of Confusion radius in pixels |
| `dof_blade_count` | uint | `6` | 4 - 10 | Aperture blade count (affects bokeh shape) |
| `dof_blade_rotation` | float | `0.0` | 0.0 - 360.0 | Aperture blade rotation angle (degrees) |

### Focus Tracking

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `dof_focus_target` | entity_id | `0` | Lock focus on a scene entity (0 = manual distance) |

When `dof_focus_target` is set, the engine traces the distance from the camera to the target entity each frame and updates `dof_focal_distance` automatically. This is essential for cinematic sequences tracking a character.

### Debug Visualization

When "Focus Debug" is enabled in the status bar, the viewport overlays a color-coded depth visualization:

```
  +---DOF DEBUG OVERLAY-----------------------------------+
  |                                                       |
  |   NEAR BLUR          IN FOCUS         FAR BLUR        |
  |   (red tint)         (green tint)     (blue tint)     |
  |                                                       |
  |  |<-- near -->|<-- focal -->|<-- far -->|             |
  |  |  transition|   region    | transition|             |
  |  0m     7m    10m    12m    14m    24m   ...          |
  |               ^                                       |
  |          focal distance                               |
  |                                                       |
  |  CoC radius: |====5.2px===|  (at cursor position)     |
  +----------------------------------------------------- -+
```

---

## 8. Ambient Occlusion (HBAO+)

This section configures the SSAO pass defined in [screen-space-effects.md](../specs/screen-space-effects.md). The PPV editor exposes its parameters as overridable volume settings.

### Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `ssao_enabled` | bool | `true` | - | Toggle SSAO |
| `ssao_intensity` | float | `1.0` | 0.0 - 3.0 | Occlusion darkness multiplier |
| `ssao_radius` | float | `0.5` | 0.1 - 5.0 | World-space sampling radius (meters) |
| `ssao_power` | float | `2.0` | 0.5 - 8.0 | Occlusion power curve (higher = darker AO, sharper falloff) |
| `ssao_bias` | float | `0.025` | 0.0 - 0.1 | Angle bias to prevent self-occlusion |
| `ssao_quality` | enum | `Medium` | Low / Medium / High | Sample count: 4/8/16 directions |
| `ssao_half_res` | bool | `true` | - | Compute at half resolution (faster, slight quality loss) |

### GTAO Mode (Ground Truth AO)

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `ssao_gtao_enabled` | bool | `false` | - | Use GTAO instead of HBAO+ (higher quality, ~1.5x cost) |
| `ssao_gtao_slices` | uint | `3` | 1 - 8 | Number of horizon slices per direction |

### Temporal Accumulation

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `ssao_temporal` | bool | `true` | - | Blend with previous frame for stability |
| `ssao_temporal_blend` | float | `0.9` | 0.5 - 0.98 | History weight (higher = more stable, slower response) |

---

## 9. Screen Space Reflections (Hi-Z SSR)

This section configures the SSR pass defined in [screen-space-effects.md](../specs/screen-space-effects.md).

### Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `ssr_enabled` | bool | `true` | - | Toggle SSR |
| `ssr_intensity` | float | `1.0` | 0.0 - 2.0 | Reflection brightness multiplier |
| `ssr_quality` | enum | `Medium` | Low / Medium / High | Ray count: 32/64/128 steps |
| `ssr_max_roughness` | float | `0.4` | 0.0 - 1.0 | Reflections fade out above this roughness |
| `ssr_temporal` | bool | `true` | - | Enable temporal reprojection for stability |
| `ssr_temporal_blend` | float | `0.8` | 0.0 - 0.95 | History blend factor |

### Fallback Configuration

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `ssr_fallback_mode` | enum | `IBL` | IBL / ReflectionCapture / None |
| `ssr_fallback_intensity` | float | `1.0` | Fallback reflection brightness when SSR misses |

When SSR fails to find a hit (off-screen geometry, too rough), the engine falls back to pre-computed reflection sources: IBL cubemaps or placed reflection captures. This parameter controls which fallback is used and its strength.

---

## 10. Tone Mapping

Tone mapping compresses the HDR scene luminance range into the display's LDR range.

### Operator Selection

| Operator | Description | Character |
|----------|-------------|-----------|
| **ACES Filmic** | Academy Color Encoding System filmic curve. Industry standard, pleasing contrast. (default) | Rich, cinematic |
| **Reinhard** | Simple `color / (color + 1)`. Soft highlights, low contrast. | Soft, vintage |
| **Uncharted 2** | Hejl-Burgess filmic with adjustable shoulder and toe. | Punchy, game-like |
| **Custom LUT** | Use the 3D LUT from Color Grading as the entire tone curve. | Artist-defined |
| **None (Linear)** | No tone mapping. Clamp to [0,1]. For HDR displays only. | Raw, flat |

### Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `tonemap_operator` | enum | `ACES` | ACES / Reinhard / Uncharted2 / CustomLUT / None | Algorithm |
| `tonemap_shoulder` | float | `0.22` | 0.0 - 1.0 | Highlight compression (Uncharted2 only) |
| `tonemap_toe` | float | `0.3` | 0.0 - 1.0 | Shadow lift (Uncharted2 only) |
| `tonemap_slope` | float | `1.0` | 0.5 - 2.0 | Mid-section slope (Uncharted2 only) |

### Gamma Correction

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `gamma_mode` | enum | `sRGB` | sRGB / Gamma2.2 / Custom | Output gamma curve |
| `gamma_custom` | float | `2.2` | 1.0 - 3.0 | Custom gamma value (used when mode = Custom) |

### HDR Output Mode

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `hdr_output_mode` | enum | `SDR` | SDR (8-bit sRGB) / HDR10 (10-bit PQ) / HDR16 (scRGB float) |

When HDR10 is selected, the tone mapper outputs to the PQ (Perceptual Quantizer, ST.2084) transfer function instead of sRGB. The HDR calibration parameters from Color Grading (min/max nits, paper white) are used to map scene luminance to the display's dynamic range.

### HLSL: Tone Mapping Pass

```hlsl
// shaders/post/tonemap.hlsl
//
// Applies tone mapping operator + gamma correction.
// Runs AFTER color grading, bloom, SSAO/SSR composite.
// Input: HDR color (RGBA16F or R11G11B10F)
// Output: LDR color (RGBA8_UNORM for SDR, RGB10A2 for HDR10)

#include "common.hlsli"

cbuffer TonemapParams : register(b2) {
    uint   tm_operator;        // 0=ACES, 1=Reinhard, 2=Uncharted2, 3=CustomLUT, 4=None
    float  tm_exposure;        // pre-tonemap exposure multiplier (from EV100 calculation)
    float  tm_shoulder;        // Uncharted2 shoulder
    float  tm_toe;             // Uncharted2 toe

    float  tm_slope;           // Uncharted2 slope
    uint   tm_gamma_mode;      // 0=sRGB, 1=Gamma2.2, 2=Custom
    float  tm_gamma_custom;    // custom gamma value
    float  tm_pad;

    float2 tm_screen_size;
    float2 tm_texel_size;
};

Texture2D<float4> g_hdr_input : register(t0);
Texture3D<float4> g_lut       : register(t1);  // 3D LUT for CustomLUT mode

SamplerState sampler_linear_clamp : register(s0);

RWTexture2D<float4> g_output : register(u0);

// ACES Filmic (Narkowicz 2015 approximation)
float3 aces_filmic(float3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Reinhard extended
float3 reinhard(float3 x) {
    return x / (x + 1.0);
}

// Uncharted 2 (Hejl-Burgess)
float3 uncharted2_partial(float3 x, float shoulder, float toe, float slope) {
    float A = shoulder;  // shoulder strength
    float B = toe;       // toe strength
    float C = slope;     // linear section slope
    float D = 0.20;      // linear section offset
    float E = 0.02;      // toe numerator
    float F = 0.30;      // toe denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 uncharted2(float3 color, float shoulder, float toe, float slope) {
    float  W = 11.2;  // linear white point
    float3 curr = uncharted2_partial(color, shoulder, toe, slope);
    float3 white_scale = 1.0 / uncharted2_partial(float3(W, W, W), shoulder, toe, slope);
    return curr * white_scale;
}

// sRGB gamma (IEC 61966-2-1)
float3 linear_to_srgb(float3 c) {
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(max(c, EPSILON), 1.0 / 2.4) - 0.055;
    return (c <= 0.0031308) ? lo : hi;
}

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float3 color = g_hdr_input[tid].rgb;

    // Apply exposure
    color *= tm_exposure;

    // Tone mapping operator
    switch (tm_operator) {
        case 0: color = aces_filmic(color); break;
        case 1: color = reinhard(color); break;
        case 2: color = uncharted2(color, tm_shoulder, tm_toe, tm_slope); break;
        case 3: {
            // CustomLUT: use 3D LUT as tone curve
            float3 lut_uv = saturate(color);
            float lut_size = 16.0;
            lut_uv = lut_uv * ((lut_size - 1.0) / lut_size) + 0.5 / lut_size;
            color = g_lut.SampleLevel(sampler_linear_clamp, lut_uv, 0).rgb;
            break;
        }
        case 4: color = saturate(color); break;  // None: hard clamp
    }

    // Gamma correction
    switch (tm_gamma_mode) {
        case 0: color = linear_to_srgb(color); break;
        case 1: color = pow(max(color, EPSILON), 1.0 / 2.2); break;
        case 2: color = pow(max(color, EPSILON), 1.0 / tm_gamma_custom); break;
    }

    g_output[tid] = float4(color, 1.0);
}
```

---

## 11. Film Grain, Vignette, Chromatic Aberration

These three lightweight effects run as a single combined pass for efficiency.

### Film Grain

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `grain_enabled` | bool | `false` | - | Toggle film grain |
| `grain_intensity` | float | `0.1` | 0.0 - 1.0 | Grain visibility (0 = none, 1 = heavy) |
| `grain_size` | float | `1.5` | 0.5 - 4.0 | Grain particle size in pixels |
| `grain_temporal` | bool | `true` | - | Animate grain each frame (flickering). False = static grain. |
| `grain_tinted` | bool | `false` | - | Color grain (RGB noise) vs monochrome grain (luminance noise) |

### Vignette

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `vignette_enabled` | bool | `false` | - | Toggle vignette |
| `vignette_intensity` | float | `0.4` | 0.0 - 1.5 | Darkening strength at edges |
| `vignette_roundness` | float | `0.5` | 0.0 - 1.0 | Shape: 0 = rectangular, 1 = circular |
| `vignette_smoothness` | float | `0.5` | 0.0 - 1.0 | Edge softness: 0 = hard edge, 1 = very smooth |
| `vignette_color` | float3 | `[0, 0, 0]` | 0.0 - 1.0 | Tint color for vignette (default: black) |

### Chromatic Aberration

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `ca_enabled` | bool | `false` | - | Toggle chromatic aberration |
| `ca_intensity` | float | `0.05` | 0.0 - 0.5 | Shift distance as fraction of screen width |
| `ca_start_offset` | float | `0.0` | 0.0 - 1.0 | Distance from center where CA begins (0 = center, 1 = edge only) |
| `ca_mode` | enum | `Radial` | Radial / Uniform | Radial = increases toward edges; Uniform = constant shift |

### HLSL: Combined Film/Vignette/CA Pass

```hlsl
// shaders/post/film_effects.hlsl
//
// Combined pass: Film Grain + Vignette + Chromatic Aberration.
// Runs AFTER tone mapping (operates in LDR/gamma space).

#include "common.hlsli"
#include "noise.hlsli"

cbuffer FilmParams : register(b2) {
    // Grain
    float  fe_grain_intensity;
    float  fe_grain_size;
    uint   fe_grain_temporal;   // 0 = static, 1 = animated
    uint   fe_grain_tinted;     // 0 = mono, 1 = color

    // Vignette
    float  fe_vignette_intensity;
    float  fe_vignette_roundness;
    float  fe_vignette_smoothness;
    float  fe_pad0;
    float3 fe_vignette_color;
    float  fe_pad1;

    // Chromatic Aberration
    float  fe_ca_intensity;
    float  fe_ca_start_offset;
    uint   fe_ca_mode;          // 0 = radial, 1 = uniform
    float  fe_pad2;

    float2 fe_screen_size;
    float2 fe_texel_size;
    float  fe_time;             // g_Time for grain animation
    float3 fe_pad3;
};

Texture2D<float4> g_color_input : register(t0);

SamplerState sampler_linear_clamp : register(s0);

RWTexture2D<float4> g_output : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float2 uv = (float2(tid) + 0.5) * fe_texel_size;
    float2 center_offset = uv - 0.5;  // offset from screen center

    // --- CHROMATIC ABERRATION ---
    float3 color;

    if (fe_ca_intensity > 0.0) {
        float dist_from_center = length(center_offset * float2(fe_screen_size.x / fe_screen_size.y, 1.0));

        float ca_amount;
        if (fe_ca_mode == 0) {
            // Radial: intensity increases with distance from center
            ca_amount = fe_ca_intensity * smoothstep(fe_ca_start_offset, 1.0, dist_from_center * 2.0);
        } else {
            // Uniform: constant offset everywhere
            ca_amount = fe_ca_intensity;
        }

        float2 dir = normalize(center_offset + EPSILON);
        float2 offset = dir * ca_amount * fe_texel_size;

        // Sample R, G, B at slightly different UV offsets
        color.r = g_color_input.SampleLevel(sampler_linear_clamp, uv + offset, 0).r;
        color.g = g_color_input.SampleLevel(sampler_linear_clamp, uv, 0).g;
        color.b = g_color_input.SampleLevel(sampler_linear_clamp, uv - offset, 0).b;
    } else {
        color = g_color_input[tid].rgb;
    }

    // --- VIGNETTE ---
    if (fe_vignette_intensity > 0.0) {
        float2 d = abs(center_offset) * 2.0;  // [0, 1] from center to edge

        // Shape: lerp between rectangular (max) and circular (length)
        float vignette_dist = lerp(max(d.x, d.y), length(d), fe_vignette_roundness);

        // Smooth falloff
        float vignette = 1.0 - smoothstep(1.0 - fe_vignette_smoothness, 1.0, vignette_dist);
        vignette = lerp(1.0, vignette, fe_vignette_intensity);

        // Apply vignette color
        color = lerp(fe_vignette_color, color, vignette);
    }

    // --- FILM GRAIN ---
    if (fe_grain_intensity > 0.0) {
        // Noise UV scaled by grain size
        float2 noise_uv = uv * fe_screen_size / fe_grain_size;

        // Temporal offset: different noise each frame
        float time_offset = fe_grain_temporal ? frac(fe_time * 7.13) : 0.0;

        if (fe_grain_tinted) {
            // Color grain: independent noise per channel
            float3 grain;
            grain.r = hash21(noise_uv + float2(time_offset, 0.0)) - 0.5;
            grain.g = hash21(noise_uv + float2(0.0, time_offset)) - 0.5;
            grain.b = hash21(noise_uv + float2(time_offset, time_offset)) - 0.5;
            color += grain * fe_grain_intensity;
        } else {
            // Monochrome grain
            float grain = hash21(noise_uv + float2(time_offset, time_offset * 0.7)) - 0.5;
            color += grain * fe_grain_intensity;
        }
    }

    g_output[tid] = float4(saturate(color), 1.0);
}
```

---

## 12. Motion Blur

Motion blur simulates the light accumulation that occurs during a camera's shutter open period.

### Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `mb_enabled` | bool | `false` | - | Toggle motion blur |
| `mb_shutter_angle` | float | `180.0` | 0.0 - 360.0 | Shutter angle in degrees. 180 = 50% of frame time blur. 360 = full frame. |
| `mb_sample_count` | uint | `8` | 4 - 16 | Number of blur samples along the velocity direction |
| `mb_camera_enabled` | bool | `true` | - | Apply blur from camera motion |
| `mb_object_enabled` | bool | `true` | - | Apply blur from object motion (velocity buffer) |
| `mb_max_velocity` | float | `32.0` | 4.0 - 128.0 | Maximum blur length in pixels (clamp to prevent artifacts) |

### Motion Blur Pipeline

```
Velocity Buffer (GBuffer3: RG16F)
    |
    +--> Tile Max (compute): find max velocity per 16x16 tile
    |
    +--> Neighbor Max (compute): expand tile max to 3x3 neighbors
    |
    +--> Gather Blur (compute): sample along velocity direction,
         weight by depth to avoid background bleeding onto foreground
```

### Shutter Angle vs. Blur Length

```
blur_pixels = velocity_pixels * (shutter_angle / 360.0)
```

A shutter angle of 180 degrees means the shutter is open for half the frame interval, so motion during that half-frame produces blur. 360 degrees = full frame exposure (maximum blur). 0 degrees = no blur.

Camera motion blur is computed from the per-frame camera delta (stored in the velocity buffer as if the camera were moving and the world static). Object motion blur comes from per-object velocity in the velocity buffer.

---

## 13. Fog Override

Fog parameters can be overridden per-volume, allowing different fog settings inside caves, underwater areas, or specific zones.

### Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `fog_override_enabled` | bool | `false` | - | Toggle fog override for this volume |
| `fog_color` | float3 | `[0.5, 0.6, 0.7]` | 0.0 - 1.0 | Fog color (linear) |
| `fog_density` | float | `0.005` | 0.0 - 0.5 | Exponential fog density coefficient |
| `fog_height_falloff` | float | `0.2` | 0.0 - 2.0 | Vertical fog density falloff rate |
| `fog_max_opacity` | float | `1.0` | 0.0 - 1.0 | Maximum fog opacity (1 = fully opaque at distance) |
| `fog_start_distance` | float | `0.0` | 0.0 - 500.0 | Distance at which fog begins (meters) |
| `fog_inscattering_color` | float3 | `[1.0, 0.9, 0.7]` | 0.0 - 2.0 | Directional inscattering tint (sun-facing fog glow) |
| `fog_inscattering_power` | float | `8.0` | 1.0 - 64.0 | Inscattering concentration toward light direction |

---

## 14. Custom Post-Process Materials

Artists and programmers can inject custom `.uzmat` materials (using the `POSTPROCESS` shader domain) into the post-process pipeline at specified insertion points.

### Insertion Points

| Point | Order | When | Use Case |
|-------|-------|------|----------|
| `BeforeToneMap` | After color grading, before tone mapping | HDR space manipulation | Color effects, HDR fog overlays |
| `AfterToneMap` | After tone mapping, before film effects | LDR space effects | Scan lines, posterization |
| `BeforeUI` | After all PP effects, before UI rendering | Final full-screen effect | Underwater overlay, dream sequence |
| `AfterAll` | After UI, before readback | Debug overlays | Frame analysis, pixel grid |

### Volume Configuration

```ini
[custom_material.0]
material  = "res://materials/pp_scanlines.uzmat"
insertion = AfterToneMap
enabled   = true
intensity = 0.5

[custom_material.1]
material  = "res://materials/pp_edge_detect.uzmat"
insertion = BeforeToneMap
enabled   = false
intensity = 1.0
```

### Custom Material Access

Custom post-process materials can bind these engine-provided textures:

| Texture | Register | Format | Description |
|---------|----------|--------|-------------|
| SceneColor | `t0` | `RGBA16F` (before TM) or `RGBA8` (after TM) | Current scene color |
| SceneDepth | `t1` | `D32_FLOAT` | Hardware depth buffer |
| GBuffer0 | `t2` | `RGBA8_UNORM` | Albedo + AO |
| GBuffer1 | `t3` | `RGBA16F` | Normal + roughness + metallic |
| Velocity | `t4` | `RG16F` | Motion vectors |
| CustomInput | `t5` | varies | Previous custom PP output (for chaining) |

### Example: Scan-Line Effect

```hlsl
// shaders/custom/pp_scanlines.hlsl
// POSTPROCESS domain shader: scan-line CRT overlay

#include "common.hlsli"

cbuffer MaterialParams : register(b2) {
    float  scanline_density;    // lines per screen height (default 480)
    float  scanline_intensity;  // darkness of scan lines (0-1)
    float  scanline_speed;      // vertical scroll speed (0 = static)
    float  scanline_time;       // g_Time
    float2 scanline_screen_size;
    float2 scanline_texel_size;
};

Texture2D<float4> g_scene_color : register(t0);
RWTexture2D<float4> g_output    : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint2 tid : SV_DispatchThreadID) {
    float2 uv = (float2(tid) + 0.5) * scanline_texel_size;
    float3 color = g_scene_color[tid].rgb;

    // Scan line pattern
    float y = uv.y * scanline_density + scanline_time * scanline_speed;
    float line = sin(y * PI) * 0.5 + 0.5;  // 0 to 1 oscillation
    line = pow(line, 2.0);  // sharpen

    // Apply
    color *= lerp(1.0, line, scanline_intensity);

    g_output[tid] = float4(color, 1.0);
}
```

---

## 15. File Format (.uzppv)

The `.uzppv` file format is INI-like, plain text, AI-friendly. Each file represents a single Post-Process Volume configuration.

### Format Specification

```
[uzppv version=1]
name = "<human-readable name>"
description = "<optional description>"

[volume]
shape     = box | sphere | unbound
priority  = <integer>
enabled   = true | false

; Box shape
position  = [x, y, z]
extents   = [half_x, half_y, half_z]
rotation  = [rx, ry, rz]

; Sphere shape
; position = [x, y, z]
; radius   = <float>

blend_radius   = <float, meters>
falloff_preset = linear | ease_in | ease_out | ease_in_out | step | custom
falloff_curve  = (x0,y0), (cx0,cy0), (cx1,cy1), (x1,y1)  ; cubic bezier (custom only)

; --- Override sections (only include sections this volume overrides) ---

[color_grading]
temperature = <Kelvin>
tint        = <float>
saturation  = <float>
contrast    = <float>
gamma       = <float>
gain        = <float>
offset      = <float>
shadows_hue        = <degrees>
shadows_saturation = <float>
shadows_offset     = <float>
midtones_hue       = <degrees>
midtones_saturation = <float>
midtones_gamma      = <float>
highlights_hue         = <degrees>
highlights_saturation  = <float>
highlights_gain        = <float>
color_curve_r = (x,y), (x,y), ...
color_curve_g = (x,y), (x,y), ...
color_curve_b = (x,y), (x,y), ...
color_curve_l = (x,y), (x,y), ...
lut_texture   = "res://..."
lut_size      = 16 | 32
lut_intensity = <float>

[exposure]
mode            = manual | auto
ev100           = <float>
compensation    = <float>
auto_min_ev100  = <float>
auto_max_ev100  = <float>
adapt_speed_up   = <float>
adapt_speed_down = <float>
metering_mode   = average | spot | center_weighted | histogram
lock_exposure   = true | false

[bloom]
enabled        = true | false
method         = gaussian | convolution
intensity      = <float>
threshold      = <float>
scatter        = <float>
kernel_0_width = <float>
kernel_0_tint  = [r, g, b]
; ... kernel_1 through kernel_5 ...
kernel_texture = "res://..."
dirt_texture   = "res://..."
dirt_intensity = <float>

[dof]
enabled         = true | false
method          = gaussian | bokeh
focal_distance  = <float, meters>
focal_region    = <float, meters>
near_transition = <float, meters>
far_transition  = <float, meters>
f_stop          = <float>
sensor_size     = <float, mm>
max_coc         = <float, pixels>
blade_count     = <uint>
blade_rotation  = <degrees>
focus_target    = <entity_id or 0>

[ssao]
enabled        = true | false
intensity      = <float>
radius         = <float>
power          = <float>
bias           = <float>
quality        = low | medium | high
half_res       = true | false
gtao_enabled   = true | false
temporal       = true | false
temporal_blend = <float>

[ssr]
enabled          = true | false
intensity        = <float>
quality          = low | medium | high
max_roughness    = <float>
temporal         = true | false
temporal_blend   = <float>
fallback_mode    = ibl | reflection_capture | none
fallback_intensity = <float>

[tonemapping]
operator     = aces | reinhard | uncharted2 | custom_lut | none
shoulder     = <float>
toe          = <float>
slope        = <float>
gamma_mode   = srgb | gamma22 | custom
gamma_custom = <float>
hdr_output   = sdr | hdr10 | hdr16

[film_grain]
enabled   = true | false
intensity = <float>
size      = <float>
temporal  = true | false
tinted    = true | false

[vignette]
enabled    = true | false
intensity  = <float>
roundness  = <float>
smoothness = <float>
color      = [r, g, b]

[chromatic_aberration]
enabled      = true | false
intensity    = <float>
start_offset = <float>
mode         = radial | uniform

[motion_blur]
enabled        = true | false
shutter_angle  = <degrees>
sample_count   = <uint>
camera_enabled = true | false
object_enabled = true | false
max_velocity   = <float, pixels>

[fog]
override_enabled   = true | false
color              = [r, g, b]
density            = <float>
height_falloff     = <float>
max_opacity        = <float>
start_distance     = <float, meters>
inscattering_color = [r, g, b]
inscattering_power = <float>

[custom_material.0]
material  = "res://..."
insertion = before_tonemap | after_tonemap | before_ui | after_all
enabled   = true | false
intensity = <float>
```

### Example: cinematic_forest.uzppv

```ini
[uzppv version=1]
name = "Cinematic Forest"
description = "Warm, atmospheric forest volume with shallow DoF and golden hour grading"

[volume]
shape        = box
priority     = 15
enabled      = true
position     = [120.0, 0.0, 85.0]
extents      = [40.0, 20.0, 30.0]
rotation     = [0, 0, 0]
blend_radius = 8.0
falloff_preset = ease_in_out

[color_grading]
temperature  = 5200
tint         = 0.05
saturation   = 1.15
contrast     = 1.1
gamma        = 1.0
gain         = 1.05
offset       = 0.0
shadows_hue  = 220.0
shadows_saturation = 0.15
shadows_offset     = 0.02
highlights_hue     = 40.0
highlights_saturation = 0.2
highlights_gain       = 1.1
lut_texture  = "res://luts/golden_hour_16.png"
lut_size     = 16
lut_intensity = 0.4

[exposure]
mode             = auto
compensation     = 0.5
auto_min_ev100   = 4.0
auto_max_ev100   = 12.0
adapt_speed_up   = 2.0
adapt_speed_down = 0.8
metering_mode    = center_weighted

[bloom]
enabled        = true
method         = gaussian
intensity      = 0.8
threshold      = 0.9
scatter        = 0.75
kernel_5_tint  = [1.0, 0.9, 0.7]
dirt_texture   = "res://textures/lens_dirt_forest.png"
dirt_intensity = 0.15

[dof]
enabled         = true
method          = bokeh
focal_distance  = 8.0
focal_region    = 3.0
near_transition = 2.0
far_transition  = 15.0
f_stop          = 2.0
sensor_size     = 36.0
max_coc         = 24.0
blade_count     = 7
blade_rotation  = 15.0

[vignette]
enabled    = true
intensity  = 0.35
roundness  = 0.6
smoothness = 0.7
color      = [0.02, 0.01, 0.0]

[fog]
override_enabled   = true
color              = [0.35, 0.4, 0.3]
density            = 0.008
height_falloff     = 0.3
max_opacity        = 0.85
start_distance     = 5.0
inscattering_color = [1.0, 0.85, 0.6]
inscattering_power = 12.0
```

### Example: combat_intense.uzppv

```ini
[uzppv version=1]
name = "Combat Intense"
description = "High-energy combat volume: punchy contrast, screen shake, adrenaline"

[volume]
shape    = unbound
priority = 50
enabled  = true

[color_grading]
temperature = 7000
tint        = -0.1
saturation  = 0.85
contrast    = 1.4
gamma       = 0.95
gain        = 1.0
offset      = -0.02
shadows_hue = 200.0
shadows_saturation = 0.25
shadows_offset     = -0.03
highlights_hue     = 15.0
highlights_saturation = 0.3
highlights_gain       = 1.2

[exposure]
mode             = auto
compensation     = -0.3
auto_min_ev100   = 3.0
auto_max_ev100   = 13.0
adapt_speed_up   = 5.0
adapt_speed_down = 2.0
metering_mode    = center_weighted

[bloom]
enabled   = true
method    = gaussian
intensity = 1.2
threshold = 0.8
scatter   = 0.6

[tonemapping]
operator = uncharted2
shoulder = 0.3
toe      = 0.25
slope    = 1.2

[chromatic_aberration]
enabled      = true
intensity    = 0.12
start_offset = 0.3
mode         = radial

[motion_blur]
enabled        = true
shutter_angle  = 270.0
sample_count   = 12
camera_enabled = true
object_enabled = true
max_velocity   = 48.0

[vignette]
enabled    = true
intensity  = 0.5
roundness  = 0.4
smoothness = 0.4
color      = [0.05, 0.0, 0.0]

[film_grain]
enabled   = true
intensity = 0.15
size      = 1.8
temporal  = true
tinted    = false
```

---

## 16. Pipeline Execution Order

The full post-process pipeline executes in a strict order. Each step is a DX12 compute or full-screen pass.

```
HDR Scene Color (from Deferred Lighting / Forward Transparent / Skybox)
    |
    v
+--[1. Color Grading]--- (white balance, lift/gamma/gain, curves, LUT)
|   Input:  HDR color (R11G11B10F or RGBA16F)
|   Output: Graded HDR color
|   CBV:    ColorGradingParams (b2)
|   SRVs:   HDR input (t0), curves (t1-t4), LUT (t5)
    |
    v
+--[2. SSAO Compute]--- (HBAO+ half-res + bilateral blur)
|   Input:  Depth (t0), Normals (t1)
|   Output: AO buffer (R8_UNORM, half-res)
|   CBV:    SSAOParams (b2)
    |
    v
+--[3. SSAO Apply]--- (multiply AO into ambient term)
|   Input:  Graded HDR (t0), AO buffer (t1)
|   Output: HDR with AO
    |
    v
+--[4. Hi-Z Generation]--- (min-depth mip pyramid for SSR)
|   Input:  Depth buffer
|   Output: Hi-Z mip chain
    |
    v
+--[5. SSR Ray March]--- (compute, full-res)
|   Input:  Depth (t0), Normals (t1), HDR color (t2), Hi-Z (t3), Velocity (t4)
|   Output: SSR result (RGBA16F: color + confidence)
|   CBV:    SSRParams (b2)
    |
    v
+--[6. SSR Resolve]--- (blend SSR + IBL fallback)
|   Input:  HDR color (t0), SSR result (t1), GBuffer1 (t2)
|   Output: HDR with reflections
    |
    v
+--[7. Custom PP: BeforeToneMap]--- (user materials, if any)
    |
    v
+--[8. Bloom]--- (threshold -> downsample chain -> upsample chain -> composite)
|   Input:  HDR color
|   Output: HDR color + bloom
    |
    v
+--[9. Depth of Field]--- (CoC compute -> gather/scatter blur)
|   Input:  HDR color (t0), Depth (t1)
|   Output: HDR with DoF blur
|   CBV:    DOFParams (b2)
    |
    v
+--[10. Auto-Exposure]--- (histogram -> compute EV100 -> smooth adapt)
|   Input:  HDR color (histogram), previous EV100
|   Output: Updated exposure value (written to exposure CBV)
    |
    v
+--[11. Tone Mapping]--- (ACES/Reinhard/Uncharted2/LUT + gamma)
|   Input:  HDR color (t0), exposure value
|   Output: LDR color (RGBA8_UNORM or RGB10A2)
|   CBV:    TonemapParams (b2)
    |
    v
+--[12. Custom PP: AfterToneMap]--- (user materials, if any)
    |
    v
+--[13. TAA Resolve]--- (jitter reprojection + variance clamp + sharpen)
|   Input:  LDR color (t0), TAA history (t1), Velocity (t2)
|   Output: Anti-aliased LDR color
|   CBV:    TAAParams (b2)
    |
    v
+--[14. Film Grain + Vignette + Chromatic Aberration]--- (combined pass)
|   Input:  LDR color (t0)
|   Output: LDR with film effects
|   CBV:    FilmParams (b2)
    |
    v
+--[15. Motion Blur]--- (tile velocity -> gather blur)
|   Input:  LDR color (t0), Velocity (t1)
|   Output: Final LDR color
    |
    v
+--[16. Custom PP: BeforeUI]--- (user materials, if any)
    |
    v
+--[17. UI Rendering]--- (HTML+CSS UI overlay)
    |
    v
+--[18. Custom PP: AfterAll]--- (debug overlays, if any)
    |
    v
Final Output (RGBA8_UNORM) -> Readback -> JPEG Encode -> WebSocket
```

### Render Graph Pass IDs

These integrate with the engine's render graph ordering system:

| Pass | Graph Order | Description |
|------|-------------|-------------|
| ColorGrading | 710 | White balance, lift/gamma/gain, curves, LUT |
| SSAO | 720 | HBAO+ compute + bilateral blur |
| SSAOApply | 725 | Multiply AO into ambient |
| HiZGenerate | 730 | Min-depth mip pyramid |
| SSRRayMarch | 735 | Screen-space reflection ray march |
| SSRResolve | 740 | Blend SSR + fallback |
| CustomPP_BeforeTM | 745 | Custom materials (before tone map) |
| Bloom | 750 | Downsample + upsample bloom chain |
| DOF | 755 | Depth of field |
| AutoExposure | 760 | Histogram + EV100 adaptation |
| ToneMapping | 765 | HDR to LDR conversion |
| CustomPP_AfterTM | 770 | Custom materials (after tone map) |
| TAA | 775 | Temporal anti-aliasing resolve |
| FilmEffects | 780 | Grain + vignette + CA (combined) |
| MotionBlur | 785 | Velocity-based motion blur |
| CustomPP_BeforeUI | 790 | Custom materials (before UI) |
| UI | 800 | HTML+CSS game UI |
| CustomPP_AfterAll | 810 | Debug overlays |

---

## 17. Implementation Notes

### C++ Class Hierarchy

```
PostProcessPipeline (orchestrates the full PP stack each frame)
    |
    +-- PostProcessVolume (spatial volume + override settings)
    |       |
    |       +-- PPVolumeShape (Box, Sphere, Unbound)
    |       +-- PPVolumeSettings (all overridable parameters)
    |       +-- FalloffCurve (cubic bezier)
    |
    +-- PostProcessBlender (resolves overlapping volumes)
    |       |
    |       +-- active_volumes[]  (sorted by priority)
    |       +-- blend_weights[]   (computed from camera position)
    |       +-- blended_settings  (final resolved PPVolumeSettings)
    |
    +-- PostProcessPassManager (owns individual pass instances)
    |       |
    |       +-- ColorGradingPass
    |       +-- SSAOPass
    |       +-- SSRPass
    |       +-- BloomPass
    |       +-- DOFPass
    |       +-- ExposurePass
    |       +-- TonemapPass
    |       +-- FilmEffectsPass (grain + vignette + CA)
    |       +-- MotionBlurPass
    |       +-- TAAPass
    |       +-- CustomPPPass (N instances)
    |
    +-- PPVolumeRegistry (loads/caches .uzppv files)
    |       |
    |       +-- load(path) -> Handle<PostProcessVolume>
    |       +-- hot_reload(path)  (file watcher triggers)
    |       +-- get_all_volumes() -> Span<Handle<PostProcessVolume>>
    |
    +-- ExposureState (auto-exposure adaptation state)
            |
            +-- current_ev100, target_ev100
            +-- histogram_buffer (RWStructuredBuffer<uint>, 64 bins)
            +-- adaptation_timer
```

### C++ Structures

```cpp
// src/renderer/postprocess/ppv_settings.h

// Every overridable post-process parameter, stored per-volume.
// Fields set to NaN indicate "not overridden" (falls through to lower priority).
struct PPVolumeSettings {
    // Color Grading
    float  cg_temperature;          // NaN = not overridden
    float  cg_tint;
    float  cg_saturation;
    float  cg_contrast;
    float  cg_gamma;
    float  cg_gain;
    float  cg_offset;
    float3 cg_shadows_color;
    float  cg_shadows_offset;
    float3 cg_midtones_color;
    float  cg_midtones_gamma;
    float3 cg_highlights_color;
    float  cg_highlights_gain;
    // ... curves stored separately as arrays
    StringId cg_lut_path;
    float  cg_lut_intensity;

    // Exposure
    uint8_t exp_mode;               // 0xFF = not overridden
    float  exp_ev100;
    float  exp_compensation;
    float  exp_auto_min;
    float  exp_auto_max;
    float  exp_adapt_speed_up;
    float  exp_adapt_speed_down;
    uint8_t exp_metering_mode;

    // Bloom
    bool   bloom_enabled;
    uint8_t bloom_method;
    float  bloom_intensity;
    float  bloom_threshold;
    float  bloom_scatter;
    float  bloom_kernel_width[6];
    float3 bloom_kernel_tint[6];
    StringId bloom_kernel_texture;
    StringId bloom_dirt_texture;
    float  bloom_dirt_intensity;

    // Depth of Field
    bool   dof_enabled;
    uint8_t dof_method;
    float  dof_focal_distance;
    float  dof_focal_region;
    float  dof_near_transition;
    float  dof_far_transition;
    float  dof_f_stop;
    float  dof_sensor_size;
    float  dof_max_coc;
    uint32_t dof_blade_count;
    float  dof_blade_rotation;
    EntityId dof_focus_target;

    // SSAO
    bool   ssao_enabled;
    float  ssao_intensity;
    float  ssao_radius;
    float  ssao_power;
    float  ssao_bias;
    uint8_t ssao_quality;
    bool   ssao_half_res;
    bool   ssao_gtao_enabled;
    bool   ssao_temporal;
    float  ssao_temporal_blend;

    // SSR
    bool   ssr_enabled;
    float  ssr_intensity;
    uint8_t ssr_quality;
    float  ssr_max_roughness;
    bool   ssr_temporal;
    float  ssr_temporal_blend;
    uint8_t ssr_fallback_mode;
    float  ssr_fallback_intensity;

    // Tonemapping
    uint8_t tm_operator;
    float  tm_shoulder;
    float  tm_toe;
    float  tm_slope;
    uint8_t tm_gamma_mode;
    float  tm_gamma_custom;
    uint8_t tm_hdr_output;

    // Film Grain
    bool   grain_enabled;
    float  grain_intensity;
    float  grain_size;
    bool   grain_temporal;
    bool   grain_tinted;

    // Vignette
    bool   vignette_enabled;
    float  vignette_intensity;
    float  vignette_roundness;
    float  vignette_smoothness;
    float3 vignette_color;

    // Chromatic Aberration
    bool   ca_enabled;
    float  ca_intensity;
    float  ca_start_offset;
    uint8_t ca_mode;

    // Motion Blur
    bool   mb_enabled;
    float  mb_shutter_angle;
    uint32_t mb_sample_count;
    bool   mb_camera_enabled;
    bool   mb_object_enabled;
    float  mb_max_velocity;

    // Fog
    bool   fog_override_enabled;
    float3 fog_color;
    float  fog_density;
    float  fog_height_falloff;
    float  fog_max_opacity;
    float  fog_start_distance;
    float3 fog_inscattering_color;
    float  fog_inscattering_power;

    // Custom Materials (up to 8)
    struct CustomMaterialSlot {
        StringId material_path;
        uint8_t  insertion_point;
        bool     enabled;
        float    intensity;
    } custom_materials[8];

    // Check if a field is overridden (not NaN for floats, not 0xFF for enums)
    bool is_overridden(size_t field_offset) const;

    // Blend two settings: lerp all overridden fields by weight
    static PPVolumeSettings blend(const PPVolumeSettings& base,
                                   const PPVolumeSettings& overlay,
                                   float weight);
};
```

```cpp
// src/renderer/postprocess/pp_volume.h

struct PostProcessVolume {
    StringId          name;
    StringId          file_path;       // "res://volumes/forest.uzppv"
    PPVolumeShape     shape;           // Box, Sphere, Unbound
    int32_t           priority;        // higher wins
    bool              enabled;
    float             blend_radius;    // meters
    FalloffCurve      falloff;         // cubic bezier

    // Box-specific
    float3            position;
    float3            extents;
    float3            rotation;        // Euler degrees

    // Sphere-specific
    float             radius;

    PPVolumeSettings  settings;

    // Compute blend weight for a given camera position
    float compute_weight(const float3& camera_pos) const;

    // Serialize / deserialize
    void save(const char* path) const;
    static PostProcessVolume load(const char* path);
};
```

```cpp
// src/renderer/postprocess/pp_blender.h

struct PostProcessBlender {
    Array<Handle<PostProcessVolume>> volumes;  // all volumes in scene
    PPVolumeSettings                 blended;  // final resolved settings

    // Called each frame with current camera position.
    // Sorts volumes by priority, computes weights, blends settings.
    void update(const float3& camera_pos, float delta_time);

    // Get the final blended settings (read-only, valid until next update)
    const PPVolumeSettings& get_blended() const { return blended; }
};
```

```cpp
// src/renderer/postprocess/pp_pipeline.h

struct PostProcessPipeline {
    PostProcessBlender   blender;
    PostProcessPassManager passes;
    ExposureState        exposure;
    PPVolumeRegistry     registry;

    // Per-frame execution: blend volumes, update passes, dispatch GPU work
    void execute(ID3D12GraphicsCommandList* cmd,
                 const FrameContext& frame,
                 const RenderTargets& targets);
};
```

### DX12 Resource Management

| Resource | Format | Resolution | Lifetime | Description |
|----------|--------|------------|----------|-------------|
| `pp_hdr_temp_0` | `RGBA16F` | Full-res | Per-frame | Ping-pong HDR work buffer A |
| `pp_hdr_temp_1` | `RGBA16F` | Full-res | Per-frame | Ping-pong HDR work buffer B |
| `pp_ldr_temp` | `RGBA8_UNORM` | Full-res | Per-frame | LDR work buffer (post-tonemap) |
| `ao_raw` | `R8_UNORM` | Half-res | Per-frame | SSAO raw output |
| `ao_blur_temp` | `R8_UNORM` | Half-res | Per-frame | SSAO blur intermediate |
| `ao_final` | `R8_UNORM` | Half-res | Per-frame | SSAO final blurred |
| `bloom_chain[6]` | `R11G11B10F` | 1/2 to 1/64 | Per-frame | Bloom downsample/upsample chain |
| `hiz_mips` | `R32_FLOAT` | Full + N mips | Per-frame | Hi-Z depth pyramid |
| `ssr_result` | `RGBA16F` | Full-res | Per-frame | SSR ray march output |
| `ssr_history` | `RGBA16F` | Full-res | Persistent | SSR temporal history (ping-pong) |
| `taa_history[2]` | `RGBA16F` | Full-res | Persistent | TAA history (ping-pong) |
| `histogram_buf` | `Structured<uint>` | 64 elements | Per-frame | Luminance histogram |
| `exposure_buf` | `Structured<float>` | 1 element | Persistent | Current EV100 value |
| `curve_luts` | `R8_UNORM` | 256x1 x4 | On-change | Color grading curves (R,G,B,L) |
| `color_lut_3d` | `RGBA8_SRGB` | 16^3 or 32^3 | On-change | 3D color LUT |

All per-frame resources are allocated from the frame arena allocator and recycled. Persistent resources (TAA/SSR history, exposure) survive across frames.

### Volume Hot-Reload

The `PPVolumeRegistry` watches `.uzppv` files via the engine's file watcher. On change:

1. Re-parse the modified `.uzppv` file.
2. Update the `PostProcessVolume` settings in place.
3. The `PostProcessBlender` picks up changes on the next `update()` call.
4. No PSO recompilation needed (parameters are data-driven via CBVs).

Latency: typically 1-2 frames from file save to visible change.

### VSCode WebView Implementation

The editor UI runs as a TypeScript WebView panel in the VSCode extension.

| Component | Technology | Notes |
|-----------|------------|-------|
| Color wheels | HTML5 Canvas 2D | Drag interaction, hue/sat rings |
| Curve editor | HTML5 Canvas 2D | Bezier control points, snappable grid |
| Histogram | HTML5 Canvas 2D | 64-bar chart, auto-scaled |
| 3D Viewport | JPEG stream from engine | Same pipeline as scene viewport |
| Sliders | Custom HTML input range | Labelled, numeric readout, reset-on-double-click |
| Volume list | Standard HTML list | Drag-to-reorder, context menu |

### IPC Handlers (postprocess.*)

```json
postprocess.createVolume {
    "name": "forest_entrance",
    "shape": "box",
    "position": [10, 0, 5],
    "extents": [8, 5, 6],
    "priority": 10,
    "blend_radius": 5.0
}
    -> { "ok": true, "volume_id": "vol_0042" }

postprocess.deleteVolume { "volume_id": "vol_0042" }
    -> { "ok": true }

postprocess.setVolumeParam {
    "volume_id": "vol_0042",
    "section": "bloom",
    "param": "intensity",
    "value": 1.5
}
    -> { "ok": true }

postprocess.getVolumeParam {
    "volume_id": "vol_0042",
    "section": "bloom",
    "param": "intensity"
}
    -> { "ok": true, "value": 1.5 }

postprocess.getBlendedState {}
    -> { "ok": true, "settings": { ... all current blended values ... } }

postprocess.save { "volume_id": "vol_0042", "path": "res://volumes/forest_entrance.uzppv" }
    -> { "ok": true }

postprocess.load { "path": "res://volumes/forest_entrance.uzppv" }
    -> { "ok": true, "volume_id": "vol_0043" }

postprocess.getHistogram {}
    -> { "ok": true, "bins": [0, 42, 180, ...], "current_ev": 8.3 }

postprocess.lockExposure { "locked": true }
    -> { "ok": true }

postprocess.setLUT { "volume_id": "vol_0042", "path": "res://luts/golden_hour.png" }
    -> { "ok": true }

postprocess.listVolumes {}
    -> { "ok": true, "volumes": [
        { "id": "global", "name": "Global", "priority": 0, "active": true },
        { "id": "vol_0042", "name": "forest_entrance", "priority": 10, "active": true }
    ]}
```

### AI-Native Integration (MCP)

The `postprocess_generate` MCP tool allows AI agents to create or modify PPV files from natural language descriptions.

```json
// MCP tool: postprocess_generate
{
    "prompt": "Create a horror ambiance: desaturated, cold blue shadows, heavy vignette, slight chromatic aberration, low exposure",
    "base_volume": "res://volumes/global.uzppv",  // optional: start from existing
    "output_path": "res://volumes/horror_ambiance.uzppv"
}
    -> { "ok": true, "path": "res://volumes/horror_ambiance.uzppv" }
```

The AI agent generates the `.uzppv` INI content based on the prompt, writes it to disk, and the engine hot-reloads it immediately.

---

## 18. Performance Budget

Target performance at 1080p on mid-range GPU (RTX 3060 / RX 6700 XT equivalent):

| Pass | Budget | Notes |
|------|--------|-------|
| Color Grading | 0.1ms | Simple per-pixel math + LUT lookup |
| SSAO (Medium) | 0.8ms | Half-res compute + bilateral blur (2 passes) |
| SSR (Medium, 64 steps) | 1.5ms | Full-res compute, roughness early-out |
| Bloom (Gaussian) | 0.5ms | 6-level downsample + upsample chain |
| Bloom (Convolution) | 1.5ms | FFT-based, for cinematic only |
| Depth of Field (Gaussian) | 0.5ms | Separable blur, half-res CoC |
| Depth of Field (Bokeh) | 1.5ms | Scatter-based, aperture shape |
| Auto-Exposure | 0.2ms | Histogram compute + reduction |
| Tone Mapping | 0.1ms | Per-pixel math |
| TAA | 0.3ms | Full-res compute, Catmull-Rom history sample |
| Film Effects (combined) | 0.1ms | Grain + vignette + CA in one pass |
| Motion Blur | 0.4ms | Tile max + neighbor max + gather |
| Custom PP (per material) | 0.1-0.5ms | Depends on material complexity |
| **Total (typical gameplay)** | **~3.5ms** | SSAO + Bloom + TM + TAA + Film |
| **Total (cinematic max)** | **~6.5ms** | All effects enabled, high quality |

The volume blending system itself (CPU-side `PostProcessBlender::update()`) costs less than 0.01ms even with 32 active volumes.

---

## 19. Testing Requirements

### Unit Tests

| Test | Coverage |
|------|----------|
| PPVolumeSettings::blend() | Verify lerp correctness for all field types, NaN passthrough for non-overridden fields |
| PostProcessVolume::compute_weight() | Box inside/outside/blend region, sphere inside/outside/blend, unbound always 1.0 |
| FalloffCurve::evaluate() | All presets return expected values at t=0, t=0.5, t=1.0 |
| .uzppv parsing | Round-trip save/load for all parameter types, missing sections, invalid values |
| PostProcessBlender::update() | Priority ordering, weight computation, multi-volume blend |
| Exposure histogram | Verify bin distribution for known luminance patterns |
| White balance math | Compare against reference D65/D50 conversions |

### Integration Tests

| Test | Description |
|------|-------------|
| Volume hot-reload | Modify .uzppv on disk, verify engine picks up changes within 2 frames |
| Before/after compare | Enable/disable a volume, screenshot both states, verify pixel difference |
| LUT import | Import .cube file, verify 3D texture dimensions and sample values |
| Custom PP material | Load a custom PP .uzmat, verify it receives correct input textures |
| Lua API round-trip | Push/pop volumes from Lua, verify blended state matches expected |
| IPC handlers | Call all postprocess.* IPC endpoints, verify responses |

### Visual Regression Tests

Automated screenshot comparisons for:
- Default global volume (baseline)
- Color grading: warm/cool/desaturated presets
- Bloom: off vs low vs high intensity
- DoF: near focus vs far focus vs bokeh shape
- SSAO: off vs medium vs high
- Tone mapping: each operator
- Film effects: each combination

---

## 20. Future Extensions

| Feature | Phase | Description |
|---------|-------|-------------|
| **Weather volumes** | Phase 3+ | PPV + particle system + fog for rain/snow/sandstorm |
| **Time-of-day blend** | Phase 3+ | Automatic volume transitions based on game clock |
| **Volume LOD** | Phase 3+ | Reduce PP quality at distance from camera |
| **PPV animation** | Phase 4+ | Keyframeable PP parameters on cinematic timeline |
| **Network sync** | Phase 4+ | Replicate PPV state in multiplayer (host-authoritative) |
| **DLSS/FSR integration** | Phase 3+ | Upscaling pass inserted after TAA, before film effects |
| **Lens flare system** | Phase 3+ | Screen-space lens flares driven by bright light sources |
| **Auto color grading** | Phase 4+ | AI-assisted: "match this reference image" via MCP |
