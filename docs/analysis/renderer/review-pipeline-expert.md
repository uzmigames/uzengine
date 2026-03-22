<!-- doc-meta
title: Pipeline Expert Review — Render Pass Order, Resource States, and Missing Stages
description: Render graph engineer's review of the consensus analysis and strategic plan, based on reading the actual C++ pipeline code
author: render-graph-engineer
date: 2026-03-22
-->

# Pipeline Expert Review

**Reviewer**: render-graph-engineer (render pipeline / render graph specialist)
**Reviewed documents**: `consensus-analysis.md`, `strategic-render-plan.md`
**Code read**: `scene_render_frame.cpp`, `frame_setup.cpp`, `frame_geometry.cpp`, `frame_lighting.cpp`, `frame_postprocess.cpp`, `frame_overlay.cpp`, `frame_shadows.cpp`, `scene_lifecycle.cpp`, `deferred_lighting_ps.hlsl`, `contact_shadows.hlsli`

---

## 1. Pipeline Order Validation

### Actual Pass Registration Order (from `scene_render_frame.cpp:91-95`)

```
SetupFrameData()            → Camera, CBs, draw list, instancing, auto-exposure
  ↓
RegisterShadowPasses()      → CSM two-pass (static cache + dynamic), local light shadows
  ↓
RegisterGeometryPasses()    → DepthPrepass → HiZ Pyramid → [GPU Occlusion Cull] → GBuffer → Velocity
  ↓
RegisterLightingPasses()    → GTAO → DFS_CullObjects → DFS_ConeTrace → SSGI → GDF_Composite →
                              RadianceCache_Trace → SSR_Trace → SSR_Resolve →
                              DeferredLighting → SkyAtmosphere → HeightFog → CloudComposite
  ↓
RegisterPostProcessPasses() → Bloom → LensFlare → TAA/TSR → ColorGradingLUT →
                              MotionBlur (TileMax → NeighborMax → Gather) → DOF (Setup → Blur) →
                              Tonemap → CAS → FXAA
  ↓
RegisterOverlayPasses()     → EditorGrid + StatsOverlay
```

**Verdict: Pass order is CORRECT.** The pipeline follows the standard deferred rendering order:
1. Shadow depth first (no scene color dependency)
2. Geometry (depth prepass enables Hi-Z for occlusion and SSR)
3. Lighting compute passes (GTAO, DFS, SSGI, SSR) before deferred shading
4. Deferred lighting consumes all compute results
5. Atmosphere and fog render into HDR after lighting (correct — they need the lit scene)
6. Post-process operates on HDR scene color
7. Tonemap converts HDR→LDR
8. LDR passes (CAS, FXAA) operate on tonemapped output

### One Pipeline Order Issue Found

**Clouds execute OUTSIDE the render graph** (`scene_render_frame.cpp:62-88`). The volumetric cloud compute dispatch runs on a **dedicated command list** with a `WaitIdle()` barrier before the render graph even compiles. The cloud composite pass inside the render graph then reads the cloud output SRV from the previous frame's dispatch.

This is architecturally questionable but functionally correct for now — the `WaitIdle()` serializes the GPU, and the one-frame latency is masked by temporal reprojection. However, it means:
- Cloud dispatch cannot overlap with shadow/geometry passes (perf loss)
- `WaitIdle()` is a full GPU drain — extremely expensive
- The cloud composite reads stale data on the first frame after enabling clouds

**Recommendation**: Move cloud compute into the render graph as a proper pass (between SSGI and SSR, or as an async compute pass). This eliminates the `WaitIdle()` and enables overlap with geometry rendering.

---

## 2. Resource State Issues

### 2.1 Manual Barriers Inside Render Graph Callbacks — Pervasive Pattern

The render graph (`RgBuilder`) tracks resource states via `pb.Read()` / `pb.Write()` declarations. However, **many passes perform manual `cmd->ResourceBarrier()` calls inside their execution callbacks**:

