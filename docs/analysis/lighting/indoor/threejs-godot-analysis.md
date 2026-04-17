<!-- doc-meta
title: Indoor Lighting in Game Engines — Analysis & Recommendations for UzEngine
date: 2026-04-06
author: Claude Code Research Team
status: reference
category: rendering
tags: indoor, lighting, ambient, fill-lights, sponza, enclosed-scenes
depth: 1000 lines max
-->

# Indoor Lighting in Game Engines — Analysis & Recommendations

**Problem**: A directional light with shadow maps sees everything inside a building as "in shadow" because walls/ceiling block the light. Without special handling, interiors are either pitch black or uniformly lit. This document analyzes how Three.js, Godot 4, and industry-standard engines solve this problem, then recommends a priority implementation path for UzEngine.

---

## Executive Summary

The "directional light only" approach NEVER works for indoor scenes. All professional engines use a layered approach:

1. **Ambient/Environment fill** (lowest priority, highest frequency) — provides base illumination everywhere
2. **Multiple fill lights** (point/spot lights) — artist-placed inside buildings to supplement directional light
3. **Real-time GI** (optional, high cost) — indirect light propagation for full dynamic lighting
4. **Screen-space AO/shadows** (postprocessing) — depth perception and contact shadows
5. **Baked lightmaps** (static geometry) — pre-computed direct + indirect light

**For UzEngine's Sponza atrium**: Current implementation relies on `DirectionalLight + SkyLight + GBuffer deferred shading`. This works for outdoor-facing surfaces but fails for interior walls. Adding point/spot lights inside the atrium + increasing `SkyLight.indirectLightingIntensity` will fix this.

---

## Part 1: Three.js Approach to Indoor Lighting

### 1.1 Core Strategy: Layered Illumination

Three.js does NOT have a built-in real-time GI system. Instead, the community pattern combines:

1. **HemisphereLight** — gradient fill from sky color (ceiling) to ground color (floor)
2. **AmbientLight** — uniform fill at a low intensity
3. **DirectionalLight** — primary sun/outdoor light
4. **PointLight/SpotLight** — artist-placed fill lights inside buildings
5. **LightProbe (spherical harmonics)** — per-object ambient captured from environment

### 1.2 HemisphereLight for Indoor Fill

```javascript
// Typical Three.js indoor setup
const skyColor = 0xb1e1ff;      // Light blue — ceiling
const groundColor = 0xb97316;   // Warm brown — floor bounce
const intensity = 0.8;          // Relative to ambient

const hemisphereLight = new THREE.HemisphereLight(
    skyColor, 
    groundColor, 
    intensity
);
scene.add(hemisphereLight);
```

**Key insight**: HemisphereLight is directional (it depends on surface normal), so it naturally simulates light bouncing off the ground/ceiling. For an atrium:
- Ceiling areas → full skyColor contribution
- Floor/wall areas → ground color contribution (bounce light)
- Side walls → linear interpolation based on normal

**Parameters**:
- Sky color (default: 0xffffff white) — what light from above looks like
- Ground color (default: 0xffffff white) — what bounce light from ground looks like
- Intensity (default: 1.0) — total contribution multiplier

### 1.3 AmbientLight for Minimum Brightness

```javascript
// Very low intensity — just prevent full black
const ambientLight = new THREE.AmbientLight(0x404040, 0.5);
scene.add(ambientLight);
```

**Usage**: AmbientLight is global and uniform. It prevents enclosed areas from being completely black without contributing to the shading model. In Three.js, this is the simplest form of ambient fill.

**Why low intensity?**: High ambient light (>0.5 intensity) makes the scene look washed out and removes all shadow depth. The proper strategy is:
1. Use HemisphereLight for directional fill (appears natural)
2. Use AmbientLight only for minimum brightness (0.2–0.5 intensity)
3. Use point/spot lights for detailed interior lighting

### 1.4 Point/Spot Lights for Interior Detail

