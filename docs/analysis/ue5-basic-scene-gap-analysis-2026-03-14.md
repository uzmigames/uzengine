# Gap Analysis: UzEngine vs UE5 Basic Scene
## Date: 2026-03-14

---

## Executive Summary

UzEngine has completed the foundational layer of a deferred PBR renderer: RHI abstraction (DX12),
render graph (DAG), GBuffer (4 targets), CSM shadows (4 cascades), deferred lighting (64 point
lights), IBL (BRDF LUT + irradiance + prefiltered specular), and a post-process stack (ACES
tonemap + pyramid bloom + FXAA). Nine working demos confirm the pipeline is functional. The
existing visual baseline is comparable to a "solid but rough" early-access renderer: surfaces are
physically correct, shadows exist, and IBL provides plausible ambient lighting. At default
settings, a UE5 Basic Scene produces noticeably smoother, more cinematic output — primarily
because of TAA (no aliasing), SSAO (perceived depth), a physically-modelled sky atmosphere
(Bruneton), auto-exposure (correct exposure in any lighting), and a high-quality bloom pyramid.

Closing the gap to UE5 default scene quality requires implementing approximately 14 additional
rendering systems. Of those, 12 have existing Rulebook tasks. The two highest-impact missing
systems that lack tasks are exponential height fog and reflection probes. Architecturally,
UzEngine's deferred pipeline is structurally sound, but the CPU-side light culling, fixed
point-light shadow limit (8 lights), and scattered environment state will become bottlenecks
before Phase 3 work is complete. These are documented as architecture risks in this report.

This analysis uses the UE5 5.6 default template as the target baseline — default settings, no
Lumen, no Nanite, no ray tracing. The objective is visual parity at equivalent scene complexity,
not feature-for-feature API parity.

---

## Feature Matrix