- **TAA/TSR pass** (`frame_postprocess.cpp:148-161`): Manual barriers on `m_taaHistory[writeIdx]` (SRV→UAV) and `m_taaHistory[readIdx]` (UAV→SRV), plus a post-dispatch barrier back to SRV.
- **Motion blur passes** (`frame_postprocess.cpp:316-396`): Manual SRV↔UAV transitions on `m_motionBlurTileMax`, `m_motionBlurNeighborMax`, `m_motionBlurOutput`.
- **DOF passes** (`frame_postprocess.cpp:409-467`): Manual SRV↔UAV transitions on `m_dofCocTexture`, `m_dofOutputTexture`.
- **Color grading LUT** (`frame_postprocess.cpp:222-283`): Manual SRV↔UAV transitions on `m_colorGradingLut`.
- **SSR trace/resolve** (`frame_lighting.cpp:589-728`): Manual SRV↔UAV transitions on `m_ssrTraceOutput`, `m_ssrResolvedOutput`.
- **DFS cone trace** (`frame_lighting.cpp:415-421`): Manual UAV→SRV transition on `m_dfsOutputTexture`.

**Risk**: These manual barriers bypass the render graph's state tracking. If the render graph inserts its own transitions based on `pb.Read()`/`pb.Write()` declarations, there could be **redundant barriers** (perf waste) or **conflicting transitions** (the resource is already in the target state).

**Assessment**: This is currently safe because these textures (TAA history, motion blur intermediates, SSR outputs, DFS output) are **not imported into the render graph** via `rgBuilder.ImportTexture()`. They are internal textures managed outside the graph, accessed directly by SRV/UAV indices inside callbacks. The render graph only tracks resources it knows about (GBuffer, HDR, shadow atlas, backbuffer). So there is no conflict — but it does mean the render graph cannot optimize barrier placement for these resources.

**This is technical debt, not a bug.** The correct fix is to import all intermediate textures into the render graph and let the compiler handle state transitions.

### 2.2 GTAO Texture State — The Micro-Shadow Blocker

The consensus analysis identifies micro-shadows as disabled (`deferred_lighting_ps.hlsl:1038-1040`) due to a "GTAO texture state" concern. After reading the pipeline:

- GTAO executes as a compute pass in `RegisterLightingPasses()` (line 47-94)
- GTAO internally manages its own textures and returns an SRV index via `m_gtaoPass->GetFilteredAOSrvIndex()`
- The SRV index is written to `perFrame.gtaoSrvIndex` in `frame_setup.cpp:361`
- The deferred lighting shader reads `gtaoSrvIndex` and samples the GTAO texture

**The GTAO texture state is NOT the problem.** The GTAO pass completes before the deferred lighting pass runs (correct pipeline order: GTAO → DFS → SSGI → SSR → DeferredLighting). The filtered AO output should be in SRV state by the time deferred lighting samples it.

**Root cause hypothesis**: The micro-shadow code was likely disabled during a debugging session when GTAO was broken or the SRV index was stale, and the TODO was never revisited. The fix is to:
1. Verify GTAO output is in SRV state after the GTAO pass (check inside `GTAOPass::Execute()`)
2. Re-enable the micro-shadow code
3. Test visually

This is a 30-minute fix, not a 2-4 hour investigation.

---

## 3. SSR Status — CONSENSUS DOCUMENT IS WRONG

**The consensus analysis says SSR is MISSING. This is incorrect. SSR is FULLY IMPLEMENTED.**

Evidence from the actual code:

1. **Shaders exist**: `runtime/shaders/runtime/postprocess/ssr_trace_cs.hlsl` and `ssr_resolve_cs.hlsl` are present with compiled `.gen.h` headers.
2. **Pipeline objects exist**: `PipelineLibrary::GetSsrTrace()` and `GetSsrResolve()` in `pipeline_postprocess.cpp:548-608`, warmed up at startup (`pipeline_lifecycle.cpp:342-343`).
3. **Compute passes are registered**: `frame_lighting.cpp:575-731` registers `SSR_Trace` and `SSR_Resolve` passes when `ctx.ssrActive` is true.
4. **SSR is integrated into deferred lighting**: `deferred_lighting_ps.hlsl:1229-1234` samples the SSR texture when `ssrSrvIndex != 0xFFFFFFFF` and blends it with IBL specular.
5. **CVar defaults to ON**: `cv_SSR = UZ_CVAR_BOOL("r.SSR", true, ...)` in `frame_postprocess.cpp:24`.
6. **Activation condition** (`frame_setup.cpp:783-789`):
   ```cpp
   ctx.ssrActive = m_ssrEnabled              // CVar r.SSR = true
                 && m_ssrTraceOutput.IsValid()  // texture created at init
                 && m_ssrResolvedOutput.IsValid()
                 && m_hdrHasContent             // HDR has content (false on frame 0)
                 && m_hizMipCount > 0           // Hi-Z pyramid built
                 && m_shadowDebugMode == 0      // not in debug grid mode
                 && m_passInspector.IsPassEnabled("ssr");
   ```

