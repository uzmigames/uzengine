<!-- doc-meta: category=analysis, area=falcor, topic=postprocess-effects -->

# Falcor Post-Processing & Visual Effects Analysis

Comparing NVIDIA Falcor (GitHub: NVIDIAGameWorks/Falcor, master branch as of 2026-03-30)
against UzEngine's post-processing, atmosphere, and visual effects pipeline.

---

## 1. Render Pass Architecture

### Falcor

Falcor organizes effects as modular, pluggable **RenderPass** objects wired together at
runtime in a **RenderGraph** JSON/script. Each pass declares inputs and outputs by name;
the graph compiler resolves resource aliasing and barriers automatically. Passes are
Python-scriptable — users can swap passes at runtime without recompilation.

Key passes in `Source/RenderPasses/`:

| Pass | Purpose |
|------|---------|
| `GBuffer/GBuffer` | Rasterized G-buffer (albedo, normals, roughness, motion) |
| `GBuffer/VBuffer` | Visibility buffer — stores `{instanceID, primitiveID}` only; deferred material eval |
| `TAA` | Temporal anti-aliasing |
| `ToneMapper` | HDR→LDR with multiple operators + physical camera |
| `SimplePostFX` | Bloom, CA, vignette, barrel distortion, saturation curve |
| `SVGFPass` | Spatiotemporal Variance-Guided Filtering (RT denoiser) |
| `NRDPass` | NVIDIA Real-time Denoisers (ReLAX + ReBLUR for RT path) |
| `DLSSPass` | NVIDIA DLSS 2/3 upscaling |
| `AccumulatePass` | Frame accumulation for offline/convergence rendering |
| `ModulateIllumination` | Recombines denoised diffuse + specular with albedo |
| `PathTracer` | Full path traced GI (primary rendering path) |
| `RTXDIPass` | RTXDI direct illumination with reservoir resampling |
| `FLIPPass` | FLIP perceptual image difference metric |

### UzEngine

UzEngine uses a **render graph** with typed resource IDs and explicit pass registration
functions (e.g. `AddBloomPass`, `AddToneMapPass`). Passes are fixed C++ objects, not
runtime-scriptable. The post-process stack runs in a defined order:

```
Lighting → Cloud Composite → Sky Atmosphere → Height Fog →
Bloom (threshold→downsample→upsample→composite) →
Auto-Exposure (histogram) → TAA/TSR/FSR2 → SMAA/FXAA → Tonemap → Output
```

---

## 2. Anti-Aliasing

### Falcor TAA

**Algorithm**: Temporal AA with Catmull-Rom bicubic history filtering.

| Feature | Falcor |
|---------|--------|
| History filter | Catmull-Rom bicubic (9-tap, smooth) |
| Color space | YCgCo for variance clipping |
| Variance clipping | 3×3 neighborhood, mean ± sigma bounds |
| Anti-flicker | Brian Karis method — adaptive alpha based on distance-to-clamp |
| Motion vectors | Screen-space, external input |
| Jitter | Applied upstream; shader receives pre-jittered coords |
| DLSS integration | Dedicated `DLSSPass` — Quality / Balanced / MaxPerf |
| Blend parameter | `alpha` (0–1), user-tunable |
| Color box sigma | 0–15, user-tunable |

**DLSS** (DLSSPass):
- DLSS 2/3 via NVIDIA SDK
- Quality presets: MaxPerf / Balanced / MaxQuality
- Jitter Y-flip for Falcor's Y-up screen space
- 1×1 R32Float exposure texture input
- Sharpness control: −1.0 to 1.0
- HDR input flag

### UzEngine

| Feature | UzEngine | Gap vs Falcor |
|---------|----------|---------------|
| TAA | Yes — YCoCg neighborhood clamping, exponential moving average | Missing Catmull-Rom bicubic; uses bilinear history |
| SMAA | Yes — Jimenez 2012, three-pass (edge + blend weight + resolve) | — |
| FXAA | Yes — luminance-based edge detection | — |
| TSR | Yes — 3-pass (velocity dilation, shading rejection, YCoCg resolve) | Consolidated from reference's 12+ passes |
| FSR2 | Yes — AMD FSR2 with Quality/Balanced/Performance/Ultra presets | — |
| DLSS | Stub in UpscalerType enum (future plugin) | **MISSING**: No DLSS integration |
| XeSS | Stub in UpscalerType enum (future plugin) | **MISSING**: No XeSS integration |

