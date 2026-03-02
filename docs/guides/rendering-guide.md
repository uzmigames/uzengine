# Rendering Guide

> **Audience**: Artists and designers using UzEngine to build visually stunning scenes
> **Prerequisites**: Completed [getting-started.md](getting-started.md) and basic scene knowledge
> **Tools**: VSCode editor, asset importer, IPC debug console
> **Goal**: Create scenes with high-quality PBR materials, lighting, and post-processing

This guide walks through rendering a complete scene in UzEngine, from materials to post-processes to global illumination.

---

## Quick Start: Create Your First Lit Scene

### Step 1: Create a New Scene

1. In VSCode, create a file `res://scenes/my_first_scene.uzscene`
2. Paste the following minimal scene:

```ini
[Scene]
name = "My First Lit Scene"
enable_vsync = true
clear_color = (0.1, 0.1, 0.12, 1.0)

[Node_0]
type = "SceneRoot"
name = "Root"

[Node_1]
type = "MeshNode"
parent = 0
name = "Ground"
mesh = "res://meshes/ground.gltf"
material = "res://materials/concrete.uzmat"
position = (0, -5, 0)
scale = (10, 1, 10)

[Node_2]
type = "LightNode"
parent = 0
name = "Main Light"
type = "Directional"
intensity = 1.2
color = (1, 1, 1, 1)
rotation = (0.707, 0, 0.707, 0)  # 90° pitch (roughly overhead)
cast_shadows = true

[Node_3]
type = "MeshNode"
parent = 0
name = "Sphere"
mesh = "res://meshes/sphere.gltf"
material = "res://materials/plastic_red.uzmat"
position = (2, 0, 2)
```

3. Import meshes if needed (see [asset-pipeline-guide.md](asset-pipeline-guide.md))
4. Press **Ctrl+P** to play

You should see:
- Ground mesh with concrete material
- Red sphere with plastic material
- Directional light casting shadows
- Auto-exposed exposure based on scene brightness

---

## PBR Materials

### Overview

All materials in UzEngine use **Physically Based Rendering (PBR)** — a shader approach where material properties map to real-world physics:

- **Albedo** — Base color (no shadows/lighting baked in)
- **Metallic** — 0.0 = dielectric, 1.0 = conductor
- **Roughness** — 0.0 = mirror, 1.0 = matte
- **Normal Map** — Surface detail (perturbation of normals)
- **Ambient Occlusion (AO)** — Local shadowing from geometry
- **Emissive** — Self-illumination (light source)

### Material Asset Format (.uzmat)

Materials are simple INI files referencing texture assets.

```ini
[Material]
name = "Concrete Ground"
shader = "pbr_standard"      # or "pbr_transparent", "pbr_parallax", etc.

[Textures]
albedo = "res://textures/concrete_albedo.ktx2"          # RGB8 sRGB
normal = "res://textures/concrete_normal.ktx2"          # BC5 linear
metallic = "res://textures/concrete_metallic.ktx2"      # R8 linear (single channel)
roughness = "res://textures/concrete_roughness.ktx2"    # R8 linear
ao = "res://textures/concrete_ao.ktx2"                  # R8 linear
emissive = ""                                            # Optional

[Parameters]
albedo_tint = (1.0, 1.0, 1.0, 1.0)      # Color multiplier
metallic_scale = 1.0
roughness_scale = 1.0
ao_scale = 1.0
emissive_scale = 0.0
normal_strength = 1.0

[Flags]
two_sided = false                        # Cull backfaces?
transparent = false                      # Blend mode
parallax_enabled = false                 # Parallax occlusion mapping
```

### Creating a Material in Editor

1. Right-click in Asset Browser
2. Select "New Material"
3. Choose shader: `pbr_standard`
4. Add textures:
   - **Albedo** — Most important. Should have **no shadows or highlights** (flat lighting)
   - **Normal** — Converted to BC5 compressed format at import
   - **Roughness** — Single grayscale channel. White = smooth, black = rough
   - **Metallic** — Single channel. White = metal, black = non-metal
   - **AO** — Optional. Darkens crevices