```javascript
// Ceiling light fixture inside atrium
const ceilingLight = new THREE.PointLight(0xffffff, 800.0, 5000.0);
ceilingLight.position.set(100, 800, 200);  // Ceiling position (centimeters)
ceilingLight.castShadow = true;            // Optional: expensive for many lights
scene.add(ceilingLight);

// Spot light for focused accent
const spotLight = new THREE.SpotLight(0xffff99, 500.0, 3000.0, 45 * Math.PI/180, 0.5, 1.0);
spotLight.position.set(0, 400, 0);
spotLight.target.position.set(0, 0, 0);
spotLight.castShadow = true;
scene.add(spotLight);
scene.add(spotLight.target);
```

**Three.js PointLight signature**:
```javascript
new PointLight(color, intensity, distance, decay)
```
- `color`: Light color (0xffffff = white)
- `intensity`: In Three.js units (no standard physics, artist-controlled)
- `distance`: Attenuation range (light falloff to zero beyond this)
- `decay`: Falloff curve (2.0 = physically correct inverse-square)

**Three.js SpotLight signature**:
```javascript
new SpotLight(color, intensity, distance, angle, penumbra, decay)
```
- `angle`: Full cone angle (45° = 45-degree cone half-angle from center)
- `penumbra`: Soft edge falloff (0.0–1.0, where 1.0 = 100% soft edge)
- `decay`: Same falloff curve as point light

### 1.5 LightProbe for Ambient Capture

```javascript
import { LightProbeGenerator } from 'three/examples/jsm/lights/LightProbeGenerator.js';

// Generate spherical harmonics encoding from a cubemap
const lightProbe = LightProbeGenerator.fromCubeTexture(cubeTexture);
lightProbe.intensity = 1.5;
scene.add(lightProbe);
```

**What it does**: Captures the dominant light direction + color from an environment cubemap and encodes it as 9 spherical harmonic coefficients (3rd-order SH). Every object automatically receives this ambient lighting without additional cost.

**When to use**:
- Indoor scenes with a custom HDRI environment (ballroom, warehouse)
- When you need rich directional fill from an image

**When NOT to use**:
- Outdoor scenes (use HemisphereLight + DirectionalLight instead)
- When directional light drives the sky (redundant with SkyLight)

### 1.6 SSAO for Depth Perception

```javascript
import { EffectComposer, RenderPass, EffectPass } from 'three/examples/jsm/postprocessing/index.js';
import { SSAOEffect } from 'three/examples/jsm/postprocessing/SSAOEffect.js';

const composer = new EffectComposer(renderer);
composer.addPass(new RenderPass(scene, camera));

const ssaoEffect = new SSAOEffect(camera, scene.getObjectByName('NormalPass').texture);
ssaoEffect.radius = 30.0;           // Sampling radius in pixels
ssaoEffect.samples = 16;            // AO samples per pixel
ssaoEffect.intensity = 1.0;         // AO contribution strength
composer.addPass(new EffectPass(camera, ssaoEffect));

renderer.render(composer);
```

**Why SSAO matters for interiors**:
- Adds visual depth to corners and crevices (contact shadows)
- Darkens interior walls naturally without extra light calculations
- Runs as a fast postprocess (independent of scene complexity)

**Typical parameters for indoor scenes**:
- `radius`: 20–50 (pixel-space, larger = broader AO influence)
- `samples`: 8–32 (more = softer but slower)
- `intensity`: 0.5–1.5 (how dark the AO appears)

---

## Part 2: Godot 4 Approach to Indoor Lighting

Godot 4 is unique among game engines in offering real-time global illumination (GI) out of the box. Two systems compete for indoor lighting dominance:

### 2.1 VoxelGI — Full Real-Time GI

**What it does**: Voxelizes the scene into a 3D grid and traces indirect light bounces per frame. Every dynamic object contributes to and receives GI.

**Setup for indoor scenes**:

1. **Create and position VoxelGI node**
   ```gdscript
   var voxel_gi = VoxelGI.new()
   voxel_gi.size = Vector3(6000, 3000, 6000)  # Atrium extents (cm)
   add_child(voxel_gi)
   ```

2. **Configure static geometry**
   - Mark all static walls/ceiling/floor as "Static" in the ImportSettings
   - Or set `global_illumination_mode = StaticLightmapGI` in MeshInstance3D