**Gap**: UzEngine TAA history sampling is bilinear. Falcor uses Catmull-Rom bicubic
(9-tap), which reduces blur and ghosting on fast-moving content.

---

## 3. Tone Mapping & Color

### Falcor ToneMapper

**Operators** (enum `ToneMapperOperator`):
- Linear
- Reinhard (standard: `L/(L+1)`)
- ReinhardModified (white-point: `L*(1+L/Lmax²)/(1+L)`)
- HejiHableAlu (`(color*(6.2*color+0.5))/(color*(6.2*color+1.7)+0.06)`, γ2.2)
- HableUc2 (Uncharted 2, full parametric: A=0.22, B=0.30, C=0.10, D=0.20, E=0.01, F=0.30)
- Aces (pre-exposure factor 0.6, `(A*c²+B*c)/(C*c²+D*c+E)`, A=2.51, B=0.03, C=2.43, D=0.59, E=0.14)

**Physical camera exposure**:
- Exposure compensation (EV, −12 to +12)
- Auto-exposure via luminance mip pyramid (not histogram — uses `generateMips` + mip16 average)
- Film Speed (ISO), f-Number, Shutter Speed
- Two modes: AperturePriority, ShutterPriority
- `kExposureKey = 0.042` — standard Reinhard key value

**White balance**:
- Kelvin input (1905K – 25000K range)
- Transforms to `float3x4 colorTransform` matrix via `calculateWhiteBalanceTransformRGB_Rec709`

**Luminance weights**: Rec.601 — `dot(color, float3(0.299, 0.587, 0.114))`

**Notable absences**: No LUT/color grading table, no histogram-based exposure.

### UzEngine

| Feature | UzEngine | Gap vs Falcor |
|---------|----------|---------------|
| ACES filmic | Yes | Same math (RDO coefficients) |
| Reinhard | Not in tonemap_ps (ACES only in stack) | **MISSING**: No Reinhard, Modified Reinhard |
| Hable Uc2 | Not present | **MISSING**: No Hable/Uncharted 2 curve |
| HejiHableAlu | Not present | **MISSING**: No HejiHableAlu operator |
| Physical camera | Not present | **MISSING**: No ISO/f-Number/shutter speed inputs |
| Auto-exposure | Yes — 64-bin histogram compute, percentile filtering, asymmetric adapt | Falcor uses mip-pyramid; UzEngine histogram is **superior** |
| White balance | Not present | **MISSING**: No Kelvin white balance |
| Color grading LUT | Shader exists (`color_grading_lut_cs.hlsl`) | Present in shaders, needs C++ pass integration |
| Exposure bias | Yes (`exposureBias` in AutoExposureSettings) | — |

**Advantage**: UzEngine auto-exposure (64-bin histogram + percentile filtering + asymmetric
adaptation) is significantly more robust than Falcor's mip-pyramid approach. Histogram
correctly ignores overexposed/dark tails; mip average is skewed by sky.

**Gap**: UzEngine ToneMapper only exposes ACES. Falcor provides 6 operators including
physical camera parameters. Missing Reinhard, Hable, and physical camera model.

---

## 4. SimplePostFX Effects

### Falcor SimplePostFX

A single compute shader implementing all of:

| Effect | Algorithm | Parameters |
|--------|-----------|------------|
| Bloom | 8-level binomial pyramid downsample + additive upsample | `bloomAmount` (0–1) |
| Bloom Star | 6-lobed brute-force line sampling in 3 directions | `starAmount`, `starAngle` |
| Vignette | `max(0, 1 - r² * amount)` | `vignetteAmount` (0–1) |
| Chromatic Aberration | Per-channel scale factor (R shrinks, B expands) | `chromaticAberrationAmount` |
| Barrel Distortion | `(1 + r² * D.y) * D.x` multiplicative | `barrelDistortAmount` |
| Saturation Curve | Quadratic fit through shadow/mid/highlight saturation values | 3× float (0–2) |
| Color Offset (CDL) | ASC-CDL shadows: `col + offset` | per-channel RGB |
| Color Scale (CDL) | ASC-CDL highlights: `col * scale` | per-channel RGB |
| Color Power (CDL) | ASC-CDL midtones: `pow(max(0, col*scale+offset), power)` | per-channel RGB |
| Wipe | Blend factor to fade all effects across screen | `wipe` (0–1) |

