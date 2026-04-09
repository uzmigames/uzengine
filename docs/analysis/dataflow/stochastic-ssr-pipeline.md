# Data Flow: Stochastic SSR Pipeline (FidelityFX SSSR Pattern)

## Reference
@ref AMD 2020 "FidelityFX Stochastic Screen-Space Reflections" (MIT license)
@ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals"
@ref Stachowiak 2015 "Stochastic Screen-Space Reflections"
@ref McGuire 2014 "Efficient GPU Screen-Space Ray Tracing"

## Pipeline Overview

```
GBuffer (normal+roughness+depth) + Hi-Z + PrevSceneColor + BlueNoise
    │
    ▼
[TileClassify CS] ── 8x8 tiles, reads roughness, writes indirect args
    │
    ▼
[SSR_Trace CS] ── VNDF importance sampling + Hi-Z march (ExecuteIndirect)
    │                  Output: RGBA16F (hitColor.rgb, confidence) + R16F (hitDistance)
    ▼
[SSR_Resolve CS] ── Roughness-adaptive bilateral spatial filter (Karis HDR weight)
    │
    ▼
[SSR_Temporal CS] ── Virtual hit reprojection + YCoCg variance clip
    │                  Uses hitDistance to reconstruct world hit → reproject via prevVP
    ▼
[SSR_SpatialDenoise CS] ── Post-temporal bilateral cleanup
    │
    ▼
 ── TAA operates on IBL-only specular (no SSR contamination) ──
    │
    ▼
[SSR_Composite CS] ── Post-TAA additive blend onto TAA output
    │
    ▼
[SSR_CopyPrevColor] ── Copy TAA output for next frame's trace input
```

## Stage-by-Stage Data Flow

### Stage 0: Blue Noise Texture (one-time load)
| Item | UzEngine File | Status |
|------|---------------|--------|
| STBN 128x128x64 R8G8 volume | runtime/content/textures/stbn_128x128x64.bin | NEEDS CREATE |
| Load + RHI 3D texture + SRV | scene_renderer_init.cpp / scene_temporal_resources.cpp | NEEDS ADD |
| Member: m_stbnTexture, m_stbnSrv | scene_renderer_impl.h | NEEDS ADD |

### Stage 1: Tile Classification
| Item | UzEngine File | Status |
|------|---------------|--------|
| Shader: ssr_tile_classify_cs.hlsl | runtime/shaders/runtime/postprocess/ | NEEDS CREATE |
| PSO: GetSsrTileClassify() | pipeline_library.h + pipeline_postprocess.cpp | NEEDS ADD |
| Tile buffer (R8_UINT, w/8 × h/8) | scene_renderer_impl.h: m_ssrTileBuffer | NEEDS ADD |
| Indirect args buffer (3 uint32) | scene_renderer_impl.h: m_ssrIndirectArgs | NEEDS ADD |
| Ray counter buffer (1 uint32) | scene_renderer_impl.h: m_ssrRayCounter | NEEDS ADD |
| Ray list buffer (uint32 per pixel) | scene_renderer_impl.h: m_ssrRayList | NEEDS ADD |
| C++ dispatch in frame_gi_passes.cpp | RegisterSSRPasses() | NEEDS ADD before trace |

### Stage 2: SSR Trace (VNDF + Hi-Z)
| Item | UzEngine File | Status |
|------|---------------|--------|
| Shader: ssr_trace_cs.hlsl | runtime/shaders/runtime/postprocess/ | EXISTS — needs VNDF rewrite |
| Include: sampling.hlsli (SampleGGX_VNDF) | runtime/shaders/runtime/common/ | EXISTS |
| Input: STBN blue noise SRV | root constant: stbnSrvIdx | NEEDS ADD |
| Input: Hi-Z mip SRVs (per-mip array) | root constant: hizMipSrvIdx[N] + mipCount | NEEDS ADD |
| Input: prevVP matrix for hit distance calc | root constant or separate CB | NEEDS ADD |
| Output: RGBA16F (hitColor, confidence) | m_ssrTraceOutput | EXISTS |
| Output: R16F hitDistance | m_ssrHitDistance (new texture) | NEEDS ADD |
| CB changes: add stbnSrvIdx, hizMipCount, frameIndex | SsrTraceRootCB at b0 | NEEDS EXTEND |

### Stage 3: SSR Resolve (spatial filter)
| Item | UzEngine File | Status |
|------|---------------|--------|
| Shader: ssr_resolve_cs.hlsl | runtime/shaders/runtime/postprocess/ | EXISTS — needs kernel update |
| Roughness-adaptive elliptical kernel | ssr_resolve_cs.hlsl | NEEDS IMPLEMENT |