3. **Set the critical Interior flag**
   ```gdscript
   voxel_gi.interior = true  # Prevents sky leaking into enclosed spaces
   ```

4. **Bake the lighting** (only needed once for static geometry)
   - Select VoxelGI in editor
   - Click "Bake" in the viewport (takes several seconds)
   - Godot re-bakes whenever static geometry changes

5. **Fine-tune subdiv and bias**
   ```gdscript
   voxel_gi.subdiv = VoxelGI.Subdiv.SUBDIV_64  # Higher = more detail but slower
   voxel_gi.bias = 1.5                          # Reduce light bleeding artifacts
   voxel_gi.normal_bias = 1.5                   # Surface-normal bias to prevent self-occlusion
   ```

**Advantages**:
- Fully dynamic (objects cast GI shadows immediately)
- Works on any GPU (no ray tracing required)
- Interior mode prevents unrealistic light leaking
- Subdiv can be tuned per-scene (64 voxels vs 256 voxels)

**Disadvantages**:
- Limited to small/medium scenes (~6000 cm per axis)
- Voxel grid must be positioned and sized manually
- Baking takes time even for static scenes
- Requires re-bake when geometry changes

### 2.2 SDFGI — Scalable Real-Time GI

**What it does**: Uses a Signed Distance Field (distance to nearest surface in each voxel) to compute GI without ray tracing. Light bounces are computed per cascade (coarse + fine detail).

**Setup for indoor scenes**:

```gdscript
var world_env = WorldEnvironment.new()
var env = Environment.new()

# Enable SDFGI
env.sdfgi_enabled = true

# CRITICAL: Disable sky light in fully indoor scenes
env.sdfgi_read_sky_light = false  # Set to false for interior-only levels

# Configure cascades for indoor detail
env.sdfgi_cascades = 4            # 1–8 cascades (more = further reach but slower)
env.sdfgi_min_cell_size = 25.0    # cm (lower = more detail, more cost)
env.sdfgi_y_scale = 0.5           # 50% vertical scale = 2x detail on Y axis

world_env.environment = env
add_child(world_env)
```

**Key insight for interiors**: `sdfgi_read_sky_light = false`
- **For outdoor/mixed scenes**: Set to `true` — SDFGI reads sky light from the environment
- **For fully enclosed scenes**: Set to `false` — no sky light leaks in from above

This single parameter is the difference between a correctly lit basement and a light-leaking cave.

**Advantages**:
- Scales to any scene size (unlike VoxelGI)
- No baking required (truly real-time)
- Procedurally generated levels supported
- Automatic cascade distribution

**Disadvantages**:
- Less detailed than VoxelGI (smoother, less dynamic shadows)
- Requires signed distance field (geometry must be watertight)
- Dynamic objects have less influence than in VoxelGI
- More complex to tune (cascades, cell size, Y-scale)

### 2.3 Practical Comparison for Sponza

| Feature | VoxelGI | SDFGI | UzEngine Today |
|---------|---------|-------|---|
| **Interior mode** | `interior=true` | `read_sky_light=false` | ❌ Manual SkyLight control only |
| **Real-time GI** | ✅ Yes | ✅ Semi-real-time | ❌ No (static IBL only) |
| **Dynamic bounces** | ✅ Full | ⚠️ Limited | ❌ No |
| **Setup time** | Medium (bake required) | Low (automatic) | Low (just boost SkyLight) |
| **Small scenes** | ✅ Best | ✅ Good | ⚠️ Depends on SkyLight tune |
| **Large worlds** | ❌ No | ✅ Yes | ⚠️ Depends on SkyLight tune |

---

## Part 3: Industry-Standard Approach (UE5/Unity Reference)

### 3.1 The Canonical Three-Light System

Professional game engines teach a simple hierarchy for interior lighting:

**Primary Fill** (lowest priority, affects everywhere):
- Directional light (sun/outdoor light source)
- Ambient light (prevents black areas)
- Environment map (IBL diffuse + specular)