### UzEngine

| Effect | UzEngine | Gap |
|--------|----------|-----|
| Bloom | Yes — Karis 2013 13-tap downsample + 9-tap tent upsample, 6 stages | — |
| Bloom Star | Not present | **MISSING**: No lens star artifact |
| Vignette | Not present as standalone pass | **MISSING** |
| Chromatic Aberration | Not present as standalone pass | **MISSING** |
| Barrel Distortion | Not present | **MISSING** |
| Saturation Curve | Not present | **MISSING** |
| ASC-CDL Color Grading | Shader exists (`color_grading_lut_cs.hlsl`) | Needs C++ pass |
| Distortion (generic) | Yes — `distortion_pass` + `distortion_resolve_pass` (material-driven) | Different purpose (material heat distortion, not lens) |
| Lens Flare | Yes — `lens_flare_pass` + `lensflare_cs.hlsl` | Falcor has no lens flare |
| CAS Sharpening | Yes — `cas_sharpen_ps.hlsl` | Falcor has no CAS |
| Motion Blur | Yes — tile-max + neighbor-max + per-pixel blur | Falcor has no motion blur |
| Depth of Field | Yes — setup + blur compute shaders (`dof_setup_cs`, `dof_blur_cs`) | Falcor has no DoF |
| SSR | Yes — trace + temporal + resolve (`ssr_trace_cs`, `ssr_temporal_cs`, `ssr_resolve_cs`) | Falcor has no raster SSR |

**UzEngine advantages**: Lens flare, CAS sharpening, motion blur, depth of field, and raster
SSR are all present in UzEngine and absent from Falcor's post-processing suite.

**UzEngine gaps**: Vignette, chromatic aberration, barrel distortion, saturation curve, and
lens star effects are present in Falcor SimplePostFX but absent in UzEngine.

---

## 5. Screen-Space Ambient Occlusion

### Falcor

Falcor has no dedicated SSAO/GTAO render pass in `Source/RenderPasses/`. Its primary AO
source is the path tracer's indirect diffuse bounce. For rasterized pipelines, AO comes
from the NRDPass denoiser (which handles RT shadow/AO signals).

### UzEngine

| Feature | UzEngine |
|---------|----------|
| GTAO | Yes — Jimenez 2016, horizon-based, bilateral spatial denoise |
| GTAO temporal | Yes — `gtao_temporal_cs.hlsl` + `gtao_denoise_cs.hlsl` |
| GTAO quality presets | Low (half-res, 1 angle, 4 taps) / Medium (half-res, 2 angles, 6 taps) |
| GTAO upsample | Yes — `gtao_upsample_ps.hlsl` (half-res → full-res) |
| Hi-Z pyramid | Yes — `hiz_pyramid_pass` for SSR occlusion |

**UzEngine advantage**: Rasterized GTAO is more robust than Falcor's RT-dependent AO for
rasterized pipelines. Falcor's AO only works meaningfully with path tracing.

---

## 6. Denoising (Falcor-specific, RT-oriented)

Falcor's denoising suite is aimed at real-time ray-traced content. UzEngine has no
equivalent (rasterization-only pipeline).

### Falcor NRDPass

Integrates NVIDIA Real-time Denoisers (NRD library):

| Algorithm | Signals Denoised |
|-----------|-----------------|
| **ReLAX** (default) | Diffuse radiance + hit distance, Specular radiance + hit distance |
| **ReBLUR** | Diffuse/Specular combined with bilateral radius scaling |

**ReLAX parameters**: prepass blur radius, max accumulated frames, history rejection
thresholds, edge-stopping weights (normal/luminance/roughness), a-trous iterations (2–8),
lobe angle fractions.

**ReBLUR parameters**: hit distance trimming, specular lobe trimming, antilag intensity,
adaptive radius scaling, stabilization/history-fix strength.

### Falcor SVGFPass

Implements Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering":
- Reprojection pass (temporal accumulation)
- Moment filtering (variance estimation)
- À trous wavelet filter (4 iterations)
- Final modulation (recombine with albedo)

### UzEngine

No RT denoising — rasterized pipeline. No NRD, no SVGF. This is expected given the
rendering approach difference.

