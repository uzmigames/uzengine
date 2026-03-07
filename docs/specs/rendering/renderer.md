# Renderer Spec

## Overview

UzEngine uses **DirectX 12** as the primary render API behind an RHI abstraction layer
(`rhi.h`). The render pipeline is **Forward** with a single directional light, PBR materials,
shadow mapping, IBL, and a multi-pass post-process stack. The renderer outputs frames via the
JPEG+WebSocket streaming pipeline for display in the VSCode viewport.

All rendering code lives in `src/engine/renderer/`. No DX12 types appear above the RHI layer
(Rule RHI-1).

---

## Pipeline Overview (Actual Implementation)

```
Per Frame:
────────────────────────────────────────────────────────────────────
CPU:
  1. Scene Sync         — collect proxies, sync SkyAtmosphere, light
  2. Light Collect      — light_collect_primary() from scene Sun node
  3. IBL Dirty Check    — regenerate cubemaps if SkyAtmosphere changed

GPU (via RenderGraph):
  Pass 0: Shadow Depth    — depth-only from light perspective (2048x2048)
  Pass 1: Scene HDR       — sky + PBR meshes into R11G11B10_FLOAT RT
  Pass 2: SSAO            — 12-sample hemisphere, half-res
  Pass 3: SSAO Blur       — bilateral blur (8-tap)
  Pass 4: Bloom Down      — 5-mip progressive downsample
  Pass 5: Bloom Up        — additive upsample chain
  Pass 6: Composite       — HDR + SSAO + Bloom → ACES tonemap → backbuffer
  Pass 7: Editor Overlays — grid, gizmos, selection outlines (stencil)
  Pass 8: Readback        — copy backbuffer → CPU staging buffer

CPU:
  9. JPEG Encode        — libjpeg-turbo (quality=85, 4:2:0)
 10. WebSocket Send     — binary frame to VSCode extension
```

---

## Frame Graph (Simplified)

```
[Scene Proxies]
     │
     ▼
[Light Collect] ──▶ LightState (direction, color, intensity, cameraPos)
     │
     ├──▶ [Pass 0: Shadow Depth]  ShadowMap (2048x2048, D32_FLOAT)
     │
     ├──▶ [Pass 1: Scene HDR]     HDR RT (R11G11B10_FLOAT) + Depth (D32_FLOAT)
     │       │── Sky (procedural gradient + sun disk)
     │       └── PBR Meshes (Cook-Torrance + IBL + POM + shadows)
     │
     ├──▶ [Pass 2: SSAO]          SSAO RT (R8_UNORM, half-res)
     ├──▶ [Pass 3: SSAO Blur]     SSAO Blur RT (R8_UNORM, half-res)
     │
     ├──▶ [Pass 4: Bloom Down]    Bloom mip chain (5 mips, R11G11B10_FLOAT)
     ├──▶ [Pass 5: Bloom Up]      Additive upsample through mip chain
     │
     ├──▶ [Pass 6: Composite]     Backbuffer (RGBA8_UNORM)
     │       └── ACES tonemap + gamma + bloom blend + SSAO multiply
     │
     ├──▶ [Pass 7: Editor]        Backbuffer (selection outline, grid)
     │
     └──▶ [Pass 8: Readback]      → JPEG Encode → WebSocket
```

---

## Root Signature Layout

The root signature uses 14 parameters totaling **64/64 DWORDs** (completely full).

| Slot | Register | Type | DWORDs | Contents |
|------|----------|------|--------|----------|
| 0 | `b0` | Root Constants | 16 | World matrix (4x4 float) |
| 1 | `b1` | Root Constants | 20 | MaterialCB (albedo, metallic, roughness, ao, emission, parallaxScale, normalStrength, etc.) |
| 2 | `b2` | Root CBV | 2 | LightFrameCB (direction, color, intensity, shadow VP, cameraPos) |
| 3 | `b3` | Root Constants | 16 | ViewProjection matrix (4x4 float) |
| 4 | `t0` | Descriptor Table | 1 | Albedo texture |
| 5 | `t1` | Descriptor Table | 1 | Normal texture |
| 6 | `t2` | Descriptor Table | 1 | Metallic texture |
| 7 | `t3` | Descriptor Table | 1 | Roughness texture |
| 8 | `t4` | Descriptor Table | 1 | AO texture |
| 9 | `t5` | Descriptor Table | 1 | Displacement texture |
| 10 | `t6` | Descriptor Table | 1 | Shadow map |
| 11 | `t7` | Descriptor Table | 1 | Irradiance cubemap (IBL) |
| 12 | `t8` | Descriptor Table | 1 | Prefiltered specular cubemap (IBL) |
| 13 | `t9` | Descriptor Table | 1 | BRDF LUT (IBL) |

**Budget**: 16 + 20 + 2 + 16 + 10×1 = **64 DWORDs** (max allowed by D3D12).
No new SRV slots or root constants can be added without restructuring (see phase3 prerequisites).

---

## Material System (PBR)

### Material File Format (`.uzmat`)