**Secondary Fill** (medium priority, local control):
- Point lights inside rooms (ceiling lamps, fixtures)
- Spot lights for dramatic accents (spotlights, street lights)
- Fill light (low-intensity light opposite key light, for face lighting)

**Tertiary Detail** (highest priority, visual feedback):
- Contact shadows (SSAO, screen-space shadows)
- Emissive materials (neon signs, fire, glowing objects)
- Baked shadows for static geometry

### 3.2 Point/Spot Light Practical Guidelines

**How many interior fill lights per room?**
- Bedroom: 1–2 point lights (ceiling + corner)
- Office: 2–3 point lights (overhead + accents)
- Warehouse: 4–6 point lights (distributed grid)
- Atrium (Sponza): 3–5 point lights (pillars, corners, upper level)

**Typical intensity values** (relative scale, no physics):
- Directional sun: 1.0 lux (baseline)
- Large ceiling lamp: 800–1500 candela (relative)
- Small wall sconce: 200–500 candela
- Ambient fill: 0.2–0.5 intensity
- SkyLight: 1.0–2.0 intensity (boost indoors)

**Shadow budget**:
- Directional light: 1 shadow map (usually 2048x2048 or higher)
- Point lights: 1–2 with shadows (cubemap = expensive)
- Spot lights: 1–2 with shadows (2D shadow map)
- Total: 3–5 shadow-casting lights per scene (culling/LOD otherwise)

---

## Part 4: UzEngine Current State Analysis

### 4.1 What UzEngine Already Has

**Component infrastructure** (all present):
- ✅ `DirectionalLightComponent` — with CSM shadow cascades
- ✅ `PointLightComponent` — with range + inverse-square falloff
- ✅ `SpotLightComponent` — with inner/outer cone angles
- ✅ `SkyLightComponent` — with intensity, IBL baking, occlusion controls

**Rendering pipeline**:
- ✅ Deferred lighting pass (reads all lights from buffer)
- ✅ CSM shadow mapping for directional light
- ✅ Cubemap shadow mapping for point lights (cubemap per light)
- ✅ Perspective shadow mapping for spot lights

**Postprocessing**:
- ✅ GTAO (ambient occlusion) — provides contact shadows
- ✅ Tone mapping + bloom
- ⚠️ TAA (temporal anti-aliasing) — can help interior stability

### 4.2 Critical Gap: No Real-Time GI

UzEngine does NOT have:
- ❌ VoxelGI analog (real-time voxel-based light propagation)
- ❌ SDFGI analog (signed distance field GI)
- ❌ Baked lightmap support (precomputed direct+indirect light)

**Why this matters for Sponza**:
- Atrium ceiling is illuminated by directional sun → full brightness
- Atrium floor is in shadow of walls + ceiling → nearly black without secondary fill
- Currently: `SkyLight.indirectLightingIntensity` is the ONLY fill for shadowed areas
- Result: Either boost SkyLight too high (washes out the scene) or interior is dark

### 4.3 Data Flow: Where Interior Fill Comes From

```
Component → Constant Buffer → Shader
────────────────────────────────────

SkyLightComponent.intensity/indirectLightingIntensity
  ↓
SceneRendering gathers SkyLight into CB
  ↓
SceneConstants.skyLight (constant buffer b2)
  ↓
Deferred lighting shader reads skyLight intensity
  ↓
PBR BRDF multiplies by irradiance map
  ↓
Fragment gets skyLight contribution
```

Current issue: `SkyLight.indirectLightingIntensity` is a single scalar that affects ALL pixels equally. It's not aware of whether a pixel is in shadow or not.

**Solution**: In shadowed areas, the skyLight contribution is the ONLY fill. To make interiors visible, either:
1. Increase `skyLight.indirectLightingIntensity` → but this also brightens outdoor areas (washed out)
2. Add point/spot lights inside buildings → local control, cheap if count-limited
3. Implement real-time GI → expensive but correct (future work)

---

## Part 5: Recommended Implementation Path for UzEngine

### Phase 1: Quick Win (Immediate, 1–2 hours)

**Goal**: Make Sponza atrium visibly correct without new systems.

