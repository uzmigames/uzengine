# Data Flow: Stochastic SSR Pipeline (FidelityFX SSSR Pattern)

Updated 2026-11-19 against `3914c13c` (phase2_ssr-stochastic-refactor, item D.1). Every row below
was read from the shaders and the pass registration, not from the task ticks.

## Reference
@ref AMD 2020 "FidelityFX Stochastic Screen-Space Reflections" (MIT license)
@ref Dupuy & Benyoub 2023 "Sampling Visible GGX Normals with Spherical Caps" (the sampler in use)
@ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals"
@ref Stachowiak 2015 "Stochastic Screen-Space Reflections"
@ref McGuire 2014 "Efficient GPU Screen-Space Ray Tracing"
@ref Karis 2014 "High Quality Temporal Supersampling" (YCoCg variance clip)

## Switches

- `r.SSR` (bool, **default off**; `frame_lighting.cpp`). The quality presets turn it on at High and
  Ultra. The real-time default reflection layer is the hardware ray-traced pass
  (`r.RayTracing.Reflections`, default on where DXR exists — Phase B of the same task), which
  traces where the screen trace's confidence is low (`ReflectionNeedsRT`) or, with SSR off, every
  eligible texel (`SelectRTReflectionRoute`, `passes/raytracing/rt_reflection_pass.h`).
- Gate of the chain (`SceneRenderer::RegisterSSRPasses`, `frame/frame_gi_passes.cpp`):
  `ShowFlag::SSR` && `ctx.ssrActive` && `m_ssrHasPrevColor` (the trace reads the previous frame's
  HDR copy, so SSR starts on frame 1).
- Per-pass skips (diagnostics): `r.SSR.Trace`, `.Resolve`, `.Temporal`, `.SpatialDenoise`.
- `r.SSR.StartMipLevel` and `r.SSR.SlopeToleranceScale` reach the trace root constants (slots 14,
  15) but `ssr_trace_cs.hlsl` never reads them: no effect.
- The full CVar table is in `docs/guides/cvar-reference.md` ("SSR Parameters").

## Pipeline Overview (as registered)

```
GBuffer (depth, oct normal + metallic + roughness) + Hi-Z mips 0/1 + PrevSceneColor (last frame's HDR)
    │
    ▼
[SSR_TileClassify CS] ── 8x8 tiles; packs the pixels below MaxRoughness into a ray list,
    │                     counts them into m_ssrRayCounterArgs (groupshared + InterlockedAdd)
    ▼
[SSR_PrepareIndirect CS] ── writes DispatchIndirectArgs from the count, resets the counter
    │                        *** nothing consumes either: SSR_Trace is a direct 8x8 dispatch ***
    ▼
[SSR_Trace CS] ── per pixel: mirror reflect (roughness <= 0.04) or one GGX-VNDF sample
    │              (temporal IGN, 64-frame rotation); Hi-Z march mip 1 -> mip 0 -> 4 binary steps
    │              at full-res depth. Output: RGBA16F (hitColor.rgb linear, confidence)
    │              + R16F hit distance (bindless UAV).
    ▼
[SSR_Resolve CS] ── 15-tap Halton ellipse aligned to the screen-space reflection direction
    │                (1:1 .. 2.5:1 by roughness), normal^512 / depth edge-stopping, Karis weights
    ▼
[RT_Reflections CS]  (only on the `r.RayTracing.Reflections.Denoise` route — writes the ray texels
    │                 into the resolved buffer here so the denoiser below treats them as SSR texels)
    ▼
[SSR_Temporal CS] ── default-on `UZ_SSR_TEMPORAL_FIX` permutation: velocity px -> UV, thin-lens
    │                 virtual hit behind the surface, surface-distance disocclusion, roughness-driven
    │                 history cap 4..32 frames, 3x3 YCoCg variance clip widened by the texel's own
    │                 temporal variance (moments + lens guide textures). `TemporalFix 0` = the prior
    │                 text (velocity read as UV, virtual hit in front of the surface, blend weight
    │                 from confidence).
    ▼
[SSR_SpatialDenoise CS] ── 8-tap bilateral cleanup over the history (radius 1..3 by roughness)
    │
    ▼
 Deferred lighting reads m_ssrResolvedOutput (PerFrameCB::ssrSrvIndex) and blends it BEFORE the
 BRDF over the active reflection layer (SDF gather or prefiltered IBL) by confidence
 (block B.5 layered reflections). `r.SSR.Composite` (default 0) is the legacy additive post-TAA
 composite — it double-counts and is kept only as the A/B control.
    │
    ▼
[SSR_CopyPrevColor] ── copies the TAA output for the next frame's trace input
```

## Stage-by-Stage Bindings (what exists)

### Stage 0: Sample seeds
| Item | Where | Status |
|------|-------|--------|
| STBN 128x128x64 volume (Wolfe 2022) | — | **Not built**; `m_stbnTexture` / `m_stbnSrv` are reserved members, no loader |
| Seeds in use | `ssr_trace_cs.hlsl::GetBlueNoiseSample` | Two interleaved-gradient-noise evaluations (Jimenez 2014) with the UE5 `float2(47,17)*0.695` rotation over `frame % 64` |