| Feature | UE5 Basic Scene | UzEngine Status | Gap | Severity | Task Exists? |
|---------|----------------|-----------------|-----|----------|--------------|
| GBuffer layout (4 targets) | GBufferA–F (6 targets, flexible slots) | 4 targets: albedo+AO, oct-normal+metal+rough, depth, velocity | Shading model ID missing; no GBufferD custom data slot | P1 | Yes — 3.26.0-gbuffer-optimization (85%) |
| Shading model ID in GBuffer | 4-bit field per pixel | Not stored | Cannot support non-PBR shading models | P1 | Yes — 3.34.0-shading-models |
| Base pass (GBuffer write) | Depth prepass + full GBuffer | Present and functional | Minor: no depth prepass; depth implicit | P2 | No |
| Directional light + CSM | 4 cascades, 2048×2048, PCF 16-tap | 4 cascades, 2048×2048, Poisson-disk PCF | At parity | — | Complete |
| Sky light (IBL) | Cubemap + SH irradiance + prefiltered specular | BRDF LUT + irradiance + prefiltered (5 mips) | At parity | — | Complete |
| Deferred lighting pass | Per-light volumes, BRDF composition | Deferred, 64 point lights | At parity | — | Complete |
| PBR BRDF (Cook-Torrance GGX) | GGX NDF + Smith G + Fresnel-Schlick | Present (GGX + Burley diffuse) | Minor BRDF deviation from UE5 spec | P2 | Yes — 3.27.0-pbr-brdf-ue5-parity |
| Temporal anti-aliasing (TAA) | TAA default (TSR on high-end) | FXAA only | Significant aliasing; edges jagged in motion | P0 | Yes — 3.14.0-temporal-anti-aliasing |
| Screen-space ambient occlusion | HBAO+ or GTAO | Not implemented | Surfaces look floating; no crevice shadowing | P0 | Yes — 3.15.0-gtao-ambient-occlusion |
| Sky atmosphere (Bruneton) | Precomputed LUT sky model | Procedural gradient with sun disk | Flat, unrealistic sky; no atmospheric scattering | P1 | Yes — 3.8.0-sky-atmosphere |
| Exponential height fog | Per-vertex exponential fog | Not implemented | No atmospheric depth; far objects fully visible | P1 | No |
| Tone mapping (ACES) | ACES RRT + ODT + 3D LUT color grading | ACES tonemap present | Color grading LUT not integrated | P2 | Yes — 3.20.0-color-grading |
| Bloom | Gaussian pyramid (Q5: 5 mips) | Pyramid bloom present | Quality and threshold calibration vs UE5 | P2 | Yes — 3.28.0-bloom-pyramid-upgrade |
| Auto-exposure | Temporal luminance histogram | Not implemented | Scenes are incorrectly exposed in varied lighting | P1 | Yes — 3.19.0-auto-exposure |
| Color grading (3D LUT) | 32×32×32 or 64×64×64 LUT | Not implemented | No artistic color correction capability | P2 | Yes — 3.20.0-color-grading |
| Contact shadows | Screen-space ray march toward light | Not implemented | Hard shadow edges at contact points | P2 | Yes — 3.29.0-contact-shadows |
| Point/spot light shadows | VSM or dual-paraboloid + perspective | Architecture exists; hard limit of 8 | Fixed limit; no atlas; lights silently unshadowed | P1 | Yes — 3.30.0-point-spot-light-shadows |
| Reflection probes (local IBL) | Cubemap array, influence radius, blend | Not implemented | All reflections uniform from sky IBL | P2 | No |
| Screen-space reflections (SSR) | Hi-Z SSR with fade | Not implemented | Metallic/wet surfaces show only sky reflection | P2 | Yes — 3.16.0-screen-space-reflections |
| Depth of field | Circle of confusion, separable blur | Not implemented | All depths equally sharp (no cinematic focus) | P3 | Yes — 3.17.0-depth-of-field |
| Motion blur | Per-object velocity vector blur | Not implemented | Objects in motion leave no blur trail | P3 | Yes — 3.18.0-motion-blur |
| Film grain | Halton-sequence procedural noise | Not implemented | Output looks digitally clean (minor) | P3 | Yes — 3.21.0-film-grain |
| Vignette | Radial edge darkening | Not implemented | Flat edges (minor) | P3 | Yes — 3.22.0-vignette |
| Chromatic aberration | Lateral color fringing | Not implemented | No lens-style aberration (minor) | P3 | Yes — 3.23.0-chromatic-aberration |
| Lens flare | Occlusion-tested lens artifact | Not implemented | No lens flare on bright sources (minor) | P3 | Yes — 3.24.0-lens-flare |
| PostProcess volumes | Per-region parameter overrides | Not implemented | Cannot vary exposure/bloom per zone | P2 | Yes — 3.25.0-postprocess-volumes |
| Clustered forward+ lighting | GPU tile/cluster culling | CPU frustum culling | Scales poorly beyond 32 visible lights | P1 | Yes — 3.32.0-clustered-forward-plus |
| Material system expansion | Multiple shading models (skin, hair, cloth) | Single PBR model | Cannot render non-PBR materials | P2 | Yes — 3.31.0-material-system-expansion |
| Area lights (LTC) | Sphere, rect, tube lights | Point/spot/directional only | No area light shapes | P3 | Yes — 3.33.0-area-lights-ltc |
| Subsurface scattering | Screen-space Gaussian blur + profile | Not implemented | Skin/organic materials look hard | P3 | Yes — 3.34.0-shading-models |
| LOD system | Screen-coverage LOD selection | Not implemented | All geometry at full detail at any distance | P2 | No |
| GPU instancing | Structured buffer + SV_InstanceID | Not implemented | Each identical object is a separate draw call | P2 | No |
| Shader variants (feature mask) | Permutation system | Single uber-shader | Unused texture paths waste shader budget | P2 | Yes — 3.12.0-material-format |
| Depth prepass | Explicit depth-only render pass | Implicit (depth test in GBuffer pass) | Missing early-Z rejection; fillrate waste | P2 | No |
| Frustum culling | Per-object frustum test | Present (CPU) | Present; needs GPU driven upgrade eventually | P3 | No |
| Volumetric clouds | Froxel-based ray marching | Not implemented | Empty sky (UE5 default includes cloud layer) | P3 | No |
| Lumen GI | Real-time hierarchical GI | Not planned (out of scope) | No bounced indirect light | Out of scope | No |
| Nanite | Virtual geometry | Not planned (out of scope) | Triangle-limited meshes | Out of scope | No |

---

## Detailed Gap Analysis

### 1. Rendering Pipeline

**UE5 Pipeline**: Depth prepass → GBuffer (6 flexible targets) → Decals → Lighting composition
→ SSAO → Translucency → Post-processing → UI.

**UzEngine Pipeline**: GBuffer (4 targets, no depth prepass) → Deferred lighting → Forward
transparent → Bloom → Tonemap → FXAA.