**Steps**:

1. **Place 3–5 point lights inside the atrium**
   - Ceiling position (z=1200 cm): 1 light, intensity=1000, range=4000
   - Upper columns (z=800 cm): 2 lights, intensity=600 each
   - Floor near statues (z=0 cm): 1–2 lights, intensity=400 each
   - All with `castShadows=false` (expensive, not needed for fill)

2. **Tune SkyLight parameters**
   ```cpp
   skyLight.intensity = 2.0f;                    // Boost overall
   skyLight.indirectLightingIntensity = 2.5f;   // Boost diffuse fill
   skyLight.lowerHemisphereIsBlack = false;      // Use lower hemisphere color
   skyLight.lowerHemisphereColor = {0.1f, 0.15f, 0.2f};  // Warm floor bounce
   ```

3. **Verify with diagnostics**
   ```bash
   # Capture GBuffer to inspect per-pass output
   uzengine.exe --pass-dump --frames 120 --project sample/projects/sponza.uzproject --editor
   
   # Check if interior walls have reasonable brightness
   uzengine.exe --view-mode lit --screenshot-stdout --frames 8 --project ...
   ```

**Expected result**: Atrium interior should have visible separation between lit and shadowed areas, with fill lights preventing black corners.

---

### Phase 2: Light Quality (Medium, 4–8 hours)

**Goal**: Add artist control over interior lighting without new systems.

**What to implement**:

1. **Light probe system** (simple, CPU-side)
   - Define 5–10 light probe positions throughout the atrium
   - Each probe stores a "sample" color + intensity
   - At runtime, objects interpolate between nearby probes
   - Very fast (no GPU cost), works for static lighting

   ```cpp
   struct LightProbe {
       Vec3 position;      // World position (cm)
       Vec3 irradiance;    // Captured or baked SH coefficients
       float influenceRadius = 1000.0f;
   };
   
   // In renderer gathering:
   Vec3 probeInfluence = InterpolateLightProbes(pixelWorldPos, activeLightProbes);
   ```

2. **Per-light indirect intensity control**
   - Add `PointLightComponent.indirectLightingIntensity` field
   - Allows some lights to contribute more to GI than others
   - Matches UE5 semantics

3. **Emissive material support** (if not already present)
   - Allow materials to emit light (ballroom chandeliers, fire torches)
   - Emissive contribution fed into deferred buffer

**Implementation effort**: 4–6 hours total (light probes are optional for Phase 1)

---

### Phase 3: Real-Time GI (High effort, 2–3 weeks)

**Goal**: Full dynamic global illumination for interior scenes.

**Two approaches** (pick one based on codebase fit):

#### Option A: Simplified VoxelGI (Godot-style)

Implement a basic voxel-based GI system:
1. Voxelize static geometry into a 3D grid at scene load
2. Trace light bounces per frame (compute shader)
3. Inject into deferred lighting as secondary fill

**Pros**: Conceptually simple, matches Godot pattern
**Cons**: Limited to smaller scenes, requires baking-like workflow

**Implementation**: 2–3 weeks (compute shader + voxel structure + gather logic)

#### Option B: Ambient Probe Grid (Lightweight)

Instead of full GI, place a grid of `LightProbe` objects:
1. Define a 3D grid of probes (e.g., 8x4x8 = 256 probes)
2. Pre-bake irradiance at each probe (offline, once per level)
3. At runtime, objects sample nearby probes for ambient lighting

**Pros**: Very cheap, works in any engine, artist-friendly
**Cons**: Not fully dynamic (probes are static), requires offline bake

**Implementation**: 1 week (probe grid layout + baking tool + sampling in shader)

---

## Part 6: Recommendations for UzEngine Sponza Test Scene

### Immediate Actions (Today)

1. **Place 4 point lights** in the atrium (no shadows for now):
   ```cpp
   // Top center (ceiling, upper atrium)
   auto light1 = world.CreateEntity();
   auto& ptLight1 = world.AddComponent<PointLightComponent>(light1);
   ptLight1.GetWorldTransform().SetPosition({-50.0f, 1200.0f, -50.0f});
   ptLight1.color = {1.0f, 1.0f, 1.0f};
   ptLight1.intensity = 1200.0f;
   ptLight1.range = 4000.0f;
   ptLight1.castShadows = false;
   
   // Repeat for 3 more lights at different positions...
   ```