---

## 7. Atmosphere & Volumetrics

### Falcor

Falcor has **no dedicated atmosphere pass**. Sky rendering is handled by the path tracer's
environment map / IBL. There is no real-time sky atmosphere system in Falcor's render passes.

Volume rendering is implemented as a generic sampling framework in `Source/Falcor/Rendering/Volumes/`:

| Component | Description |
|-----------|-------------|
| `GridVolumeSampler` | Spatially-varying volumetric data via 3D grid |
| `HomogeneousVolumeSampler` | Uniform-density volume (analytical) |
| `IPhaseFunction` / `PhaseFunction` | Henyey-Greenstein and other phase functions |

These are ray-tracing primitives used by the path tracer, not rasterized atmosphere passes.

### UzEngine

| Feature | UzEngine | Falcor Gap |
|---------|----------|-----------|
| Sky Atmosphere | Yes — Hillaire 2020 single-scatter + transmittance LUT + multi-scatter LUT | Falcor has NO atmosphere |
| Aerial Perspective Volume | Yes — 3D froxel AP volume pass | Falcor has none |
| Transmittance LUT | Yes — precomputed, 256×64 | Falcor has none |
| Multi-scatter LUT | Yes — precomputed | Falcor has none |
| Bruneton Scattering LUT | Yes — 3D scattering texture | Falcor has none |
| Irradiance LUT | Yes — 64×16 precomputed | Falcor has none |
| Height Fog | Yes — exponential height fog with alpha blend | Falcor has none |
| Volumetric Clouds | Yes — Schneider & Vos 2015 HZD model, dual-lobe HG, half-res compute | Falcor has none (RT only) |
| HDRI Skybox | Yes — `hdri_skybox_pass` | Falcor uses env maps via IBL |

**UzEngine has a massive advantage** in atmosphere and volumetrics for rasterized pipelines.

---

## 8. VFX Passes

### Falcor

No dedicated VFX pipeline. Particles are simulated via path tracing or compute, but no
structured pass system exists for particles, decals, or distortion.

### UzEngine

| Pass | Description |
|------|-------------|
| `ParticleSimulationPass` | GPU compute particle simulation |
| `ParticleRenderPass` | GPU particle billboard rendering |
| `ParticleMeshRenderPass` | Mesh-based particle rendering |
| `TrailRenderPass` | Ribbon/trail rendering |
| `DecalPass` | Deferred decal projection |
| `DistortionPass` | Material-driven screen distortion accumulation |
| `DistortionResolvePass` | Apply distortion to scene color |

**UzEngine advantage**: Full VFX pipeline. Falcor has none.

---

## 9. Upscaling Summary

| Upscaler | Falcor | UzEngine |
|----------|--------|----------|
| TAA | Yes (Catmull-Rom bicubic history) | Yes (bilinear history) |
| DLSS 2/3 | Yes (DLSSPass, full SDK) | Stub only |
| FSR1 | No | Yes (EASU + RCAS) |
| FSR2 | No | Yes (AMD FFX SDK, 4 quality presets) |
| TSR | No | Yes (3-pass: dilate, reject, resolve) |
| XeSS | No | Stub only |
| SMAA | No | Yes (Jimenez 2012, 3-pass) |
| FXAA | No | Yes |

---

## 10. Feature Matrix Summary