### Stage 1: Tile classification (unconsumed)
| Item | Where | Status |
|------|-------|--------|
| `ssr_tile_classify_cs.hlsl` + `GetSsrTileClassify()` | postprocess/, pipeline library | Runs every SSR frame |
| `ssr_prepare_indirect_cs.hlsl` + `GetSsrPrepareIndirect()` | postprocess/, pipeline library | Runs every SSR frame |
| `m_ssrRayList`, `m_ssrRayCounterArgs` (ByteAddress: count @0, args @4..15) | `scene_temporal_resources.cpp::CreateSsrResources` | Created; written; **never read** |
| Indirect dispatch of the trace | — | **Not done**: `SSR_Trace` dispatches `(w+7)/8 x (h+7)/8` directly. Cost of the two dead passes: 0.027 ms of 0.130 ms at 1080p |

### Stage 2: Trace
| Item | Where | Status |
|------|-------|--------|
| Root constants (16 DWORDs, b0) | `SsrTraceConstants` in `frame_gi_passes.cpp` / `SsrTraceRootCB` | depth, normal, full-res depth as `hizSrvIdx`, prev colour, size, intensity, `-2/MaxRoughness`, hit-distance UAV, Hi-Z mip 0/1 SRVs + count, exposure (always 1.0), numSteps, startMip (unused), slopeTolerance (unused) |
| Hi-Z | `m_hizMipLevels[0..1]` when `m_hizMipCount >= 2` | Coarse `NumSteps/4` steps at mip 1, fine `NumSteps/2` at mip 0, binary refinement at full res; linear `NumSteps` march without a chain |
| VNDF | `common/sampling.hlsli::SampleGGX_VNDF` + Frisvad tangent frame | `alpha = roughness^2`, mirror below 0.04. C++ mirror `brdf_math.h` (11 `VNDF_Sampling` gtests in `test_brdf_parity.cpp`: known values at roughness 0.1/0.5/0.9, boundaries 0 and 1, visibility property, degenerate guards) |
| Outputs | `m_ssrTraceOutput` RGBA16F, `m_ssrHitDistance` R16F | `(hitColor, confidence)` non-premultiplied; confidence = screen fade x roughness fade x thickness fade^2 x intensity |

### Stage 3: Resolve
| Item | Where | Status |
|------|-------|--------|
| Elliptical kernel | `ssr_resolve_cs.hlsl` | 15 Halton(2,3) taps, major axis = projected reflection direction, aspect 1..2.5, radius 1..6 px |
| Karis weights | same | `rcp(1+L)` per tap, normalised by the weight sum; no inverse step (it amplified constant input) |

### Stage 4: Temporal
| Item | Where | Status |
|------|-------|--------|
| Root constants (8 DWORDs, b0) + `SsrTemporalPrevVP` at b2 (112 B) | `frame_gi_passes.cpp::RegisterSSRDenoisePasses` | prevViewProj, prevCameraPos, normal / moments / lens SRV+UAV, reset flag, ClipFloor, RoughnessFull |
| YCoCg conversion | `common/color_ycocg.hlsli` | Shared text with `test_color_math_parity.cpp` (round trip within 1e-5, known primaries, dyadic exactness) |
| History | `m_ssrHistory[2]` ping-pong, `m_ssrMoments[2]`, `m_ssrLens[2]` (fix permutation) | Lighting-epoch reset via `SsrTemporalFixEpoch` / `RealtimeHistoryReusable` |

### Stage 5: Spatial denoise
`ssr_spatial_denoise_cs.hlsl` over the just-written history into `m_ssrResolvedOutput`; mirrors (roughness < 0.04) pass through.

### Stage 6: Consumption
Deferred lighting blends `m_ssrResolvedOutput` before the BRDF (`PerFrameCB::ssrSrvIndex`, also published on the standalone RT route). TAA runs after; the SSR chain reads TAA's previous output as its colour source (`SSR_CopyPrevColor`).

## Measured (2026-11-19, RTX 5080, reflection-reference scene, 1920x1080, frames 200-299)

| | GPU ms |
|---|---|
| SSR_TileClassify / PrepareIndirect | 0.026 / 0.001 (dead work) |
| SSR_Trace / Resolve / Temporal / SpatialDenoise / CopyPrevColor | 0.028 / 0.016 / 0.030 / 0.017 / 0.011 |
| SSR total (default cutoff 0.35) | **0.130** (0.138 with `r.SSR.MaxRoughness 1`, every texel) |
| GPU frame SSR on / off | 1.80 / 1.69 |

Frame 95 of that scene with `r.SSR 1` is byte-identical run to run (static camera, deterministic
IGN); the default route (RT reflections) is not (136 silhouette pixels, max 87 codes, between two
runs of one binary).

## What remains (from the refactor plan)

1. Consume the ray list: a `[numthreads(64,1,1)]` trace variant dispatched through
   `m_ssrRayCounterArgs`, or drop the two classify passes (they cost 21 % of the SSR time for nothing).
2. STBN volume (the IGN seeds work; STBN would lower the per-frame error of the 1-spp lobe).
3. Remove or wire `r.SSR.StartMipLevel` / `r.SSR.SlopeToleranceScale`.
4. Phase 4 of the task: a single `(color, confidence, hitDistance, roughness)` output and the
   capture-probe layer of the cascade (`r.ReflectionProbes` is its own task); the RT layer and the
   confidence-based hybrid dispatch already exist.