2. **Boost SkyLight**:
   ```cpp
   skyLight.intensity = 2.0f;
   skyLight.indirectLightingIntensity = 2.0f;
   skyLight.lowerHemisphereColor = {0.15f, 0.2f, 0.25f};  // Warm bounce
   skyLight.lowerHemisphereIsBlack = false;
   ```

3. **Run visual test**:
   ```bash
   cd runtime && zig build
   cd demos/sponza && zig build
   ./build/bin/sponza --screenshot-stdout --frames 8 --project ../sample/projects/sponza.uzproject --editor
   ```

4. **Capture pass-dump to diagnose**:
   ```bash
   ./build/bin/sponza --pass-dump --frames 120 --project ...
   # Inspect: lighting.hdr, gbuffer.albedo, ao.result, final.ldr
   # Check if interior has visible lighting variation
   ```

### Short-term (This Week)

1. **Implement light probe system** (simple cubic interpolation)
   - 5–10 probes placed by hand in atrium
   - Pre-baked irradiance from sky + directional light
   - Sample during GBuffer pass for early ambient contribution

2. **Add LightProbe component**:
   ```cpp
   struct LightProbeComponent {
       Vec3 position;           // World position
       Vec3 irradiance;         // Captured SH or simple color
       float influenceRadius;   // How far the probe affects
   };
   ```

3. **Update deferred lighting shader** to sample nearby probes

### Long-term (If GI is Priority)

Decide between:
- **Lightweight path**: Probe grid system (1 week, 80% of effect)
- **Full path**: VoxelGI system (2–3 weeks, 100% dynamic, but complex)

---

## Part 7: Technical Deep Dive — Light Propagation in Deferred Renderers

### 7.1 Why Deferred Shading Complicates Interior Lighting

**Forward rendering** (per-object, per-light):
```hlsl
// Direct each light onto each object
for each light:
    for each pixel in object:
        lighting += evaluateBRDF(light, pixel)
```

Interior lights naturally affect nearby pixels immediately.

**Deferred rendering** (per-pixel, lighting pass):
```hlsl
// G-Buffer pass: store material properties per-pixel
GBuffer = [albedo, normal, metallic, roughness, ...]

// Lighting pass: apply lights to G-Buffer
for each light:
    for each pixel in frustum:
        if light.range contains pixel:
            lighting += evaluateBRDF(light, pixel)
```

Interior problem: **Light culling must be correct** (point lights must find affected pixels).

### 7.2 Light Culling in UzEngine's Deferred Pass

UzEngine uses **clustered deferred lighting** (if implemented) or **fullscreen pass** (if not):

**Fullscreen approach** (simplest):
```hlsl
// lighting_pass.hlsl
for (uint lightIdx = 0; lightIdx < lightCount; ++lightIdx) {
    PointLight light = lights[lightIdx];
    
    // Distance attenuation
    float dist = length(worldPos - light.position);
    if (dist > light.range) continue;  // CRITICAL: Cull far pixels
    
    float attenuation = 1.0 - (dist / light.range);  // Linear falloff
    if (light.useInverseSquared)
        attenuation = 1.0 / (1.0 + (dist * dist));   // Inverse-square
    
    // Evaluate BRDF
    lighting += attenuation * evaluatePBR(light, GBuffer);
}
```

**Why interior lights sometimes fail**: If culling is missing or broken, interior point lights don't affect shadowed pixels.

### 7.3 Integration with SkyLight

SkyLight contribution is applied **before** dynamic lights in the lighting pass:

```hlsl
// Pseudo-code for interior pixel (in shadow)
float3 finalColor = GBuffer.albedo * skyLight.irradiance * skyLight.intensity;

// Dynamic lights add on top
for (uint lightIdx = 0; lightIdx < count; ++lightIdx) {
    float3 lightContrib = evaluateDynamicLight(lights[lightIdx], GBuffer);
    finalColor += lightContrib;
}

return finalColor;
```