5. Adjust **Parameters** sliders for fine-tuning
6. Save as `.uzmat` file

### Example Materials

#### Concrete (Matte, Diffuse)

```ini
[Material]
name = "Rough Concrete"
shader = "pbr_standard"

[Textures]
albedo = "res://textures/concrete_albedo.ktx2"
normal = "res://textures/concrete_normal.ktx2"
metallic = "res://textures/concrete_metallic.ktx2"     # All black (0.0)
roughness = "res://textures/concrete_roughness.ktx2"   # Gray (0.7)
ao = "res://textures/concrete_ao.ktx2"

[Parameters]
metallic_scale = 0.0       # Non-metallic
roughness_scale = 0.85     # Rough surface
```

#### Brushed Steel (Anisotropic Metal)

```ini
[Material]
name = "Brushed Steel"
shader = "pbr_standard"

[Textures]
albedo = "res://textures/steel_albedo.ktx2"
normal = "res://textures/steel_brushed_normal.ktx2"
metallic = "res://textures/steel_metallic.ktx2"        # White (1.0)
roughness = "res://textures/steel_roughness.ktx2"      # Medium (0.4)

[Parameters]
metallic_scale = 1.0       # Fully metallic
roughness_scale = 0.4      # Relatively smooth
normal_strength = 1.2      # Exaggerate brush texture
```

#### Glass (Transparent, Smooth Metal)

```ini
[Material]
name = "Glass Window"
shader = "pbr_transparent"

[Textures]
albedo = "res://textures/glass_albedo.ktx2"        # Mostly white
normal = "res://textures/glass_normal.ktx2"
metallic = "res://textures/glass_metallic.ktx2"    # Black (0.0)
roughness = "res://textures/glass_roughness.ktx2"  # Very low (0.1)

[Parameters]
metallic_scale = 0.0
roughness_scale = 0.1
transparent = true
```

### Common Mistakes

> **⚠️ Warning**: Do NOT bake shadows or highlights into albedo textures. Albedo should be flat (no lighting). Shadows come from the lighting pass, not the texture.

> **⚠️ Warning**: Roughness and metallic maps should be **linear**, not sRGB. Most tools export sRGB by default. Check import settings.

> **💡 Tip**: For most materials, keep roughness between 0.4–0.8. Values outside this range are extreme (mirror vs. clay).

---

## Lighting

### Overview

UzEngine supports three light types:

| Light Type | Use Case | Shadows | Cost |
|------------|----------|---------|------|
| **Directional** | Sun/moon, outdoor skylight | 3-cascade CSM | High (if cascades overlap) |
| **Point** | Lanterns, bulbs, explosions | Dual-paraboloid | Medium (per-object) |
| **Spot** | Flashlights, spotlights, search lights | Perspective | Medium (per-object) |

### Directional Light (Sun)

The main outdoor light source. Typically one per scene.

```ini
[Node_0]
type = "LightNode"
name = "Sun"
type = "Directional"

[Directional]
intensity = 1.0                      # 0.0–2.0 typical
color = (1.0, 0.98, 0.85, 1.0)      # Warm white (sunset-like)
cast_shadows = true

[Shadow]
cascade_count = 3                    # 3 cascades: near, mid, far
cascade_distances = (20, 100, 500)   # Cascade split distances
shadow_map_size = 2048               # Resolution per cascade
pcf_radius = 2                       # PCF 2×2 (0–4 typical)
```

#### Rotation (Setting Light Direction)

Rotate using **pitch (X) and yaw (Z)** in world space:

```ini
[Node_0]
rotation = (pitch, 0, yaw, 0)   # Quaternion; W is implicit
```

Or use Euler angles and convert:

```python
# Convert (pitch, yaw) to quaternion for DirectionalLight
import math

pitch = math.radians(45)   # 45° down
yaw = math.radians(180)    # 180° (south)

# Formula: Quat from Euler (pitch, 0, yaw)
q = Quat.from_euler(pitch, 0, yaw)
# Approximate: (sin(pitch/2), 0, sin(yaw/2), cos(pitch/2 + yaw/2))
```