**Key Gaps**:

1. No explicit depth prepass. UzEngine writes depth implicitly in the GBuffer pass. An explicit
   prepass enables early-Z rejection, which eliminates overdraw in the GBuffer pass. On complex
   scenes this costs 5–10% CPU (extra draw commands) but saves 20–40% GPU fillrate. UE5 always
   runs a depth prepass before GBuffer. Priority: P2.

2. GBuffer lacks a shading model ID slot. UE5 stores a 4-bit shading model ID per pixel to
   route pixels through different BRDF paths (PBR, subsurface, hair, clear coat) in the lighting
   pass without shader permutations. Without this slot, supporting non-PBR materials requires
   shader permutations that multiply pass count. The 3.26.0-gbuffer-optimization task covers
   this, and is 85% complete. Priority: P1.

3. No GBufferD slot for custom material data. UE5's GBufferD carries arbitrary per-material
   parameters (clear coat roughness, subsurface profile index, etc.). UzEngine's fixed 4-target
   layout cannot represent these without a fifth render target. Priority: P2.

**Godot alternative**: Godot 4 uses forward clustering, not deferred GBuffer, which eliminates
the shading model ID problem entirely — each draw has its own shader variant. For UzEngine this
is architecturally viable but would require abandoning the existing deferred pipeline. The
current deferred path is the right choice at this scale; adding the shading model ID slot to the
GBuffer is sufficient.

---

### 2. Lighting System

**UE5**: IBL sky light (cubemap array + SH irradiance), directional light (CSM), point/spot
lights (deferred volumes), reflection captures (local probes), clustered culling.

**UzEngine**: IBL sky light (compute-baked IBL), directional light (CSM), point/spot lights
(CPU-culled, deferred pass, 64-light cap), no reflection probes, no clustered culling.

**Key Gaps**:

1. CPU-side light culling. UzEngine tests lights against camera frustum on the CPU each frame.
   For scenes with 16–32 lights this is acceptable (~0.1 ms). Beyond 64 visible lights the cost
   grows linearly and the 64-light constant buffer cap becomes a hard block. Task
   3.32.0-clustered-forward-plus addresses this with GPU tile assignment. Godot uses a compute
   shader (16×16 tiles) that runs in ~0.3 ms for 256 lights. Priority: P1.

2. No reflection probes. All IBL comes from a single global sky cubemap. Local environment
   variation (indoor reflections, puddles, metallic interior surfaces) requires placement-based
   reflection captures. UE5 uses a TextureCubeArray with distance-based blending. No Rulebook
   task exists for this. Priority: P2 (must be created).

3. Auto-exposure absent. Scene exposure is a constant multiplier. UE5 builds a 64-bucket
   luminance histogram per frame and adjusts exposure over time (configurable ramp speed). Without
   it, scenes with strong contrast (a cave exit, a sunset) are incorrectly exposed.
   3.19.0-auto-exposure covers this. Priority: P1.

---

### 3. Shadow System

**UE5**: CSM (4 cascades, logarithmic splits, PCF 16-tap), point light dual-paraboloid shadows,
spot light perspective shadows, virtual shadow maps (default in 5.4+), contact shadows
(screen-space).

**UzEngine**: CSM (4 cascades, Poisson-disk PCF) at parity. Point/spot light shadows: code
exists but hard-limited to 8 lights in a per-light texture allocation (no atlas). Contact
shadows: not implemented.

**Key Gaps**:

1. Point/spot shadow hard limit of 8. The current implementation allocates one texture per
   light. When the 9th shadowing local light is added it silently fails. Task
   3.30.0-point-spot-light-shadows should adopt Godot's shadow atlas pattern: a single 2K×2K
   texture divided into 4 quadrants (1/4/16/64 slots each), dynamically reallocated. This
   handles 32+ shadowing lights without failure. Priority: P1.

2. Contact shadows absent. Contact shadows add crevice and fold detail at shadow boundaries,
   complementing CSM. They are screen-space only (no additional textures). The algorithm is a
   4–8 step depth-difference march toward the directional light in NDC space. Inexpensive.
   3.29.0-contact-shadows covers this. Priority: P2.

3. CSM is at parity. The 4-cascade setup with 2048×2048 maps and Poisson-disk PCF matches UE5
   default settings. No gap here.

---