**SSR runs every frame on the default path** (bindless DX12, not SM5). It traces against the depth buffer using the Hi-Z pyramid, resolves with Fresnel weighting, and the result is composited in the deferred lighting shader.

### SSR Limitations (not mentioned in consensus)

1. **No true Hi-Z acceleration**: `frame_lighting.cpp:617-621` — the HiZ SRV is set to `m_gbuffer.depthSRV` (single mip), not the actual Hi-Z pyramid mip chain. The trace shader may not benefit from hierarchical acceleration. This makes SSR slower than it needs to be but functionally correct.
2. **MaxRoughness = 0.4**: `roughnessMaskScale = -2.0f / 0.4f` — SSR is only active for surfaces with roughness < 0.4. This is aggressive; UE5 defaults to maxRoughness = 0.8. Surfaces with 0.4 < roughness < 0.8 get no SSR, falling back to IBL probes only.
3. **No temporal accumulation in SSR resolve**: The resolve pass does not blend with history. Each frame's SSR is computed independently, which means noisy results on rough surfaces.

---

## 4. TAA/TSR Status — IS IT WORKING?

**TAA/TSR is WORKING.** Evidence:

1. **Halton jitter is generated**: `frame_setup.cpp:201-206` generates Halton(2,3) jitter with an 8-sample sequence when `ctx.taaActive` is true.
2. **Jitter is applied to the projection matrix**: `frame_setup.cpp:217-224` offsets `jitteredViewProj.m.m[2][0]` and `[2][1]` by the sub-pixel jitter amount.
3. **Upscaler dispatch**: `frame_postprocess.cpp:132-197` dispatches the active upscaler (TAA or TSR) with history ping-pong, motion vectors, and jitter parameters.
4. **Activation condition** (`frame_setup.cpp:195-199`):
   ```cpp
   ctx.taaActive = m_taaEnabled              // CVar r.TAA = true
                && m_taaHistory[0].IsValid()  // history textures created
                && m_taaHistory[1].IsValid()
                && m_device->IsBindless()     // requires bindless (compute)
                && m_passInspector.IsPassEnabled("taa");
   ```

**TAA is NOT "header-only" or "minimal" as the consensus claims.** It has full Halton jitter, projection matrix offset, history ping-pong, and delegates to an `ITemporalUpscaler` interface that supports both TAA and TSR implementations.

### TAA Limitation

The consensus is correct that TAA requires bindless mode (`m_device->IsBindless()`). On SM5 (non-bindless) paths, TAA is disabled and the pipeline falls back to FXAA. This is a real limitation for the SM5 fallback path.

---

## 5. Missing Pipeline Stages

### Actually Missing (confirmed by code reading)

| Feature | Status | Evidence |
|---------|--------|----------|
| **Froxel Volumetric Fog** | MISSING — height fog only | `frame_lighting.cpp:886` calls `AddHeightFogFromECS()`, no froxel system |
| **DOF** | IMPLEMENTED but disabled by default | `cv_DOF = UZ_CVAR_BOOL("r.DOF", false, ...)`, full implementation exists with CoC setup + blur |
| **Subsurface Scattering** | MISSING | No SSS pass, no SSS material flag in GBuffer |
| **Anisotropic BRDF** | MISSING | No tangent buffer in GBuffer, no anisotropy parameter |
| **Box Reflection Probes** | MISSING — sphere only | `m_reflectionProbes.Update()` exists but no box projection code found |
| **GPU Occlusion Culling** | STUB | `frame_geometry.cpp:86-88` — gated behind `m_gpuOcclusionCullingEnabled (default: false)`, bounding sphere data not uploaded |

### NOT Missing (consensus was wrong)

| Feature | Status | Evidence |
|---------|--------|----------|
| **SSR** | IMPLEMENTED and active | Full trace + resolve compute passes, integrated in deferred lighting |
| **TAA** | IMPLEMENTED and active | Halton jitter, history ping-pong, upscaler interface |
| **Motion Blur** | IMPLEMENTED and active | 3-pass (tile max, neighbor max, gather) |
| **DOF** | IMPLEMENTED | Full CoC + blur, just disabled by default (`r.DOF = false`) |
| **Color Grading LUT** | IMPLEMENTED | 32^3 3D LUT with full per-range controls |
| **Auto-Exposure** | IMPLEMENTED | GPU histogram-based, runs in `SetupFrameData()` |