### Point Light (Omnidirectional)

Radiates light in all directions from a position.

```ini
[Node_1]
type = "LightNode"
name = "Campfire"
type = "Point"

[Point]
intensity = 2.5
color = (1.0, 0.7, 0.3, 1.0)        # Orange/warm
radius = 20.0                        # Falloff distance
cast_shadows = true

[Shadow]
shadow_map_size = 512                # Per-face resolution (6 faces total)
far_plane = 50.0                     # Clip distance
pcf_radius = 1
```

**Falloff**: Light attenuation follows physically-based inverse-square falloff:

```
intensity_at_distance = intensity / (1 + (distance / radius)²)
```

### Spot Light (Directional Cone)

Concentrated cone of light, like a flashlight.

```ini
[Node_2]
type = "LightNode"
name = "Flashlight"
type = "Spot"

[Spot]
intensity = 3.0
color = (0.9, 0.95, 1.0, 1.0)       # Cool white
inner_cone = 20.0                    # Degrees (full brightness)
outer_cone = 45.0                    # Degrees (fade to zero)
range = 100.0                        # Max distance
cast_shadows = true

[Shadow]
shadow_map_size = 1024
pcf_radius = 2
```

**Cone falloff**: Smooth blend between inner and outer cone angles.

### Shadow Quality Tuning

Shadows are expensive. Balance quality vs. performance:

```ini
# High quality (8 ms for 3 cascades + point light)
[Directional_Shadow_Quality_High]
cascade_count = 4
shadow_map_size = 4096
pcf_radius = 2
use_variance_shadow_maps = false     # Standard PCF

# Balanced (3–4 ms)
[Directional_Shadow_Quality_Medium]
cascade_count = 3
shadow_map_size = 2048
pcf_radius = 1
use_variance_shadow_maps = false

# Fast (1 ms)
[Directional_Shadow_Quality_Low]
cascade_count = 2
shadow_map_size = 1024
pcf_radius = 0                       # No filtering
```

> **💡 Tip**: For most scenes, 3 cascades at 2048 resolution is a sweet spot. Increase only for hero shots.

---

## Post-Processing

### Overview

Post-processes are effects applied to the final rendered image. UzEngine supports these via the **render graph**:

| Effect | Purpose | Cost | Quality |
|--------|---------|------|---------|
| **Bloom** | Bright light glow | 2 ms | High |
| **SSAO** | Ambient occlusion | 1.5 ms | Good |
| **SSR** | Screen-space reflections | 2 ms | Good (for rough surfaces) |
| **TAA** | Temporal anti-aliasing | 1 ms | Excellent |
| **Tonemap** | Exposure & gamma | 0.1 ms | Required |

### Bloom

Blooms simulate the scattering of very bright light in the camera lens.

```ini
[Scene]
enable_bloom = true

[Bloom]
threshold = 1.0           # Brightness threshold (>1.0 = overexposed)
intensity = 0.5           # Contribution to final image
blur_radius = 2           # 1–4 typical
downsampling = 2          # 2x or 4x resolution reduction
```

How it works:

1. Extract bright pixels (> threshold)
2. Downsample 4× (faster blur)
3. Gaussian blur multiple times
4. Blend back to main image with `intensity` multiplier

Example settings:

- **Dark scene** (cave): `threshold=0.5, intensity=0.3` (subtle)
- **Sunny scene**: `threshold=1.5, intensity=0.7` (strong glow)
- **Neon scene**: `threshold=0.8, intensity=1.2` (intense)

### SSAO (Screen-Space Ambient Occlusion)

Darkens crevices and occluded areas based on depth discontinuities.

```ini
[Scene]
enable_ssao = true

[SSAO]
radius = 1.0              # How far to sample (world units)
bias = 0.1                # Prevent self-occlusion
intensity = 1.0           # Strength of darkening
blur_passes = 2           # Gaussian blur for smoothness
```