Materials use an INI-like format parsed by `MaterialManager` (`material.cpp`).
Values are auto-classified by the parser:

- **Texture**: contains `.png`, `.jpg`, or `.dds` extension → stored in `textures` map
- **Vector**: contains `[` bracket → parsed as comma-separated floats
- **Scalar**: everything else → parsed as `float`

```ini
[material]
name = StoneBricks
shader = pbr

; --- Scalar PBR parameters (populate MaterialCB root constants) ---
metallic = 0.0
roughness = 1.0
ao = 1.0

; --- Vector parameters ---
albedo = [1.0, 1.0, 1.0]

; --- Texture slots (resolved to RHI handles at load time) ---
albedo_texture = assets/textures/Stonebricks/stonebricks_albedo.jpg
normal_texture = assets/textures/Stonebricks/stonebricks_normal.jpg
roughness_texture = assets/textures/Stonebricks/stonebricks_roughness.jpg
ao_texture = assets/textures/Stonebricks/stonebricks_ao.jpg
displacement_texture = assets/textures/Stonebricks/stonebricks_disp.jpg

; --- POM displacement ---
parallax_scale = 0.05

; --- Per-material shader parameters ---
normal_strength = 1.3
self_shadow_strength = 1.0
cavity_strength = 0.4
uv_tiling = 3.0
```

### Supported Material Keys

| Key | Type | Default | Register | Description |
|-----|------|---------|----------|-------------|
| `name` | string | — | — | Display name |
| `shader` | string | `pbr` | — | Shader to use |
| `albedo` | vec3 | `[1,1,1]` | b1 | Base color multiplier (linear) |
| `metallic` | float | `0.0` | b1 | 0=dielectric, 1=metal |
| `roughness` | float | `0.5` | b1 | 0=mirror, 1=fully rough |
| `ao` | float | `1.0` | b1 | Ambient occlusion multiplier |
| `emission` | vec3 | `[0,0,0]` | b1 | Emissive color (HDR) |
| `emission_energy` | float | `0.0` | b1 | Emission intensity multiplier |
| `parallax_scale` | float | `0.0` | b1 | POM height scale (0 = disabled) |
| `normal_strength` | float | `1.0` | b1 | Normal map intensity |
| `self_shadow_strength` | float | `0.5` | b1 | POM self-shadow intensity |
| `cavity_strength` | float | `0.3` | b1 | POM cavity darkening |
| `env_multiplier` | float | `1.0` | b1 | IBL environment contribution |
| `specular_multiplier` | float | `1.0` | b1 | Specular highlight intensity |
| `uv_tiling` | float/vec2 | `1.0` | b1 | UV repeat factor |
| `albedo_texture` | path | white | t0 | Base color map |
| `normal_texture` | path | flat blue | t1 | Tangent-space normal map |
| `metallic_texture` | path | white | t2 | Metallic map |
| `roughness_texture` | path | white | t3 | Roughness map |
| `ao_texture` | path | white | t4 | Ambient occlusion map |
| `displacement_texture` | path | black | t5 | Height map for POM |

### Fallback Textures

When a material omits a texture slot, a 1x1 fallback is bound:

| Slot | Fallback Color | Rationale |
|------|---------------|-----------|
| albedo (t0) | white (1,1,1,1) | Tinted by albedo vec3 parameter |
| normal (t1) | flat blue (0.5, 0.5, 1.0, 1.0) | No perturbation |
| metallic (t2) | white (1,1,1,1) | Controlled by scalar metallic param |
| roughness (t3) | white (1,1,1,1) | Controlled by scalar roughness param |
| ao (t4) | white (1,1,1,1) | No occlusion |
| displacement (t5) | black (0,0,0,0) | No offset (POM disabled) |

### MaterialCB Layout (b1, 20 DWORDs)

```
DWORD  0-2:  albedo (float3)
DWORD  3:    metallic (float)
DWORD  4:    roughness (float)
DWORD  5:    ao (float)
DWORD  6:    parallaxScale (float)
DWORD  7:    normalStrength (float)
DWORD  8-10: emission (float3)
DWORD 11:    emissionEnergy (float)
DWORD 12:    selfShadowStrength (float)
DWORD 13:    cavityStrength (float)
DWORD 14:    envMultiplier (float)
DWORD 15:    specularMultiplier (float)
DWORD 16-17: uvTiling (float2)
DWORD 18-19: _pad (reserved)
```

### BRDF Model

The PBR shader (`pbr.ps.hlsl`) implements:

- **Distribution**: GGX (Trowbridge-Reitz) NDF
- **Geometry**: UE5 Vis_SmithJointApprox (height-correlated)
- **Fresnel**: Schlick approximation with F0 from metallic workflow
- **Diffuse**: Burley (Disney) diffuse (roughness-dependent, replaces Lambertian)
- **IBL**: Split-sum approximation (irradiance + prefiltered specular + BRDF LUT)
- **Multi-scatter**: Fdez-Aguera energy compensation term
- **POM**: Steep parallax (8-32 layers by NdotV) + linear interpolation + cavity + self-shadow