---

## 6. TOP 3 Pipeline-Level Recommendations

### Recommendation 1: Fix the Micro-Shadow TODO (30 minutes, HIGH impact)

The disabled micro-shadow at `deferred_lighting_ps.hlsl:1038-1040` is the single highest visual impact fix that requires the least work. The GTAO texture is in the correct state — the TODO is stale. Re-enable the micro-shadow code, verify visually, done.

### Recommendation 2: Fix SSR Hi-Z Mip Chain Usage (2-4 hours, MEDIUM impact)

SSR trace currently uses the raw depth buffer as its "Hi-Z" source (`frame_lighting.cpp:621: rc.hizSrvIdx = m_gbuffer.depthSRV`). The Hi-Z pyramid is built (`frame_geometry.cpp:62-76`) but not wired to SSR. Connecting the actual Hi-Z mip chain to the SSR trace shader would:
- Enable hierarchical ray marching (skip empty space faster)
- Allow more ray steps without perf cost
- Improve hit rate on distant reflections

Also raise `MaxRoughness` from 0.4 to 0.6-0.8 to match reference engine behavior.

### Recommendation 3: Move Cloud Compute Into the Render Graph (4-8 hours, MEDIUM-HIGH impact)

The `WaitIdle()` between cloud dispatch and the main render graph is a full GPU drain that serializes all work. Moving cloud compute into the render graph (or at minimum onto an async compute queue) would:
- Eliminate the GPU bubble (perf gain: potentially 1-2ms)
- Allow cloud dispatch to overlap with shadow rendering
- Remove the one-frame latency hack
- Clean up the architectural split between "inside render graph" and "outside render graph" compute

---

## 7. Corrections to the Consensus Analysis

The consensus analysis contains several factual errors that must be corrected before the team acts on them:

| Claim in Consensus | Reality |
|---------------------|---------|
| "SSR is MISSING" | SSR is fully implemented: 2 compute shaders, integrated in deferred lighting, default ON |
| "SSR is the SINGLE LARGEST MISSING FEATURE" | SSR is present. The largest actual gap is froxel volumetric fog or subsurface scattering |
| "TAA is header-only with no jitter generation" | TAA has full Halton(2,3) jitter, projection matrix offset, history ping-pong, and upscaler interface |
| "TAA minimal" | TAA/TSR is a full implementation behind an ITemporalUpscaler interface |
| "DOF disabled" | DOF is implemented but disabled by default (CVar `r.DOF = false`). Just flip the default or set at runtime. |

The consensus is correct about:
- Exposure 1.4 being too high (confirmed: `cv_Exposure = 1.4f` at `frame_setup.cpp:52`)
- SkyLight Intensity 3.5 being too high (confirmed: `cv_SkyLightIntensity = 3.5f` at `frame_setup.cpp:40`)
- Micro-shadows being disabled (confirmed: TODO at `deferred_lighting_ps.hlsl:1038-1040`)
- BRDF, IBL, shadows, bloom, tonemapping all being correct
- Tonemap parameters (Slope=0.91, Toe=0.45, Shoulder=0.22) differing from UE5 defaults (0.88, 0.55, 0.26)

---

## 8. Summary of Actionable Items (Ordered by Impact/Effort)

| # | Item | Effort | Visual Impact | Pipeline Risk |
|---|------|--------|---------------|---------------|
| 1 | Change `r.Exposure` default from 1.4 to 1.0 | 5 min | HIGH | NONE |
| 2 | Change `r.SkyLight.Intensity` default from 3.5 to 1.0 | 5 min | HIGH | NONE |
| 3 | Re-enable micro-shadows (remove TODO) | 30 min | HIGH | LOW — verify GTAO SRV state |
| 4 | Wire Hi-Z mip chain to SSR trace | 2-4 h | MEDIUM | LOW |
| 5 | Raise SSR MaxRoughness from 0.4 to 0.8 | 5 min | MEDIUM | NONE |
| 6 | Move cloud compute into render graph | 4-8 h | MEDIUM (perf) | MEDIUM — barrier management |
| 7 | Import intermediate textures into render graph | 1-2 days | NONE (perf) | LOW — incremental |
| 8 | Align tonemap film params with UE5 defaults | 30 min | LOW-MEDIUM | NONE |