Performance tuning:

```ini
# Fast path (0.5 ms)
radius = 0.5
blur_passes = 1

# Balanced (1.5 ms)
radius = 1.0
blur_passes = 2

# Quality (3 ms)
radius = 2.0
blur_passes = 3
```

> **💡 Tip**: SSAO is most effective in indoor/cave scenes. Disable for outdoor scenes to save GPU time.

### SSR (Screen-Space Reflections)

Reflections computed from screen-space depth, perfect for rough (non-metal) surfaces.

```ini
[Scene]
enable_ssr = true

[SSR]
ray_steps = 32            # Number of ray march steps (16–64)
thickness = 0.1           # Thickness of geometry for hit detection
fallback_to_env = true    # Blend to env map if no SSR hit
```

Works best for:
- Wet surfaces (roads, puddles)
- Rough metal (worn armor)
- Matte plastics

Not for:
- Very smooth surfaces (use reflection captures instead)
- Outdoor skyline (too thin, miss rays)

### TAA (Temporal Anti-Aliasing)

Eliminates aliasing by jittering the camera over frames and resolving. Essential for quality.

```ini
[Scene]
enable_taa = true

[TAA]
jitter_scale = 1.0        # 0.5–1.5; higher = more aggressive
variance_clamp = 1.0      # Temporal variance tolerance
feedback = 0.88           # 0.7–0.95; higher = more ghosting
```

> **⚠️ Warning**: TAA can cause ghosting on fast-moving objects. If severe, reduce `jitter_scale`.

### Tonemap

Converts from HDR to display range. Affects brightness, saturation, color grading.

```ini
[Scene]
tonemap_mode = "ACES"     # Industry standard (slight color shift)

[Exposure]
auto_exposure = true      # Auto-adjust based on scene
min_ev = -4.0             # Exposure value range
max_ev = 4.0
adaptation_speed = 0.5    # How fast to adjust (0.1–1.0)

[ColorGrade]
saturation = 1.0          # 0.5 = desaturated, 2.0 = vibrant
contrast = 1.0
brightness = 0.0
shadows_tint = (0, 0, 0)  # Tint dark areas
highlights_tint = (0, 0, 0)
```

Example presets:

- **Neutral**: All values at 1.0 / 0.0
- **Cinematic**: `saturation=0.9, contrast=1.1`
- **Dark & Moody**: `saturation=0.7, brightness=-0.2`
- **Bright & Airy**: `saturation=1.2, brightness=0.1`

---

## Decals

### Overview

Decals are planar surfaces projected onto geometry (bullet holes, blood splatters, paint marks). They're efficient because they don't require new geometry.

### Creating a Decal

```ini
[Node_5]
type = "DecalNode"
name = "Bullet Hole"
parent = 1  # Parent to wall
material = "res://materials/bullet_hole.uzmat"

[Decal]
size = (0.1, 0.1, 0.2)     # Width, height, projection depth
decal_layers = 15          # Which meshes this projects onto
sort_order = 0             # Z-order (higher = on top)

[Flags]
affects_diffuse = true     # Apply albedo
affects_normal = true      # Apply normal
affects_roughness = true   # Modulate roughness
```

### Decal Material

Decal materials are similar to standard PBR but typically smaller, with alpha blending:

```ini
[Material]
name = "Bullet Hole"
shader = "decal"

[Textures]
albedo = "res://textures/bullet_hole_albedo.ktx2"
normal = "res://textures/bullet_hole_normal.ktx2"

[Parameters]
albedo_tint = (1.0, 1.0, 1.0, 0.9)   # Slight transparency
```

### Performance

- Each decal adds a small GPU cost (~0.01 ms per decal)
- Limit to ~50 active decals per scene
- Use decal atlasing for many similar decals (Phase 4)

---

## Global Illumination (GI)

### Static Lightmaps

For static geometry with high-quality indirect lighting, use **lightmaps**. These are pre-baked texture atlases that store indirect light per surface.

#### Step 1: Prepare Mesh