---

## Image-Based Lighting (IBL)

Hybrid pipeline: CPU generates the source environment cubemap, GPU compute shaders
perform the convolution (with CPU fallback if compute init fails).

1. **Environment Cubemap** (256x256 per face) — CPU procedural sky from SkyAtmosphere params (`sky_cubemap.cpp`)
2. **BRDF LUT** (256x256, RG16F) — GPU compute: `ibl_brdf_lut.cs.hlsl`, 1024 Hammersley samples (`IblBaker::bake_brdf_lut()`)
3. **Irradiance Cubemap** (32x32 per face, RGBA16F) — GPU compute: `ibl_irradiance.cs.hlsl`, 1024 cosine-weighted hemisphere samples
4. **Prefiltered Specular** (128x128, 5 mip levels, RGBA16F) — GPU compute: `ibl_prefilter.cs.hlsl`, 256 GGX importance samples per mip

### Compute Pipeline

`IblBaker` (`ibl_baker.cpp`) manages 3 compute PSOs with a dedicated compute root signature
(separate from the graphics root signature, no DWORD budget conflict):

| Param | Register | Type | Contents |
|-------|----------|------|----------|
| 0 | `b0` | Root Constants (16 DWORDs) | Shader parameters (size, roughness, etc.) |
| 1 | `t0` | Descriptor Table | Source environment cubemap (SRV) |
| 2 | `t1` | Descriptor Table | Secondary SRV (reserved) |
| 3 | `u0` | Descriptor Table | Output UAV (per-mip) |
| 4 | `u1` | Descriptor Table | Secondary UAV (reserved) |

Per-mip UAV descriptors are allocated via a bump allocator in the CBV/SRV/UAV heap
(region 4096+), enabling the prefilter shader to write each mip level independently.

IBL cubemaps are regenerated when SkyAtmosphere parameters change (dirty-flag tracking in
`Renderer::update()`). Parameters compared: `sky_zenith`, `sky_horizon`, `sky_ground`, sun
direction (epsilon 1e-4f per channel).

---

## Shadow System

Single directional light shadow map:

| Property | Value |
|----------|-------|
| Resolution | 2048x2048 |
| Format | D32_FLOAT |
| Filter | 16-tap Poisson disk PCF |
| Penumbra | Distance-based (blocker search) |
| Bias | Depth bias + slope-scaled bias |
| Self-shadow | POM heightmap self-shadow (Jimenez 2016) |

The shadow view-projection matrix is computed from the directional light's direction,
fitting the scene AABB. Bound to `t6` for sampling in `pbr.ps.hlsl`.

---

## Post-Process Stack (Implemented)

| Pass | Effect | Method | Shader |
|------|--------|--------|--------|
| 2-3 | SSAO | 12-sample hemisphere + bilateral blur | `ssao.ps.hlsl`, `ssao_blur.ps.hlsl` |
| 4-5 | Bloom | 5-mip downsample + additive upsample | `bloom_down.ps.hlsl`, `bloom_up.ps.hlsl` |
| 6 | Composite | ACES tonemap + gamma 2.2 + bloom blend + SSAO | `composite.ps.hlsl` |

All post-process passes use a fullscreen triangle (3 vertices, no VB) via `fullscreen.vs.hlsl`.
Exposure is currently hardcoded to 1.0 in the composite pass.

---

## Streaming (JPEG → WebSocket)

```
DX12 Readback Buffer (RGBA8, render resolution)
    ↓
RingBuffer<FrameData, 8>  (lock-free SPSC)
    ↓
Encode Thread: libjpeg-turbo  (quality=85, 4:2:0, FastDCT)
    ↓
WebSocket Broadcast (binary frame = raw JPEG bytes)
    ↓
VSCode Webview: <canvas> drawImage on blob URL
```

---

## Planned Features (Phase 3+)

The following are documented in `.rulebook/tasks/phase3-ultra-realistic-rendering/` and are
**not yet implemented**:

- **TAA** — Temporal Anti-Aliasing (Halton jitter, velocity buffer, history clamping)
- **GTAO** — Ground Truth AO (replaces current hemisphere SSAO)
- **Contact Shadows** — screen-space ray march along light direction
- **Auto Exposure** — luminance histogram compute shader
- **SSR** — Screen-Space Reflections (Hi-Z ray march)
- **CSM** — Cascaded Shadow Maps (4 cascades, replaces single shadow map)
- **Point/Spot Lights** — clustered forward light culling
- **Volumetric Fog** — froxel-based inscattering
- **Depth of Field** — CoC-based blur
- **Color Grading** — 3D LUT
- **SSS** — Subsurface Scattering (separable screen-space)
- **Anisotropic Materials** — directional GGX
- **Area Lights** — Linearly Transformed Cosines (Heitz 2016)
- **Decals** — deferred projection into GBuffer (requires deferred pipeline)
- **Reflection Probes** — localized cubemap captures

See the phase3 task's `proposal.md` for full details on each feature.