**Interior pixel in CSM shadow**: `skyLight.irradiance` is the only contribution. If `skyLight.intensity` is 1.0, the pixel is dimly lit. If boosted to 2.0, it's brighter. If point lights are added, they stack on top.

---

## Part 8: Comparison Table — Three.js vs Godot vs UzEngine

| Feature | Three.js | Godot 4 | UzEngine |
|---------|----------|---------|----------|
| **Ambient Fill** | AmbientLight + HemisphereLight | DirectionalLight + Ambient | SkyLight component |
| **Local Fill Lights** | PointLight, SpotLight | OmniLight3D, SpotLight3D | PointLightComponent, SpotLightComponent |
| **Real-Time GI** | ❌ None (community libs) | ✅ VoxelGI, SDFGI | ❌ None (future work) |
| **Light Probes** | LightProbe (SH-based) | LightmapProbe grid | ❌ None (design in progress) |
| **Interior Mode** | Manual (many lights) | `SDFGI.read_sky_light=false` | Manual (SkyLight tune) |
| **Contact Shadows** | SSAO postprocess | Via GI or baked | GTAO postprocess |
| **Shadow Types** | PCF (2D, cubemap) | PCF + MSAA | PCF + VSM, cubemap |
| **Typical Setup** | Dir + Ambient + 5–10 Points | SDFGI enabled + 2–3 points | Dir + SkyLight + 3–5 points |

---

## Part 9: Key Parameters Reference

### Three.js Standard Defaults

```javascript
// AmbientLight
new THREE.AmbientLight(0xffffff, 1.0);

// HemisphereLight  
new THREE.HemisphereLight(0xb1e1ff, 0xb97316, 0.6);

// PointLight
new THREE.PointLight(0xffffff, 100.0, 2000.0, 2.0);  // color, intensity, distance, decay

// SpotLight
new THREE.SpotLight(0xffffff, 100.0, 2000.0, 45 * Math.PI/180, 0.5, 1.0);
```

### Godot 4 Standard Defaults

```gdscript
# SDFGI
env.sdfgi_enabled = true
env.sdfgi_cascades = 4              # 1–8
env.sdfgi_min_cell_size = 50.0      # cm
env.sdfgi_y_scale = 1.0             # 0.5 = 2x detail on Y
env.sdfgi_read_sky_light = true     # false for indoor only

# VoxelGI
voxel_gi.interior = false           # true for indoor only
voxel_gi.subdiv = VoxelGI.Subdiv.SUBDIV_64
voxel_gi.bias = 1.5
voxel_gi.normal_bias = 2.0
```

### UzEngine Current Defaults (from code)

```cpp
// SkyLightComponent
skyLight.intensity = 1.0f;
skyLight.indirectLightingIntensity = 1.0f;
skyLight.lightColor = {1.0f, 1.0f, 1.0f};
skyLight.lowerHemisphereIsBlack = true;
skyLight.minOcclusion = 0.0f;

// PointLightComponent
ptLight.range = 1000.0f;               // 10 meters
ptLight.intensity = 10.0f;
ptLight.useInverseSquaredFalloff = true;
ptLight.castShadows = false;

// SpotLightComponent
spotLight.innerAngleDeg = 30.0f;
spotLight.outerAngleDeg = 45.0f;
```

---

## Part 10: Visual Verification Methodology

### Step 1: Baseline Capture (Before Changes)

```bash
# Headless screenshot of current Sponza
uzengine.exe --screenshot-stdout --frames 8 \
    --project sample/projects/sponza.uzproject --editor \
    > sponza_baseline.png
```

View the screenshot — note dark interior, washed-out outdoor.

### Step 2: Apply Phase 1 Changes (Point Lights + SkyLight Boost)

Edit Sponza scene to add 4 point lights, increase `skyLight.intensity` to 2.0.

### Step 3: Capture After

```bash
uzengine.exe --screenshot-stdout --frames 8 --project ... > sponza_phase1.png
```