1. Create a second UV channel (UV2) with **non-overlapping UVs in [0,1] space**
   - In Blender: `Unwrap → Smart UV Project` (UV2 slot)
   - In Maya: `Automatic` unwrap
2. Import mesh to UzEngine

#### Step 2: Mark Mesh as Static

```ini
[Node_1]
type = "MeshNode"
mesh = "res://meshes/ground.gltf"
material = "res://materials/concrete.uzmat"

[Lightmap]
receive_shadows = true     # Affected by lights
cast_shadows = false       # Static geometry (no dynamic shadows on it)
lightmap_uv_channel = 1    # Use UV2 channel
```

#### Step 3: Bake Lightmap (Phase 3)

```
Editor command: baker.bake_scene(scene_path)

Output: res://lightmaps/scene_name_lightmap.uzlightmap
```

The baker:

1. Renders the scene with path tracing (4+ bounces)
2. Accumulates light samples per texel
3. Filters and compresses the result
4. Stores in `.uzlightmap` asset

Once baked, lightmaps apply automatically to all receiving nodes.

### DDGI (Dynamic Global Illumination)

For **dynamic objects** or scenes with moving lights, use DDGI probes instead of baking.

#### Step 1: Add DDGI Volume

```ini
[Node_10]
type = "DDGIVolume"
name = "DDGI Grid"
parent = 0

[DDGI]
grid_position = (0, 0, 0)
grid_scale = (30, 20, 30)   # World-space extent
probe_spacing = 2.0         # Distance between probes
ray_count = 128             # Rays per probe per frame
irradiance_resolution = 16  # 16×16 per probe
```

#### Step 2: Configure Probes

Probes automatically:
- Ray march every frame
- Accumulate irradiance (temporal smoothing)
- Relocate if stuck in geometry

No baking needed! Just add the volume and play.

### Reflection Captures

For **glossy surfaces**, place reflection captures:

```ini
[Node_11]
type = "ReflectionCapture"
name = "Lobby Capture"
position = (5, 3, 5)

[ReflectionCapture]
influence_radius = 20.0
intensity = 1.0
type = "Stationary"        # Or "Realtime" for per-frame updates
```

Then **bake** the capture (Phase 2 onward):

```
Editor command: baker.bake_reflection_capture(capture_id)

Output: cubemap texture, automatically referenced
```

#### When to Use Each

| Technique | When | Cost |
|-----------|------|------|
| **Lightmap** | Static scene (interiors, architecture) | ~10–30 min bake, free at runtime |
| **DDGI** | Dynamic scene (characters, moving objects) | 2.5 ms/frame |
| **Reflection** | Glossy materials (water, metal, plastic) | 0.5 ms/frame |

---

## Advanced Techniques

### Parallax Occlusion Mapping

Adds depth to surfaces without adding geometry. Enabled per-material:

```ini
[Material]
name = "Stone Wall with Depth"
shader = "pbr_standard"

[Textures]
height = "res://textures/wall_height.ktx2"

[Parameters]
parallax_enabled = true
parallax_height = 0.05     # Exaggeration factor
parallax_samples = 8       # 4–16 typical
```

### Emission (Self-Illumination)

Makes materials glow as light sources:

```ini
[Material]
name = "Glowing Neon"

[Textures]
emissive = "res://textures/neon_glow.ktx2"

[Parameters]
emissive_scale = 2.0       # HDR brightness
```

Emissive doesn't cast light onto other objects (it's not a light source). To affect GI, add a Light node nearby.

### Two-Sided Materials

For thin geometry (cloth, paper, leaves):

```ini
[Material]
name = "Leaves"

[Flags]
two_sided = true           # Render both sides
cast_shadows = true        # Still cast shadows

[Parameters]
roughness_scale = 0.7
```

---

## Performance Tips

### Draw Call Budget

- **Target**: 1000–1500 draw calls per frame at 1080p
- Each MeshNode with a unique material = 1 draw call
- Use atlased textures and shared materials to reduce calls