### 4. Post-Processing Pipeline

**UE5 post-process order**: Eye adaptation → Bloom → TAA → Depth of Field → Motion Blur →
Color Grading LUT → Tonemap → Film Grain → Vignette → Chromatic Aberration.

**UzEngine post-process order**: Bloom (pyramid) → Tonemap (ACES) → FXAA.

**Key Gaps by priority**:

**P0 — TAA** (task 3.14.0): FXAA is a spatial-only filter that blurs edges. TAA accumulates
samples over time by jittering the projection matrix (Halton sequence) and reprojecting history
using velocity vectors. Without TAA, all in-motion geometry shimmers and edges are jagged.
Dependencies: velocity buffer (already present in GBuffer), history framebuffer (new allocation).

**P1 — Auto-exposure** (task 3.19.0): As described under Lighting. A luminance histogram compute
pass feeds an eye adaptation buffer that drives the tonemap's exposure multiplier.

**P2 — Color grading LUT** (task 3.20.0): ACES tonemap is present but the 3D LUT stage that
applies user color correction is absent. A 32×32×32 RGBA16F LUT is sampled after ACES to apply
per-studio or per-scene color grading. This must be integrated before TAA, not after, to avoid
color drift on history blend.

**P2 — Bloom pyramid upgrade** (task 3.28.0): UzEngine has pyramid bloom. The task is to
calibrate threshold, mip count (target 7 mips matching UE5 Q5), and intensity to match UE5's
default output. Godot's glow system uses additive accumulation of 7 half-res mips; this is the
reference for parameter tuning.

**P2 — Contact shadows** (task 3.29.0): Described above.

**P2 — PostProcess volumes** (task 3.25.0): Axis-aligned or sphere-shaped volumes that blend
to per-zone exposure, bloom, tonemap, and DOF settings. Critical for scene-authoring flexibility.

**P3 — DOF** (task 3.17.0): Separable circle-of-confusion blur. Godot uses a dithered approach
with two-pass Gaussian. UE5 uses a tiled DOF with half-resolution foreground/background
separation. Godot's simpler approach achieves similar visual results for default-quality settings.

**P3 — Motion blur** (task 3.18.0): Per-object velocity-based blur using the velocity GBuffer
channel (already written). Requires a gather-based blur kernel per pixel weighted by screen-space
velocity magnitude.

**P3 — Film grain, Vignette, Chromatic aberration, Lens flare** (tasks 3.21–3.24):
Polish-layer effects. Film grain: Halton-sequence noise per frame. Vignette: radial power curve
applied in screenspace. Chromatic aberration: offset UV sample in RGB channels. Lens flare:
occlusion-tested screen-space sprites. Low implementation cost. Can be added as a single
"screen effects" pass.

---

### 5. Sky and Atmosphere

**UE5**: Bruneton et al. physical sky model with four precomputed LUTs (transmittance 64×256,
multi-scatter 32×32×32, sky view 192×104, aerial perspective volume). Default scene uses
SkyAtmosphere component enabled.

**UzEngine**: Procedural gradient sky with sun disk. No atmospheric scattering. No Rayleigh
blue shift at horizon. No aerial perspective (fog does not increase with viewing angle through
atmosphere).

**Key Gaps**:

1. Sky atmosphere model (task 3.8.0): Bruneton precomputed tables are the standard approach.
   The four LUTs are generated once on startup (each < 1 ms on modern GPU) and sampled every
   frame. Sky view LUT (192×104) is regenerated each frame when the sun moves. Implementation
   reference: Hillaire 2020 (A Scalable and Production Ready Sky and Atmosphere Rendering
   Technique) provides a clean-room formulation.

2. Aerial perspective volume: A 3D texture aligned to the view frustum, populated by the
   atmosphere compute, gives distance-based inscattering. This is what makes distant mountains
   look hazy. Without it, fog is uniform (exponential height only).

3. Exponential height fog (no task exists): Even without a full Bruneton sky, exponential
   height fog is a cheap improvement over no fog at all. Formula: `fog = exp(-(height −
   cameraHeight) × density)`. Should be added as a simple pass applied to scene color after
   deferred lighting. A new task must be created for this. Priority: P1.

---

### 6. Material System