Compare:
- Are interior walls now visible? (should be)
- Are outdoor areas properly lit? (should not be washed out)
- Are shadows still present? (should see variation)

### Step 4: Pass-Dump Inspection

```bash
uzengine.exe --pass-dump --frames 120 --project ...
# Check INDEX.html:
# - gbuffer.albedo: Colors should be visible everywhere
# - lighting.hdr: Interior should have non-zero contribution
# - ao.result: Corners should show AO darkening (depth cues)
# - final.ldr: Balanced exposure (no blacks, no whites)
```

### Step 5: Per-Pixel Validation

```bash
# Sample pixel deep inside atrium (should not be black)
uzengine.exe --pixel-debug --pixel-debug-x 960 --pixel-debug-y 540 \
    --project ... --editor
# Check PIXEL_DEBUG_JSON: id 5–7 (HDR color RGB) should be > 0.1 in all channels
```

---

## Conclusion & Recommendations

### For Sponza Atrium Specifically

**Implement Phase 1 immediately** (today):
1. Add 4 point lights at strategic positions (ceiling, upper pillars)
2. Set `skyLight.indirectLightingIntensity = 2.0–2.5`
3. Capture and verify interior is visible
4. Commit as "fix: interior atrium lighting via point lights + sky intensity boost"

**Expected result**: Atrium should look like a properly lit interior with shadow variation and no black corners.

### For General Interior Scenes

**The three-light hierarchy always applies**:
1. **Ambient fill** (SkyLight) — affects everywhere uniformly
2. **Dynamic points/spots** (3–5 per room) — local detail
3. **Occlusion** (GTAO) — visual depth cues

This works in **all engines** and is cheap to implement.

### For Future Enhancement

**Light probe grid** (1 week):
- Implement a simple 3D grid of light probes
- Pre-bake irradiance from sky + directional + dynamic lights
- Sample during deferred lighting for better interior fill
- Beats full GI in iteration speed

**Real-time GI** (2–3 weeks, if priority):
- Decide between ambient probe grid vs VoxelGI
- Ambient probe grid is faster and art-friendly
- VoxelGI is more dynamic but complex

---

## References

### Three.js Documentation
- [AmbientLight](https://threejs.org/docs/pages/AmbientLight.html)
- [HemisphereLight](https://discoverthreejs.com/book/first-steps/ambient-lighting/)
- [LightProbe & LightProbeGenerator](https://threejs.org/docs/pages/LightProbe.html)
- [PointLight](https://threejs.org/docs/pages/PointLight.html)
- [pmndrs/postprocessing SSAOEffect](https://react-postprocessing.docs.pmnd.rs/effects/ssao)

### Godot 4 Documentation
- [Introduction to Global Illumination](https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/introduction_to_global_illumination.html)
- [Using VoxelGI](https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_voxel_gi.html)
- [Using SDFGI](https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_sdfgi.html)
- [Environment and Light in Godot](https://hexaquo.at/pages/environment-and-light-in-godot-setting-up-for-photorealistic-3d-graphics/)

### Game Development References
- [Three-Point Lighting](https://en.wikipedia.org/wiki/Three-point_lighting)
- [The Level Design Book — Lighting](https://book.leveldesignbook.com/process/lighting)
- [Unreal Engine Lighting Quick Start](https://docs.unrealengine.com/4.26/en-US/BuildingWorlds/LightingAndShadows/QuickStart)
- [Unity Lighting Manual](https://docs.unity3d.com/6000.3/Documentation/Manual/Lighting.html)
- [Radiator Blog — Lighting Theory for 3D Games](https://www.blog.radiator.debacle.us/2015/07/lighting-theory-for-3d-games-part-4.html)

### Academic Papers
- Hillaire, S. (2020). "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" — atmosphere + volumetric clouds
- Ubisoft (2015). "Screen-Space Contact Shadows" — contact shadow technique

---

**Document generated**: 2026-04-06  
**Status**: Analysis + Recommendations (Reference)  
**Next step**: Implement Phase 1 (point lights + SkyLight boost for Sponza atrium)