### Texture Memory Budget

- **Total VRAM**: 1–2 GB for textures on mid-range GPUs
- Use compressed formats:
  - **Albedo**: BC7 (16 MB per 2K texture)
  - **Normals**: BC5 (8 MB per 2K)
  - **Data (roughness, metallic)**: BC4 (4 MB per 2K)

### Shadow Quality vs. Performance

| Profile | Cascade Count | Map Size | Cost |
|---------|---------------|----------|------|
| **Ultra** | 4 | 4096 | 8 ms |
| **High** | 3 | 2048 | 4 ms |
| **Medium** | 2 | 1024 | 1.5 ms |
| **Low** | 1 | 512 | 0.3 ms |

Choose based on your target framerate and GPU.

### LOD (Level of Detail)

For distant objects, use lower polygon meshes:

```ini
[Node_5]
type = "MeshNode"
mesh = "res://meshes/tree_high_poly.gltf"
lod_meshes = [
    "res://meshes/tree_medium_poly.gltf",
    "res://meshes/tree_low_poly.gltf"
]
lod_distances = [50, 100]  # Switch at 50m, then 100m
```

### Dynamic Resolution Scaling

If framerate drops below 60 FPS, automatically reduce render resolution:

```ini
[Scene]
enable_dynamic_resolution = true
target_framerate = 60.0
resolution_scale_min = 0.75
resolution_scale_max = 1.0
```

---

## Debugging & Visualization

### IPC Debug Commands

#### View Intermediate Render Targets

```
debug.show_render_target("gbuffer_albedo")
debug.show_render_target("gbuffer_normal")
debug.show_render_target("gbuffer_depth")
debug.show_render_target("ssao_mask")
debug.show_render_target("ssr_buffer")
debug.show_render_target("bloom_final")
```

#### Lighting Debug

```
debug.show_light_heatmap()          # Brightness per pixel
debug.show_shadow_cascades()        # Color-coded cascades
debug.show_ddgi_probes()            # Grid of DDGI probes
debug.show_reflection_probes()      # Reflection capture positions
```

#### Performance

```
debug.gpu_frame_time()              # GPU time per frame (ms)
debug.gpu_frame_graph()             # Per-subsystem breakdown
debug.ddgi_statistics()             # Probe update time
```

### Visual Glitches & Fixes

#### Shadow Artifacts (Moire, Banding)

**Problem**: Repeating lines in shadow maps
**Fix**: Increase `pcf_radius` or `shadow_map_size`

#### Light Leaks (Bright Spots in Crevices)

**Problem**: SSAO or DDGI leaking light through geometry
**Fix**:
- Increase `ao_bias` in SSAO
- Enable probe relocation in DDGI
- Check mesh normals (should point outward)

#### Flickering Reflections

**Problem**: SSR jittering with TAA
**Fix**:
- Reduce `taa_jitter_scale`
- Increase SSR `ray_steps`
- Use reflection captures instead

#### Banding in Gradients

**Problem**: Visible color bands in smooth gradients
**Fix**:
- Enable TAA (temporal jitter hides banding)
- Use higher bit-depth textures (16-bit for HDR)

---

## Workflow: Complete Lit Scene

Here's a complete walkthrough from start to finish.

### 1. Scene Setup

```ini
[Scene]
name = "Castle Interior"
clear_color = (0.05, 0.05, 0.08, 1.0)
enable_bloom = true
enable_ssao = true
enable_ssr = true
enable_taa = true

[Bloom]
threshold = 1.0
intensity = 0.6

[SSAO]
radius = 1.5
intensity = 1.0

[SSR]
ray_steps = 48
fallback_to_env = true
```

### 2. Import Assets

- Download or create models (FBX/glTF)
- Import via Asset Browser (Ctrl+I)
- Verify mesh normals (point outward)

### 3. Create Materials

For each surface type:

- **Stone wall**: Concrete-like material (rough, non-metallic)
- **Torch holder**: Oxidized metal (rough, metallic)
- **Tapestry**: Cloth (rough, two-sided)
- **Torches**: Emissive (glowing fire)