### Stage 4: SSR Temporal (virtual hit reprojection)
| Item | UzEngine File | Status |
|------|---------------|--------|
| Shader: ssr_temporal_cs.hlsl | runtime/shaders/runtime/postprocess/ | EXISTS — needs virtual reproject |
| Input: hitDistance SRV | root constant: hitDistanceSrvIdx | NEEDS ADD |
| Input: prevViewProj matrix | root constant CB or dedicated CB | NEEDS ADD |
| Virtual reproject: world hit → prev screen | ssr_temporal_cs.hlsl | NEEDS IMPLEMENT |
| CB changes: add hitDistanceSrvIdx, prevViewProj[16] | SsrTemporalRootCB | NEEDS EXTEND |

### Stage 5: SSR Spatial Denoise
| Item | UzEngine File | Status |
|------|---------------|--------|
| Shader: ssr_spatial_denoise_cs.hlsl | runtime/shaders/runtime/postprocess/ | EXISTS |
| Currently disabled with if(false) | frame_gi_passes.cpp:388 | NEEDS RE-ENABLE |

### Stage 6: TAA (no changes needed)
TAA operates on IBL-only specular — SSR is not blended into HDR before TAA.

### Stage 7: SSR Composite (post-TAA)
| Item | UzEngine File | Status |
|------|---------------|--------|
| Shader: ssr_composite_cs.hlsl | runtime/shaders/runtime/postprocess/ | EXISTS |
| Currently active when ctx.ssrActive | frame_postprocess.cpp:139 | EXISTS |

### Stage 8: SSR CopyPrevColor
| Item | UzEngine File | Status |
|------|---------------|--------|
| GPU copy: TAA output → m_ssrPrevSceneColor | frame_postprocess.cpp:228 | EXISTS |

## Changes Required (in dependency order)

### Sub-task 1: Blue noise texture (1 file: scene_temporal_resources.cpp + scene_renderer_impl.h)
1. Generate/embed STBN 128x128x64 R8G8 data in `runtime/content/textures/stbn_128x128x64.bin`
2. Add m_stbnTexture + m_stbnSrv to scene_renderer_impl.h
3. Load + create RHI 3D texture in CreateSsrResources()

### Sub-task 2: Hit distance texture (1 file: scene_temporal_resources.cpp + scene_renderer_impl.h)
1. Add m_ssrHitDistance + m_ssrHitDistanceSrv + m_ssrHitDistanceUav to scene_renderer_impl.h
2. Create R16F texture in CreateSsrResources(), destroy in DestroySsrResources()

### Sub-task 3: Tile classification resources (scene_renderer_impl.h + scene_temporal_resources.cpp)
1. Add m_ssrRayList, m_ssrRayCounter, m_ssrIndirectArgs, m_ssrTileClassifyOutput
2. Create buffers in CreateSsrResources()
3. Create ssr_tile_classify_cs.hlsl shader
4. Add PSO to pipeline_library

### Sub-task 4: Trace shader VNDF rewrite (1 file: ssr_trace_cs.hlsl)
1. Add STBN blue noise SRV binding
2. Replace mirror reflect(-V,N) with VNDF sampling for roughness > 0.04
3. Write hitDistance to separate R16F output
4. Extend SsrTraceRootCB with stbnSrvIdx, frameIndex, hizMipCount

### Sub-task 5: Temporal virtual reprojection (1 file: ssr_temporal_cs.hlsl)
1. Add hitDistance SRV + prevViewProj to SsrTemporalRootCB
2. Replace surface motion vector reprojection with virtual hit point reprojection
3. Keep YCoCg variance clip (already correct)

### Sub-task 6: Re-enable pipeline (1 file: frame_gi_passes.cpp)
1. Remove `if (false)` from temporal and spatial denoise blocks
2. Add tile classify dispatch before trace
3. Pass STBN SRV, hitDistance SRV/UAV to trace
4. Pass hitDistance SRV + prevViewProj to temporal
5. Flip m_ssrHistoryIndex after temporal

### Sub-task 7: Enable SSR default (1 file: frame_lighting.cpp)
1. Change `cv_SSR` default from `false` to `true`

### Sub-task 8: C++ parity test (2 files: brdf_math.h + test_brdf_parity.cpp)
1. Add SampleGGX_VNDF C++ equivalent to brdf_math.h
2. Add unit tests (known-value, boundary, hemisphere property)