| Category | Feature | Falcor | UzEngine | Priority |
|----------|---------|--------|----------|----------|
| AA | TAA Catmull-Rom history | YES | NO (bilinear) | P2 |
| AA | DLSS integration | YES | stub | P1 |
| AA | SMAA | NO | YES | — |
| AA | FSR2 | NO | YES | — |
| AA | TSR | NO | YES | — |
| Tonemap | ACES | YES | YES | — |
| Tonemap | Reinhard / Modified | YES | NO | P2 |
| Tonemap | Hable Uc2 | YES | NO | P2 |
| Tonemap | HejiHableAlu | YES | NO | P3 |
| Tonemap | Physical camera (ISO/f/shutter) | YES | NO | P2 |
| Tonemap | White balance (Kelvin) | YES | NO | P2 |
| Tonemap | Histogram auto-exposure | NO | YES (superior) | — |
| Tonemap | Mip-pyramid auto-exposure | YES | NO | — |
| Color | LUT color grading | NO | partial (shader, no pass) | P1 |
| Color | ASC-CDL grading | YES | partial (shader, no pass) | P1 |
| Effects | Vignette | YES | NO | P2 |
| Effects | Chromatic Aberration | YES | NO | P2 |
| Effects | Barrel Distortion | YES | NO | P3 |
| Effects | Saturation Curve | YES | NO | P2 |
| Effects | Lens Star / Bloom Star | YES | NO | P3 |
| Effects | Lens Flare | NO | YES | — |
| Effects | CAS Sharpening | NO | YES | — |
| Effects | Motion Blur | NO | YES | — |
| Effects | Depth of Field | NO | YES | — |
| Effects | Raster SSR | NO | YES | — |
| AO | GTAO (rasterized) | NO | YES | — |
| AO | RT AO via NRD | YES | NO | P3 |
| Denoise | SVGF | YES | NO | P3 (RT only) |
| Denoise | NRD (ReLAX/ReBLUR) | YES | NO | P3 (RT only) |
| Atmosphere | Hillaire 2020 sky | NO | YES | — |
| Atmosphere | Transmittance/AP LUTs | NO | YES | — |
| Atmosphere | Height fog | NO | YES | — |
| Atmosphere | Volumetric clouds | NO | YES | — |
| VFX | Particle system (GPU) | NO | YES | — |
| VFX | Decals | NO | YES | — |
| VFX | Screen distortion | NO | YES | — |
| Architecture | Runtime-scriptable passes | YES (Python) | NO (C++ fixed) | P3 |
| Architecture | VBuffer / deferred material | YES | NO | P3 |

---

## 11. Recommended Gaps to Address

Listed by impact vs. implementation cost:

### P1 — High impact, low cost

1. **Color grading LUT pass** — shader already exists (`color_grading_lut_cs.hlsl`).
   Needs C++ pass wiring + PostProcessStackDesc integration. Exposes ASC-CDL and LUT.
2. **DLSS integration** — `UpscalerType::DLSS` stub exists. Implement `DlssUpscaler`
   using DLSS SDK following same pattern as `Fsr2Upscaler`.

### P2 — Standard industry features, moderate cost

3. **Additional tone mapping operators** — add Reinhard, Modified Reinhard, Hable Uc2
   to `tonemap_ps.hlsl`. Add operator enum to ToneMapCB. Low shader complexity.
4. **Physical camera exposure** — add ISO/f-number/shutter speed to `AutoExposureSettings`,
   compute EV = log2(shutter × f²/ISO × 100).
5. **White balance** — add Kelvin→RGB matrix to tonemap CB, apply before tone curve.
6. **Vignette + Chromatic Aberration** — add to post-process stack as single compute pass.
   Small shaders, big visual impact for cinematic content.
7. **Saturation curve (per tonal range)** — add to color grading LUT pass.
   Falcor's quadratic luma-partitioned approach is simple to port.
8. **TAA Catmull-Rom history** — replace bilinear history sampler in `taa_resolve_cs.hlsl`
   with 9-tap Catmull-Rom. Reduces temporal blur on camera cuts.

### P3 — Research renderer features (not rasterizer priority)

9. **Barrel distortion** — lens simulation for VR or cinematic rendering.
10. **Lens star bloom** — 6-lobed star via 3-direction line sampling (Falcor's approach).
11. **RT denoising (NRD/SVGF)** — only relevant if a path-tracing mode is added.
12. **VBuffer** — visibility buffer for GPU-driven rendering (far future).

---

## 12. Falcor-Specific Techniques Not Applicable to UzEngine

These Falcor features are specific to its path-traced architecture and not applicable
to UzEngine's rasterized deferred pipeline:

- **AccumulatePass** — offline convergence accumulation (path tracer only)
- **ModulateIllumination** — NRD-denoised diffuse/specular recombination
- **RTXDIPass** — RTXDI reservoir-based direct illumination (DXR required)
- **SVGFPass** / **NRDPass** — noisy RT signal denoising
- **WARDiffPathTracer** — differentiable path tracer (research)
- **BSDFOptimizer** — material fitting (offline tool)
- **FLIPPass** — perceptual image comparison metric (QA tool)
- **PathTracer** — full path tracing (different rendering paradigm)

---

*Sources: NVIDIAGameWorks/Falcor master branch (2026-03-30), UzEngine runtime source.*