### 4. Build Scene Graph

```ini
[Node_0]
type = "SceneRoot"
name = "Root"

[Node_1]
type = "MeshNode"
name = "Walls"
mesh = "res://castle_walls.gltf"
material = "res://materials/stone_wall.uzmat"
position = (0, 0, 0)
# ... (lightmap data once baked)

[Node_2]
type = "LightNode"
name = "Torchlight"
type = "Point"
intensity = 3.0
color = (1.0, 0.6, 0.2)
position = (3, 2, 0)
radius = 15.0
cast_shadows = true

[Node_3]
type = "MeshNode"
name = "Torch"
mesh = "res://torch.gltf"
material = "res://materials/torch_fire.uzmat"
position = (3, 2, 0)
scale = (0.5, 0.5, 0.5)

[Node_4]
type = "DDGIVolume"
name = "Dynamic GI"
grid_scale = (30, 15, 30)
probe_spacing = 3.0
```

### 5. Lighting Pass

Place key lights:

1. **Main directional light** (sun or torchlight from window)
2. **Fill lights** (secondary, softer)
3. **Point/spot lights** for props (torches, lamps)

### 6. Post-Process Tuning

Render and iterate:

- Does bloom look right? Adjust `threshold` and `intensity`
- Is occlusion too strong/weak? Adjust `SSAO.intensity`
- Do reflections look crisp or noisy? Adjust `SSR.ray_steps`

### 7. Bake Lightmap (Optional)

If the scene has minimal dynamic lights, bake:

```
baker.bake_scene("res://scenes/castle.uzscene")
```

Wait 10–30 minutes. Once done, disable DDGI and use lightmap results.

### 8. Final Pass

- Check frame time (`debug.gpu_frame_time()`)
- Verify no GPU errors in console
- Capture screenshot (Ctrl+S)

---

## Related Guides

- [Asset Pipeline Guide](asset-pipeline-guide.md) — How to import and prepare assets
- [Physics Guide](physics-guide.md) — Physics-based object interaction
- [Lua Scripting Guide](lua-scripting-guide.md) — Dynamic material/light changes via scripts
- [Editor Workflow](editor-workflow.md) — VSCode panels and keyboard shortcuts

---

## Related Specs

- [Renderer Spec](../specs/renderer.md) — Full PBR pipeline details
- [Render Graph Spec](../specs/render-graph.md) — Post-process composition
- [Global Illumination Spec](../specs/global-illumination.md) — Lightmaps, DDGI, reflection captures
- [Screen-Space Effects Spec](../specs/screen-space-effects.md) — SSAO, SSR, TAA algorithms
- [Shader System Spec](../specs/shader-system.md) — Shader hot-reload and variants

---

## FAQ

**Q: My scene is too dark. How do I brighten it?**

A: Check in order:
1. Increase directional light `intensity`
2. Add fill lights (point lights with lower intensity)
3. Enable DDGI (adds dynamic GI)
4. Increase `Exposure.auto_exposure` max value
5. Check if ambient occlusion (SSAO) is too strong

**Q: Shadows look pixelated. How do I fix it?**

A: Increase `shadow_map_size` (2048 → 4096). Cost: ~2× GPU time.

**Q: Post-processes are too expensive. What should I disable?**

A: Priority (remove in order):
1. SSAO (1.5 ms)
2. SSR (2 ms)
3. Bloom (2 ms)
4. TAA (keep enabled for quality; only reduce `jitter_scale`)

**Q: How do I add emissive objects that light the scene?**

A:
1. Create material with `emissive_scale > 1.0`
2. Add Point/Spot light at same position
3. Light the scene; emissive texture just glows

**Q: Can I have both lightmaps and DDGI?**

A: Yes. Use lightmaps for static geometry, DDGI for dynamic. They blend automatically.

**Q: How do I test performance on different GPUs?**

A: Use `debug.gpu_frame_time()` to profile locally. For CI/CD, render at target resolutions and log GPU time.