**UE5**: PBR (Cook-Torrance GGX) as default. Additional shading models via 4-bit GBuffer slot:
Subsurface (skin), Cloth, Hair, Clear Coat, Two Sided Foliage, Eye. Material parameters:
BaseColor, Metallic, Roughness, Specular, AO, Emissive, Normal, ClearCoat, Anisotropy,
SubsurfaceColor, Opacity, CustomData.

**UzEngine**: Single PBR model (GGX + Burley diffuse). Parameters: albedo, normal, metallic,
roughness, AO, displacement (POM). Emission per-material. No shading model switching.

**Key Gaps**:

1. Shading model routing (task 3.34.0): Requires GBuffer shading model ID slot first (3.26.0).
   After that, the lighting pass branches on the stored ID to apply different BRDF paths. Minimum
   for UE5 parity: DefaultLit (0) and Unlit (1). Subsurface (2) and Clear Coat (3) are P3.

2. Material system expansion (task 3.31.0): Clear coat layer, anisotropy, thin translucency.
   These parameters must be packed into GBufferD.

3. Shader variants (task 3.12.0): The current uber-shader samples all texture slots regardless
   of material configuration. A feature-mask variant cache (as in Godot's StandardMaterial3D)
   should only compile paths for active texture slots, reducing register pressure and instruction
   count.

4. PBR BRDF calibration (task 3.27.0): UE5 uses a specific Burley diffuse normalization, a
   specific GGX NDF form, and a specific Smith G2 correlation. Minor but visible differences in
   metallic roughness midtones. The task is to align UzEngine's BRDF constants and LUT to UE5's
   published values (Brian Karis, 2013).

---

### 7. Environment and Reflections

**UE5**: SkyLight provides global IBL. Reflection Capture components (sphere and box) override
IBL locally. SSR extends reflections with real-time screen-space data. SkyLight supports cloud
shadow and DFAO occlusion.

**UzEngine**: SkyLight IBL present (compute-baked, 5 mip levels). No reflection probes.
No SSR. SkyLight AO parameter exists (struct) but DFAO not computed.

**Key Gaps**:

1. Reflection probes: No Rulebook task exists. A probe stores a 128×128 cubemap (6 faces, 7
   mips for roughness) at a world position with an influence radius. During the lighting pass,
   the probe nearest to the pixel is sampled in place of the sky cubemap for the specular IBL
   term. UE5 stores all probes in a TextureCubeArray; for UzEngine a simpler approach is to
   store 4–8 probes as individual textures indexed in the lighting constant buffer. Priority: P2.
   A new task must be created.

2. SSR (task 3.16.0): Screen-space reflections trace view-space rays through the depth buffer.
   The Hi-Z depth pyramid (mipped depth) accelerates traversal by allowing long-range marching
   at lower resolution. SSR output is blended with probe/IBL specular based on confidence (ray
   hit distance, fresnel angle). Dependency: requires depth pyramid pass.

3. IBL at parity for global sky: The GPU-compute IblBaker (BRDF LUT 256×256 RG16F, irradiance
   32×32 cubemap, prefiltered 128×128 5-mip specular) matches UE5's default IBL quality.

---

### 8. Mesh and Scene

**UE5**: Automatic LOD selection by screen coverage, GPU instancing via GPU scene structured
buffers, frustum culling (CPU + optional GPU HZB), glTF + FBX import, meshoptimizer.

**UzEngine**: No LOD, no GPU instancing, CPU frustum culling only, mesh loading functional.

**Key Gaps**:

1. LOD system: No Rulebook task exists. Screen-coverage LOD selection requires: per-mesh LOD
   levels stored as separate index buffers, bounding sphere in screen space evaluated per mesh,
   integer LOD selection clamped by view distance bias. Dithered cross-fade avoids popping.
   Priority: P2. A new task must be created.

2. GPU instancing: No Rulebook task exists. For UzEngine the minimum implementation is a
   StructuredBuffer<float4x4> of instance transforms uploaded per draw, indexed by SV_InstanceID
   in the vertex shader. Requires: per-mesh instance list in scene, instanced PSO variant, batch
   grouping by (mesh, material). Priority: P2. A new task must be created.

3. Depth prepass: No task exists. Should be added as a companion to the GBuffer optimization
   work (3.26.0). Priority: P2.

---

## Implementation Priority (Dependency-Aware Order)

### Wave 1 — Foundation (unblocks everything else)

These must complete before any Wave 2 work begins:

1. **GBuffer optimization complete** (task 3.26.0 — 85% done): Add shading model ID slot and
   GBufferD. Unblocks shading models, material expansion, SSR, contact shadows.

2. **Depth prepass** (no task): Add explicit depth-only pre-pass before GBuffer. Unblocks
   contact shadows and Hi-Z pyramid. Dependency: none.

3. **Depth pyramid / Hi-Z** (no separate task): Compute-shader mipped depth buffer built after
   depth prepass. Unblocks SSR and SSAO.

### Wave 2 — Core Visual Quality (largest impact on UE5 parity)

These produce the biggest perceptual delta. Implement in this order to avoid wasted effort:

4. **TAA** (task 3.14.0): Dependency — velocity buffer (exists), history framebuffer. The
   single highest-impact post-process addition. Eliminates shimmer and jagged edges.

5. **GTAO / SSAO** (task 3.15.0): Dependency — depth pyramid. Adds perceived depth and
   crevice shadowing. Dramatically improves scene grounding.

6. **Sky atmosphere** (task 3.8.0): Dependency — none (new compute passes). Replaces flat
   gradient with physically-modelled sky.

7. **Auto-exposure** (task 3.19.0): Dependency — TAA history (needed for temporal luminance
   smoothing). Correct exposure in varied lighting.

8. **Exponential height fog** (no task — must create): Dependency — none. Fast, high-impact.
   Add before atmosphere so both can be calibrated together.

9. **Point/spot light shadows with atlas** (task 3.30.0): Dependency — none. Replaces fixed
   8-light limit with dynamic atlas.

### Wave 3 — Polish and Refinement

10. **Clustered forward+ lighting** (task 3.32.0): Dependency — none. GPU-driven light culling.
    Required before shipping scenes with >64 lights.

11. **Color grading LUT + PostProcess volumes** (tasks 3.20.0 + 3.25.0): Dependency — ACES
    already present. Add 3D LUT sampling and per-volume parameter blending.

12. **PBR BRDF calibration** (task 3.27.0): Dependency — GBuffer complete. Align UzEngine BRDF
    constants to UE5 reference values.

13. **Contact shadows** (task 3.29.0): Dependency — depth buffer. Short screen-space march.

14. **Reflection probes** (no task — must create): Dependency — IBL complete (exists). Local
    cubemap captures blended into specular IBL term.

15. **SSR** (task 3.16.0): Dependency — depth pyramid, reflection probes (fallback). Hi-Z
    screen-space reflection.

16. **Bloom pyramid calibration** (task 3.28.0): Dependency — none. Tune threshold and mip
    count to match UE5 default.

17. **Shader variant cache** (task 3.12.0 scope): Dependency — material format work. Compile
    only texture paths in use.

18. **Material system expansion** (task 3.31.0): Dependency — GBufferD slot (3.26.0). Clear
    coat, anisotropy, thin translucency.

19. **Shading models** (task 3.34.0): Dependency — 3.31.0, shading model ID in GBuffer.

### Wave 4 — Advanced Features

20. **DOF** (task 3.17.0): Cinematic focus separation. Dependency — depth buffer.

21. **Motion blur** (task 3.18.0): Dependency — velocity buffer (exists).

22. **LOD system** (no task — must create): Dependency — mesh format stable (3.11.0).

23. **GPU instancing** (no task — must create): Dependency — none (vertex shader change only).

24. **Screen effects** (tasks 3.21.0–3.24.0): Film grain, vignette, chromatic aberration, lens
    flare. Low cost; can be implemented as a single pass.

25. **Area lights LTC** (task 3.33.0): Dependency — BRDF calibration complete.

26. **Subsurface scattering** (task 3.34.0 scope): Dependency — shading model ID in GBuffer.

27. **Depth prepass** + LOD together (efficiency win).

28. **Volumetric clouds** (no task): Optional atmosphere enhancement. Dependency — sky
    atmosphere complete.

---

## Task Coverage Analysis

| Gap | Existing Task | Status | Notes |
|-----|--------------|--------|-------|
| GBuffer shading model ID + GBufferD | 3.26.0-gbuffer-optimization | In progress (85%) | Scope confirmed to include shading model ID |
| Temporal anti-aliasing | 3.14.0-temporal-anti-aliasing | Exists | Depends on velocity buffer (present) |
| GTAO / SSAO | 3.15.0-gtao-ambient-occlusion | Exists | Scope should include Hi-Z pyramid prerequisite |
| Sky atmosphere (Bruneton) | 3.8.0-sky-atmosphere | Exists | Clean-room Hillaire 2020 formulation acceptable |
| Auto-exposure | 3.19.0-auto-exposure | Exists | Must integrate with TAA history for temporal smoothing |
| Exponential height fog | None | Missing | Must create new task |
| Point/spot light shadow atlas | 3.30.0-point-spot-light-shadows | Exists | Scope must specify atlas not per-light textures |
| Clustered forward+ | 3.32.0-clustered-forward-plus | Exists | GPU compute 16×16 tiles |
| Color grading LUT | 3.20.0-color-grading | Exists | 3D LUT sampling in tonemap pass |
| PostProcess volumes | 3.25.0-postprocess-volumes | Exists | Parameter interpolation per frustum volume |
| PBR BRDF calibration | 3.27.0-pbr-brdf-ue5-parity | Exists | Karis 2013 reference values |
| Contact shadows | 3.29.0-contact-shadows | Exists | Screen-space only |
| Reflection probes | None | Missing | Must create new task |
| SSR | 3.16.0-screen-space-reflections | Exists | Dependency: Hi-Z pyramid |
| Bloom calibration | 3.28.0-bloom-pyramid-upgrade | Exists | 7 mips, threshold calibration |
| Material expansion (clear coat, etc.) | 3.31.0-material-system-expansion | Exists | Needs GBufferD first |
| Shading models | 3.34.0-shading-models | Exists | Needs shading model ID in GBuffer |
| DOF | 3.17.0-depth-of-field | Exists | Godot separable COC approach acceptable |
| Motion blur | 3.18.0-motion-blur | Exists | Velocity buffer present |
| Film grain | 3.21.0-film-grain | Exists | Trivial after TAA |
| Vignette | 3.22.0-vignette | Exists | Trivial screen-space pass |
| Chromatic aberration | 3.23.0-chromatic-aberration | Exists | Trivial UV offset |
| Lens flare | 3.24.0-lens-flare | Exists | Occlusion-tested sprites |
| Shader variants | 3.12.0-material-format | Exists | Feature bitmask cache |
| Area lights LTC | 3.33.0-area-lights-ltc | Exists | After BRDF calibration |
| LOD system | None | Missing | Must create new task |
| GPU instancing | None | Missing | Must create new task |
| Depth prepass | None | Missing | Must create new task |
| Subsurface scattering | 3.34.0-shading-models (partial) | Exists (scoped in) | Verify scope covers full SSS blur |
| Volumetric clouds | No current task | Missing | Low priority; after sky atmosphere |

---

## Missing Tasks

The following gaps have no Rulebook task and must be created before the relevant wave begins:

1. **Exponential height fog** — P1. Add before sky atmosphere work so both can be calibrated
   together. Algorithm: per-vertex or per-pixel exponential decay by altitude + camera distance.
   Simple fog color from atmosphere inscattering color. One to two days effort.

2. **Reflection probes** — P2. Spherical capture volumes, TextureCubeArray storage (or 4–8
   individual cubemaps), influence radius blend in lighting pass. One to two weeks effort.

3. **Depth prepass** — P2. Explicit depth-only render pass before GBuffer. Unblocks early-Z
   rejection and Hi-Z pyramid for SSR/SSAO. One to two days effort; can be created as a subtask
   of 3.26.0-gbuffer-optimization or standalone.

4. **Hi-Z depth pyramid** — P2. Compute shader that generates a mipped depth buffer after
   depth prepass. Unblocks SSR (task 3.16.0) and can accelerate SSAO. One day effort.

5. **LOD system** — P2. Screen-coverage LOD selection, per-mesh LOD level index buffers,
   dithered cross-fade. Two to three weeks effort.

6. **GPU instancing** — P2. StructuredBuffer<float4x4> instance transforms, SV_InstanceID
   indexing in vertex shader, batch grouping by (mesh, material). One week effort.

---

## Architecture Risks

The following architectural decisions in the current codebase could block future features or
require significant refactoring:

### Risk 1: Deferred Pipeline Lock-In

UzEngine is committed to a deferred GBuffer pipeline. This is sound for its scale target, but
creates two downstream costs:

- **Transparency** requires a separate forward pass (already the case; acceptable).
- **MSAA** is incompatible with standard deferred rendering. UE5 uses TAA as the only AA on
  deferred targets; UzEngine must do the same. Do not add MSAA as a feature — it will not work
  correctly with the deferred pipeline.

Mitigation: Document explicitly in `docs/architecture/render-pipeline.md` that MSAA is
unsupported by design. TAA is the only AA path.

### Risk 2: CPU-Side Light Culling

The current deferred lighting pass iterates visible lights on CPU, uploads a constant buffer,
and issues a draw call per light. With task 3.32.0-clustered-forward-plus, this will be replaced
by GPU tile assignment. However, task 3.32.0's name implies a forward rendering change; the
implementation must apply to the current deferred pipeline (tile-based deferred, not full
forward+). Scope must be clarified to avoid an unnecessary pipeline switch.

### Risk 3: Fixed 8-Light Shadow Limit

The ShadowManager's `point_light_shadows[8]` array allocates one texture per light. Silent
failure (return RID_INVALID) when the limit is exceeded means scenes can lose shadows without
any indication. Task 3.30.0 must adopt the Godot shadow atlas pattern (described in detail
in `docs/analysis/godot-architecture-decisions.md` — Decision 4) to eliminate this limit.

### Risk 4: Root Signature Saturation

The graphics root signature is documented as FULL (14 params, 64/64 DWORDs). Adding GBufferD,
reflection probe arrays, and per-volume post-process UBOs will require new descriptor table
slots. Plan a root signature audit before Wave 2 work begins. Options: move per-material
parameters to a dynamic bindless table (task in phase 2: bindless-resources), or consolidate
per-pass descriptor tables.

### Risk 5: Scattered SceneEnvironment State

SkyLight, Fog, PostProcessStack, and Tonemapper are separate objects with no unified versioning.
When multiple systems read these per-frame, cache coherence and dirty-flag granularity are
suboptimal. The Godot architecture decision (Decision 3 in
`docs/analysis/godot-architecture-decisions.md`) recommends a single `SceneEnvironment` struct
with a `version` field. This consolidation should happen before PostProcess volumes (Wave 3) to
avoid complex merge work later.

### Risk 6: No Depth Prepass Means No Hi-Z Pyramid

Contact shadows, SSAO, and SSR all benefit from or require a mipped depth buffer. Without an
explicit depth prepass, the Hi-Z pyramid cannot be built from a clean state. This is a
one-time architecture addition that unblocks three Phase 3 tasks.

---

## Recommendations

### Priority 1 — Complete GBuffer Optimization (3.26.0)

Task is 85% done. Completing it (adding shading model ID and GBufferD) is the immediate
unblocking action. All shading model, material expansion, and SSR work stalls until this is
done.

### Priority 2 — TAA Before Any Other Post-Process

TAA (3.14.0) is the single highest visual-quality addition in the backlog. Every other
post-process addition (DOF, motion blur, color grading) is more visible after TAA removes the
underlying shimmer. TAA also requires the velocity buffer, which is already written in the
GBuffer pass. Implement TAA immediately after 3.26.0.

### Priority 3 — Add Missing Foundation Tasks

Create four new Rulebook tasks before Wave 2 begins:
- Exponential height fog (P1)
- Depth prepass + Hi-Z pyramid (P2, can be one task)
- Reflection probes (P2)
- LOD system (P2)
- GPU instancing (P2)

### Priority 4 — SSAO and Sky Atmosphere in Parallel

Once TAA is in, GTAO/SSAO (3.15.0) and sky atmosphere (3.8.0) are the next two biggest visual
gaps. They are independent of each other and can be implemented in parallel by two engineers.
SSAO requires the Hi-Z pyramid; sky atmosphere requires only new compute passes.

### Quick Wins to Defer

Vignette (3.22.0), film grain (3.21.0), chromatic aberration (3.23.0), and lens flare (3.24.0)
are each one to two days of work but produce minimal perceptual improvement compared to SSAO or
TAA. Defer to Wave 4. Do not allow these to compete for time against Wave 2 work.

### Things to Defer Indefinitely

Lumen GI, Nanite, and Virtual Shadow Maps are UE5-specific complexity that is out of scope for
UzEngine's mid-to-large game target without a dedicated team. Volumetric clouds are a late
Phase 3 / Phase 4 addition only after sky atmosphere is shipping and stable. Do not create
tasks for these in the Phase 3 backlog.

---

*End of Gap Analysis*
