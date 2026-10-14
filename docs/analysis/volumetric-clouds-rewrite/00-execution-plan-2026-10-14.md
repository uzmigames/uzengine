# Volumetric clouds rewrite — execution plan (2026-10-14)

Rulebook task `phase0-3.1.0-volumetric-clouds-rewrite` (`.rulebook/tasks/phase0-3.1.0-volumetric-clouds-rewrite/{proposal,tasks}.md`,
65 open checklist items). Planning consult only: no engine, shader, build or tasks.md change was made for this document.
Every file:line below was read on 2026-10-14 at `main` 692087ab plus the uncommitted working-tree edits listed in §7.4.

## 0. Verdict

1. **The "critical blocker" is historical.** Items 0.B1 / 0.B2 / 0.B3 were written on 2026-03-20 (commit 11474bbe) about the
   2026-03-19 runtime noise-bake shaders (a89dd09a). Those shaders were deleted the next day (8332def6: noise loaded from disk,
   MIT three-geospatial data) and the raymarch has been measured TDR-free since: 0.12 re-verified 2026-10-08 (180 frames,
   no device removal, CloudCompute 1.30 ms) and the 2026-10-08 baseline captured 14 runs x 300 frames at 1920x1080 with
   cloud passes at 3.6-5.7 ms (`docs/analysis/atmosphere-sky/clouds/clouds-reference-baseline.md`). The current
   `cloud_raymarch_cs.hlsl` evaluates procedural value-noise FBM (not Worley), with every loop capped
   (`min(g_primarySteps, 64u)` at :390, `min(g_lightSteps, 6u)` at :333, 3 fixed multi-scatter terms at :439).
   Nothing in the current cloud path calls `WaitIdle` (grep of `runtime/engine/renderer`: none in `cloud_pass.cpp`,
   `scene_render_frame.cpp` or `frame_atmosphere_passes.cpp`), so 0c.2 / 0c.4's premise ("remove WaitIdle, win 10 %") is stale too.
2. **What is real:** the cloud compute still runs *outside* the render graph as a deferred dispatch
   (`scene_render_frame.cpp:137-160`, fed by `m_pendingCloudInput` set in `frame_atmosphere_passes.cpp:289-290`), with hand-rolled
   barriers (`cloud_pass.cpp:547-563, 692-697`) that the `RgStateRegistry` cannot see, and a composite pass that reads depth and the
   cloud texture without declaring either (`frame_atmosphere_passes.cpp:296-297`). Side effects today: the clouds are composited
   with the **previous frame's camera** (registration on frame N, dispatch at the top of frame N+1), and the composite is absent on
   frame 0 (`m_cloudOutputReady`). This is 0c.1 and it is the prerequisite of Phases 3, 4 and 5. It is a ~1 day change.
3. **Minimum ship-able milestone = M0 + M1 (§5):** clouds as graph passes, hard loop caps by construction, `tracingMaxDistanceKm`
   wired, the already-loaded 128^3 Perlin-Worley volume actually sampled, adaptive march with blue-noise jitter and depth
   early-out. Multi-scattering, temporal resolve, aerial perspective, Bayer upscale, BSM and multi-layer are deferred behind it,
   in that order.

## 1. State of the code versus the task text

| Task claim | What the code says now | Evidence |
|---|---|---|
| 0.B1 "Worley noise shader TDRs (128^3 x 27-iteration Worley)" | No Worley shader exists in `runtime/shaders/`. The only `cloud_noise_*_cs.hlsl` left are stale copies in demo build output dirs, and they are the trivial stub `[numthreads(8,8,1)] void main(uint3 DTid){}`. Noise comes from `atmosphere/shape.bin` (128^3 R8) and `shape_detail.bin` (32^3 R8). | `runtime/demos/vfx_viewport/build/bin/shaders/.../cloud_noise_shape_cs.hlsl:1`; `cloud_pass.cpp:261-467`; `clouds-noise-assets.md` gates N1-N4 |
| 0.B2 "`[unroll]` -> 29 KB binary, `[loop]` still TDRs" | Same dead code (a89dd09a). | `git show a89dd09a:runtime/shaders/runtime/atmosphere/cloud_noise_shape_cs.hlsl` |
| 0.B3 "dispatch on main cmdList corrupts render graph state (HDR white)" | Dated 2026-03-20, before the state registry (stage 0 landed 2026-10-09, 9735d401; stage 1 466df62e). The 2026-10-08 baseline montage shows normal frames with the dispatch on the main list. | tasks.md:24; `git log -S'HDR goes white'` -> 11474bbe |
| 0c.2 / 0c.4 "remove WaitIdle, >= 10 % frame time" | No WaitIdle in the cloud path. | grep `WaitIdle` in `runtime/engine/renderer` |
| header comment "cap at 32 steps — 64 causes TDR with cold shader compilation" (`cloud_raymarch_cs.hlsl:387-389`) | Misdiagnosis left in place; the cap is 64 and the shader runs 64 steps at 3.6-5.7 ms. | baseline doc |
| "shape noise 128^3 loaded" | Uploaded (8 MB RGBA8 + 8 MB staging kept alive) but **never sampled**: `SampleDensity` (`:268-318`) uses `FBM(ValueNoise3D)`; only the 32^3 detail volume is sampled (`:305-315`). | `clouds-noise-assets.md` "Still open" |
| CloudCB 256 B, 18 UE-named fields | The 18 fields at offsets 188-248 are never read by the shader; CB is exactly full. | `cloud_pass.cpp:473-520`, `cloud_raymarch_cs.hlsl:69-85` |
| Root constants "20 DWORD" (consult prompt) | The global root signature's `Num32BitValues = 24` (`dx12_pso_creation.cpp:186`). Clouds use 12 (`cloud_pass.cpp:640-653`), composite 4, deferred lighting 9 (`lighting_pass.cpp:169`). Treat the budget as whatever `dx12_pso_creation.cpp:186` says on the day; keep clouds <= 12. | — |
| `cloud_temporal_cs.hlsl` | Compiles, not wired (header `:15-18`); reprojects at a fixed NDC depth 0.5 (`:105-107`). | — |
| `m_cloudCmdList` (0.9) | Created and destroyed, never used. | `scene_renderer_init.cpp:679-680, 1667` |

Re-verification on the current binary (20 minutes, do this once before closing 0.B1-0.B3 as historical):

```
# headful; never --flip, never headless viewport capture (both hang on this box)
runtime\build\bin\uzengine.exe --project sample\projects\cloud_test.uzproject --frames 180 --gpu-validation
# expect: exit 0; log without DXGI_ERROR_DEVICE_REMOVED / DEVICE_HUNG / "D3D12 ERROR"; "CloudPass: shape noise loaded" and
#         "detail noise loaded" once; RgStateRegistry summary at shutdown with 0 claim mismatches naming Cloud_*.
python scripts\rendering\test_clouds.py --poses sun        # C1 (completes, cloud pass present), C2 (metrics finite)
```

## 2. TDR root-cause ranking (question 1)

The historical chain, ranked by evidence; then the standing rules that make the rewrite TDR-free *by construction* rather than by luck.

| # | Hypothesis | Evidence for / against | Diagnostic (if it ever recurs) | Fix / rule |
|---|---|---|---|---|
| 1 | **Garbage inputs x uncapped loops.** The March raymarch had `uint stepCount = max(2u, primarySteps)` and an uncapped `for (i < lightSteps)` (a89dd09a). Any wrong value in `primarySteps`/`lightSteps` (CB packing drift, uninitialised input, a float's bit pattern) runs 10^9 iterations -> watchdog. The same CB fed `ResourceDescriptorHeap[shapeNoiseSrv]` / `[weatherMapSrv]`: a garbage or UINT32_MAX index after a failed bake is a page fault -> DEVICE_REMOVED, reported by the app as "crash". | The caps `min(.., 64u)` / `min(.., 6u)` were added as the workaround and the shader has been stable since. Block B.4 (8851f37e, 2026-10-06) found and fixed a real CloudCB packing drift (`float3 g_windVelocity` at 152 straddling 160 -> HLSL moved it to 160 and shifted `g_primarySteps`, `g_lightSteps`, `g_shapeNoiseSrv` by 8 bytes; fixture `scripts/rendering/test_cbuffer_layouts_fixtures/cloud_raymarch_cs.before.hlsl:60-64`). The March CB was row-aligned, so the drift is proven for 2026-10-06, not for March; the uncapped loops are proven for March. | `python scripts/rendering/test_cbuffer_layouts.py`; the first-frame `CloudPass: CB — ...` log lines (`cloud_pass.cpp:655-675`); `static_assert(offsetof(...))` per field that a loop bound or descriptor index depends on. | Keep compile-time caps on every loop forever: `kMaxPrimarySteps`, `kMaxLightSteps`, `kMaxIterations` (adaptive march, §5 M1), `kMaxOctaves`. Guard every bindless index `!= 0xFFFFFFFF` before use (the detail path already does, `:306`). Never a `while`, never a loop var that the CB alone bounds. |
| 2 | **Noise-bake "TDR" was mis-attributed.** `BakeNoiseTextures` was called from `CloudPass::Execute` step 0 on the *same* command list as the uncapped raymarch (a89dd09a), so one hung submission blamed both. The bake's cost does not support a TDR: 128^3 = 2.1 M threads x ~120 hash evaluations (4 channels x 27 cells + Perlin) ~ 0.25 G hashes, single-digit ms on an RTX 5080. A 29 KB DXIL from unrolled 3x3x3x4 is ordinary. `normalize(0)` gives NaN, never a hang. | Unverifiable (code deleted) and moot: noise is a disk asset. | — | Do not resurrect runtime baking. If a new volume is ever needed (STBN, weather), generate it offline (Python) into `runtime/content/atmosphere/*.bin` and load through `ReadCloudAsset` (`cloud_pass.cpp:237-259`). |
| 3 | **Per-step noise cost.** Today: 64 primary x (1 + 6 light) density samples x 4 FBM octaves x 8 hashes ~ 14 k hash evaluations per half-res pixel. Heavy (3.6-5.7 ms) but ~1000x below the 2 s watchdog. | Measured, not a TDR. | gpu_ms per pass in the run manifest (`test_clouds.py` cost metric). | Sample the loaded 128^3 / 32^3 volumes (2 fetches per density sample) — M1. |
| 4 | **Dispatch dimension error.** | Not present: `(half + 7) / 8` groups (`cloud_pass.cpp:687-690`). The March bake used 32^3 groups of 4^3 = exactly 128^3, also fine. | Log dispatch dims once per pipeline (the pass already logs root constants once). | For every new 3D dispatch (BSM cascades, STBN) use one `DivUp()` helper and log dims once. |
| 5 | **Actual infinite loop.** | None in the current shaders: all loops are counted `for` with uint bounds and caps; the only negative-offset loops (temporal 3x3, `cloud_temporal_cs.hlsl:135-136`) use `int`. | GBV + DRED breadcrumbs name the pass; the TDR reproduces at `--frames 2`. | Rule 1 above. |

Standing rule for the rewrite (checked in review of every shader diff): *every* loop has a compile-time upper bound, every CB
field that bounds a loop or indexes the heap is covered by `test_cbuffer_layouts.py` and a `static_assert`, and a scene that
sets the worst case (`cloud-reference-horizon-steps16` with `minStepSize` at its floor) is in the gate (§8).

## 3. Frame corruption and the render-graph integration (question 2)

### 3.1 What "HDR goes white" most likely was

With the composite blend `SrcBlend = One, DstBlend = SrcAlpha` (`cloud_composite_ps.hlsl:5-8`), any NaN or huge value in the cloud
texture propagates into `HDR_SceneColor` and the tonemapper clips it to white. In March the raymarch read a garbage CB /
descriptor index (§2 rank 1) — the same defect that TDR'd also produced NaN/garbage radiance when it did not hang. A second
contributor is plausible but unprovable: before the registry, imports took the caller's state claim on faith and the audit of
2026-10-08 counted 80 cross-frame mismatches; a raw `Common -> UnorderedAccess` on the cloud output while the graph believed
something else is exactly the class the registry now reports. Neither reproduces today: the baseline montage is clean.

### 3.2 Why it must move into the graph anyway

- The registry forbids raw barriers on imported handles **during** `Execute` (`render_graph_states.h:128-131`) but cannot see
  `cmdList->ResourceBarrier` calls that bypass it (`cloud_pass.cpp:557, 697`): the cloud output's state lives in
  `m_initialBarriersDone`, a second source of truth that resets on resize and is invisible to `--gpu-validation` triage.
- The composite's reads of `GBuf_Depth` and `Cloud_Output` are undeclared (`frame_atmosphere_passes.cpp:296-297`): ordering is
  correct only because the lighting pass happens to leave depth in `ShaderResource` and the raw dispatch happens before the
  graph. Phase 3 (ping-pong history), Phase 4 (BSM read by `deferred_lighting_ps`) and Phase 5 (transmittance LUT / AP volume
  reads) all need declared edges the graph can order and barrier.
- One-frame camera latency and the missing frame-0 composite (§0.2) disappear for free.

### 3.3 Designs considered

| Design | Shape | Trade-offs |
|---|---|---|
| **A. In-graph passes owned by `CloudPass::RegisterPasses`** (the `VolumetricFogPass::RegisterPasses` pattern, `volumetric_fog_pass.cpp:108-237`) | CloudPass keeps its textures/pipelines and registers `Cloud_Raymarch` (+ later `Cloud_Temporal`, `Cloud_Shadow_*`) and `Cloud_Composite` with tracked imports and declared reads/writes; SceneRenderer passes `hdrRes`, `depthRes`, the per-frame alloc and `CloudPassInput`. | ~150 lines moved, ~80 deleted. All barriers from the graph; the registry polices them; per-pass GPU timestamps come free (`render_graph_builder.cpp:593-600`); the harness sums any `cloud*` key. Coupling: CloudPass learns `rg::RgBuilder`, as the fog pass already does. **Recommended.** |
| B. Thin wrapper: one `AddPass("Cloud_Raymarch")` whose callback calls the existing `CloudPass::Execute` with its barriers stripped | Smallest diff. | Keeps `CloudPassInput` copied through a `unique_ptr`, keeps the "one Execute does everything" shape; the temporal ping-pong and BSM need separate passes with their own declarations, so B becomes A within one milestone. Not worth the intermediate state. |
| C. Dedicated compute queue + fences (the `m_cloudCmdList` / 0c.3 idea) | Overlap clouds with shadows/GBuffer. | The graph executes one command list on one queue (`RgBuilder::Execute(IRHICommandList*)`), the registry is a single timeline (`render_graph_states.h:22`), no cross-queue fence support. In the reference scene the whole frame without clouds is 0.5-0.6 ms, so there is nothing to overlap with; Sponza-class scenes would benefit, but it is a render-graph feature, not a cloud feature. **Own follow-up; mark 0c.3 "evaluated, deferred".** |

### 3.4 The exact pass-registration shape (design A)

New entry point replacing `CloudPass::Execute` (keep `Execute` only until M0 lands, then delete it):

```cpp
// cloud_pass.h
void RegisterPasses(rg::RgBuilder& rg, rg::RgStateRegistry* states, IRHICommandList* frameList,
                    rg::RgResourceId hdrRes, rg::RgResourceId depthRes,
                    const CBAllocation& perFrameAlloc, CBRingAllocator& cbRing,
                    PipelineLibrary* pipelines, bool bindless,
                    uint32_t renderWidth, uint32_t renderHeight,
                    const CloudPassInput& input);
```

```cpp
void CloudPass::RegisterPasses(/* as above */) {
    if (!m_initialized || !bindless || !EnsureRaymarchPipeline()) return;   // clouds are SM6.6-only, like the fog pass

    // 1. One-time noise upload: outside the graph (registration time, the frame list is open since RenderFrame:76),
    //    so its barriers go THROUGH THE REGISTRY, never raw:
    //    states->Transition(frameList, tex, CopyDst); CopyBufferToTexture(...); states->Transition(frameList, tex, ShaderResource);
    if (!m_noiseLoadAttempted) LoadNoiseTextures(frameList, states, m_vfs);

    // 2. Constants: ring allocation, alive for the frame. Root constants stay at <= 12 DWORDs; everything new goes in the CB.
    CBAllocation cbAlloc = cbRing.Allocate(sizeof(CloudCB));  /* fill as today, cloud_pass.cpp:566-618 */
    const uint32_t rootConst[12] = { /* as today, cloud_pass.cpp:640-653 */ };

    // 3. Tracked imports: the registry supplies Common on first use (creation state), the real last state afterwards.
    //    (render_graph_builder.h:151-160.) One import per handle per graph.
    const auto cloudOut  = rg.ImportTrackedTexture("Cloud_Output",      m_outputTexture);
    const auto shapeRes  = m_shapeNoiseTexture.IsValid()  ? rg.ImportTrackedTexture("Cloud_ShapeNoise",  m_shapeNoiseTexture)  : rg::RgResourceId{};
    const auto detailRes = m_detailNoiseTexture.IsValid() ? rg.ImportTrackedTexture("Cloud_DetailNoise", m_detailNoiseTexture) : rg::RgResourceId{};

    // 4. Ray march. NO ResourceBarrier calls inside the callback: the graph emits Common/ShaderResource -> UnorderedAccess
    //    before it and UnorderedAccess -> ShaderResource before the composite (render_graph_builder.cpp:496-531, 563-591).
    rg.AddPass("Cloud_Raymarch", [=](rg::RgPassBuilder& pb) {
        pb.Write(cloudOut, RHIResourceState::UnorderedAccess);
        pb.Read(depthRes, RHIResourceState::ShaderResource);          // declare the moment the shader samples depth (M1 early-out)
        if (shapeRes.IsValid())  pb.Read(shapeRes,  RHIResourceState::ShaderResource);
        if (detailRes.IsValid()) pb.Read(detailRes, RHIResourceState::ShaderResource);
        pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources&) {
            UZ_PROFILE_SCOPE("Cloud::Raymarch");
            cmd->SetPipeline(m_raymarchPipeline);
            cmd->SetRootConstants(kSlotRootConstants, rootConst, 12);
            cmd->SetRootCBV(kSlotPerObject, cbAlloc.gpuAddr);            // b2 == CloudCB (root_slots.h:36)
            // no SetDescriptorTable(4, ...): the shader writes through ResourceDescriptorHeap[g_outputUavIdx]
            cmd->Dispatch((m_halfWidth + 7) / 8, (m_halfHeight + 7) / 8, 1);
        });
    });

    // 5. Composite: the body of frame_atmosphere_passes.cpp:299-358 moves here unchanged, with the reads declared.
    rg.AddPass("Cloud_Composite", [=, &perFrameAlloc](rg::RgPassBuilder& pb) {
        pb.Write(hdrRes,   RHIResourceState::RenderTarget);
        pb.Read(cloudOut,  RHIResourceState::ShaderResource);
        pb.Read(depthRes,  RHIResourceState::ShaderResource);
        pb.SetExecutionCallback([=, &perFrameAlloc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("Cloud::Composite");
            /* BeginRenderPass(res.GetTexture(hdrRes), clear=false); SetPipeline(pipelines->GetCloudComposite());
               root constants { m_outputSrvIndex, depthSrvIndex }; SetRootCBV(kSlotPerFrame, perFrameAlloc.gpuAddr);
               viewport/scissor renderWidth x renderHeight; Draw(3); EndRenderPass(); */
        });
    });
}
```

Call site: `SceneRenderer::RegisterAtmosphereFogCloudPasses` (`frame_atmosphere_passes.cpp:206-363`) keeps the component gather
and the `CloudPassInput` fill (`:218-281`) and replaces `:283-359` with one `m_cloudPass->RegisterPasses(rgBuilder,
m_rgStates.get(), ctx.cmdList, ctx.hdrRes, ctx.depthRes, ctx.perFrameAlloc, *m_cbRing, ctx.pipelines, ctx.bindless,
ctx.renderWidth, ctx.renderHeight, cloudInput)`. Pass order among `hdrRes` writers stays VolFog -> HeightFog -> Clouds (write-
after-write chains order by registration); the physically right order (fog over clouds) is a Phase 5 decision, not M0's.

Delete: `scene_render_frame.cpp:137-160`; `scene_renderer_impl.h:219-223` (`m_cloudCmdList`, `m_cloudOutputSrv`,
`m_pendingCloudInput`, `m_pendingCloudDispatch`, `m_cloudOutputReady`); `scene_renderer_init.cpp:679-680, 1667`;
`cloud_pass.cpp:547-563, 683-684, 692-697` and `m_initialBarriersDone`; `frame_lighting.cpp:608-614` (`cloud.raw0/raw1`
register invalid handles — `GetDebugRaymarchOutput` returns `{}`); the `temporalBlendFactor`/`prevViewProj` members of
`CloudPassInput` until the temporal pass exists. Keep `m_passInspector.IsPassEnabled("clouds")` and `RegisterTarget("cloud.output",
..., m_cloudPass->GetCloudOutputSrv(), ...)`.

Registry rules the implementation must respect (they are what makes 0c.1 verifiable):
- Consecutive passes writing the **same** UAV get no barrier from `ScheduleBarriers` (`render_graph_builder.cpp:515`: a barrier
  is emitted only when the state changes), and `UavBarrier` on an imported handle is fatal during `Execute`. So no two cloud
  passes may write the same texture back-to-back: temporal history and BSM temporal resolves are ping-pong pairs, both imported
  tracked, and a read (`ShaderResource`) sits between any two writes of one texture.
- Resize: `DestroyOutputTexture` + `CreateOutputTexture` yields a new generational handle, which the registry treats as a new key
  in its creation state (`render_graph_states.h:11-13`); nothing to reset.
- The noise upload's `Common -> CopyDst -> ShaderResource` goes through `states->Transition` so the first `pb.Read(shapeRes,
  ShaderResource)` is a no-op barrier instead of a double transition.
- `g_depthSrvIdx` is in the root constants but the raymarch does not sample depth today (`cloud_raymarch_cs.hlsl:351-478`); declare
  `pb.Read(depthRes)` in the same change that adds the depth early-out (M1), so that declarations match reads exactly.

Gate for 0c.1 (= M0's gate, §5): a `--gpu-validation --frames 180` run with zero D3D12 errors and zero registry claim
mismatches for `Cloud_*`; `test_clouds.py` default run against the baseline with cloud cost within +-10 % and image delta
|dY|/Y over the sky region < 1 % at every pose (the only expected change is the camera-latency fix, invisible in static
captures); the frame-0/1 screenshot of `cloud_test` shows clouds (today frame 0 has none).

## 4. Dependency map of the 65 items

```
0c.1 in-graph passes ─┬─> 3.3/3.4/3.7 temporal ping-pong (needs declared history reads/writes)
                      ├─> 4.6 lighting reads BSM (needs a declared edge lighting <- Cloud_Shadow_*)
                      ├─> 4.7 raymarch reads BSM
                      ├─> 5.2 raymarch reads transmittance LUT / AP volume (tracked imports already exist: frame_atmosphere_passes.cpp:57-101)
                      └─> 0c.4 (rescoped: cost parity), 0c.3 (deferred)
7.1 tracingMaxDistanceKm + hard iteration cap ─> 1.3 adaptive step, 1.5 empty-space skipping (unbounded otherwise: 800 km chord / 0.01 km = 80 k iterations)
(new) 1.0 sample the 128^3 shape volume ─> 1.6 mip LOD (needs a texture), 6.3/6.4 density profiles, 7.3 constants
1.4 jitter ─> a temporal filter (TAA today; 3.3-3.4 own resolve) or the frame flickers
3.6 cloud front depth ─> 3.5 depth-dilated reprojection, 5.1 transmittance-weighted depth (same output channel: do once)
2.1-2.3 octaves ─> 2.4 powder, 2.5 Draine (optional), 2.6 component fields
4.1-4.5, 4.8, 4.9 BSM passes ─> 4.6, 4.7 consumers; 4.6 also touches lighting_pass.cpp (in flux, §7.4)
6.1-6.7 multi-layer: independent of 2-5; CB layout churn -> test_cbuffer_layouts.py each step
8.1-8.5 = metrics that already exist in scripts/rendering/test_clouds.py (mask, banding, temporal CoV, silver lining, aerial, cost)
Tail 1.1-1.3: docs/specs/rendering/atmosphere.md + a gtest for the CB layout + the harness run
```

Prerequisite chain for the minimum ship: `0c.1 -> 7.1 + caps -> 1.0 -> 1.2-1.8 -> 1.9`.

## 5. Milestones (shippable increments, each with a VERIFY gate)

Every milestone runs gate G0 (§8) in addition to its own gate. "Done" means the gate's numbers are pasted into the milestone's
closing note under `docs/analysis/volumetric-clouds-rewrite/`.

### M0 — Clouds are render-graph passes (items 0c.1, 0c.2*, 0c.4*, closes 0.B1-0.B3 as historical)  ~1 day
- Implement §3.4 exactly; delete the listed code. Rename nothing else; the GPU-timing keys become `Cloud_Raymarch` and
  `Cloud_Composite` (the harness matches any key starting with `cloud`, `test_clouds.py:29-31`).
- 0c.2 and 0c.4 are rescoped in the closing note: "no WaitIdle existed; the dispatch is same-frame and the composite exists on
  frame 0; cost parity gate passed".
- 0.B1-0.B3: close with the evidence table of §1 plus the §1 re-verification run output. Do **not** spend time on Worley.
- 0c.3: "evaluated, deferred to a render-graph multi-queue follow-up" (§3.3 design C).
- Also fix the stale comment at `cloud_raymarch_cs.hlsl:387-389` (it documents a wrong cause).
- **Gate M0:** G0; `test_clouds.py` vs baseline: C1, C2; `cloud_ms` within +-10 % per pose; image delta < 1 %; frame-1
  screenshot of `cloud_test` shows clouds; registry summary: 0 claim mismatches, 0 double imports.

### M1 — Ship-able clouds (7.1, new caps, new 1.0, 1.2-1.9, part of 7.3)  ~3-4 days
Decisions:
- **CB, not root constants, for new fields.** Grow `CloudCB` to 512 B (ring allocation; `static_assert` and `test_cbuffer_layouts.py`
  updated) and *repurpose* the 18 never-read fields at 188-248. Keep root constants at 12 DWORDs. The six CVar-as-root-constant
  floats (`cloud_pass.cpp:626-653`) move into the CB in the same change.
- **Hard caps by construction** (§2): `kMaxIterations = 256` on the adaptive loop (`for (i < kMaxIterations && t < tFar)`),
  `kMaxLightSteps = 8`, `tFar = min(tFar, tNear + g_tracingMaxDistanceKm)` (7.1; the 50 km literal at `:385` goes away).
- **1.0 density from the loaded volumes**: `shape = shapeTex.SampleLevel(s3, pos * shapeScale, mip).rgba` with the Schneider
  Perlin-Worley remap (R base, GBA Worley FBM `0.625/0.25/0.125`), coverage remap `saturate(remap(shape, 1 - coverage, 1, 0, 1))`,
  detail erosion from the 32^3 volume as today (`:305-315`). Use static sampler **s3** (bilinear wrap, `dx12_pso_creation.cpp:310-323`),
  not `s0` (16x anisotropic — what `linearSampler : register(s0)` binds today). Keep the procedural FBM behind
  `r.Cloud.ProceduralNoise` (default 0) for one milestone of A/B, then delete it with its 6 CVars. Guard
  `g_shapeNoiseSrv != 0xFFFFFFFF` like the detail path.
- **Jitter without a new asset**: 2D blue noise 64x64 R8 (generated offline into `runtime/content/atmosphere/bluenoise64.bin`,
  loaded by `ReadCloudAsset`) plus the R2/golden-ratio per-frame offset `frac(bn + 0.618034 * frameIndex)`; the STBN 128x128x64
  volume (1.1) is deferred to M2 — under TAA the quality difference is small and this avoids a 1 MB asset decision now.
- **Depth early-out** (new): read `GBuf_Depth` at the half-res pixel, convert to view distance and clamp `tFar` (geometry pixels
  cost nothing; the composite's mask at `cloud_composite_ps.hlsl:56-58` stays as the full-res edge mask). This is the item that
  declares `pb.Read(depthRes)` in `Cloud_Raymarch`.
- 1.3 adaptive step `stepT = clamp(stepT * perspectiveStepScale, minStep, maxStep)`, 1.5 empty-space skipping (step toward
  `maxStep` while density == 0, with `kMaxIterations` still the bound), 1.6 mip from distance, 1.7 component fields
  (`minStepSizeKm = 0.05`, `maxStepSizeKm = 1.0`, `perspectiveStepScale = 1.006`, `jitterScale = 1.0`), 1.8 C++ packing.
- The 3-term "multi-scatter" hack (`:435-445`) and the powder blend (`:452-453`) stay in M1 (M2 replaces them); the fake
  aerial tint in the composite (`cloud_composite_ps.hlsl:68-80`) stays until Phase 5.
- **Gate M1 (= 1.9):** G0 on `cloud_test` **and** on `cloud-reference-horizon-steps16` with `minStepSizeKm` at its floor (the
  worst-case iteration count must terminate and cost < 3x the sun pose). `test_clouds.py` vs baseline: `hf_over_control`
  at the sun pose <= 30 (baseline 257) and `dose_monotone == false` (16/32/64 steps converge once the march is adaptive and
  jittered); `temporal sky.cov_p95` <= 1.5x baseline (0.0026-0.0040) with TAA on; `cloud_fraction` within +-20 % of baseline
  per pose; `cloud_ms` <= baseline per pose (the texture path should land well below: target <= 2.5 ms at the sun pose);
  montage reviewed (JPEG, opened with `code.cmd -r`) at the four poses.

**M0 + M1 is the minimum ship-able milestone (question 4):** no TDR by construction, no undeclared state, same-frame camera,
plausible structured clouds without banding, cost at or below today's.

### M2 — Lighting and temporal (2.1-2.4, 2.6, 2.7; 3.3, 3.4, 3.6, 3.7; 5.1-5.4; 1.1)  ~1 week
- 2.1-2.3 Oz/Kulla octaves (a = b = c = 0.5, `multiScatterOctaves` default 8, `kMaxOctaves = 8`), sky irradiance term from the
  SH L0 already passed as `ambientColor` (`frame_atmosphere_passes.cpp:123-125, 240-242`), replacing `:402-409, 435-458`.
  2.4 powder behind `powderScale = 0`. 2.5 Draine: optional, defer unless 8.3 (sunset glow) fails its review.
- Temporal resolve as a graph pass at the march resolution: `Cloud_Temporal` reads `Cloud_Output` + `Cloud_History[read]`,
  writes `Cloud_History[write]` (ping-pong, both imported tracked); composite reads the history. `cloud_temporal_cs.hlsl`
  stays variance-clipped but reprojects with the **cloud front depth** (3.6, written to a second R16F target or packed into a
  spare channel) instead of NDC 0.5 (`:105-107`). `prevViewProj` comes from `m_prevViewProj` (unjittered) as today.
- 5.1-5.3: the cloud pass declares reads of the transmittance LUT and the AP volume (tracked handles in `AtmosphereFrameContext`)
  and applies `L * T(camera -> frontDepth) + S(1 - cloudT)`; the composite's fake tint (`cloud_composite_ps.hlsl:68-80`) and the
  raymarch's `exp(-d * 0.04)` blend (`:467-476`) are deleted in the same change. Decide the fog/cloud composite order here.
- 1.1 STBN volume if the M1 temporal gate was marginal.
- **Gate M2:** silver lining: sun-pose `ratio` > away-pose `ratio` by >= 0.15 (baseline 0.388 vs 0.401, i.e. none);
  aerial `falloff` <= 0.10 at the horizon pose (baseline 0.19); `sky.cov_p95` <= baseline (the resolve must not add flicker);
  the 60-frame sequence of 8.5 reviewed for ghosting at the `away` pose with the camera orbiting (new project variant).

### M3 — Quarter-res Bayer upscale (3.1, 3.2, 3.5, 3.8)  ~1 week, behind `r.Cloud.TemporalUpscale` (default 0 until the gate passes)
- Quarter-res march, 4x4 Bayer slot per frame, full-res resolve that passes current-slot pixels through and reprojects the rest
  with 3.5 closest-depth velocity dilation. The composite becomes a pass-through at full res.
- **Gate M3 (= 8.4):** `cloud_ms` <= 0.4x M2 at 1080p; `sky.cov_p95` <= 1.2x M2; no ghosting in the orbit sequence; the
  registry shows the two history textures alternating `UnorderedAccess`/`ShaderResource` with no claim mismatch.

### M4 — Beer shadow maps (4.1-4.10)  own follow-up task, ~1.5 weeks
- `Cloud_Shadow_Cascade{0..2}` orthographic marches from the sun (RGBA16F: front depth, mean extinction, max and tail optical
  depth), temporal resolve per cascade (ping-pong), `SampleCloudShadow(worldPos)` with Vogel 16 taps.
- **Data path decision:** per-cascade matrices and params in a **bindless `StructuredBuffer<CloudShadowCascade>`** (one root-constant
  DWORD for its SRV index, one for the cascade count), not in a CB: `PerFrameCB` has no free bytes (`deferred_lighting_ps.hlsl:48`),
  root slot 5 (`b3`) is pixel-only (`dx12_pso_creation.cpp:229`) so a CB there cannot serve the raymarch or the fog inject CS,
  and `ShadowDataCB` (`b4`) is shared with the CSM path. `GBufferIndices` grows from 9 to 11 of 24 DWORDs.
- Consumers: 4.6 `deferred_lighting_ps.hlsl` (`sunShadow *= SampleCloudShadow(...)`) — the lighting pass declares
  `pb.Read(bsmRes[i], ShaderResource)`; the froxel fog inject (`volumetric_fog_pass.cpp:144-161`) should consume the same
  function (sun shafts under clouds) in the same change; 4.7 the raymarch replaces its 6-step light march with a BSM lookup
  (biggest remaining cost win).
- **Gate M4 (= 8.1 ground shadows):** a new `cloud-reference-shadow` pose looking down at the floor from 2 km: shadowed floor
  luminance / lit floor luminance in [0.2, 0.6] under coverage 0.45; CoV p95 over the orbit sequence <= M2's; GBV clean;
  `lighting_pass.cpp` diff reviewed against the concurrent edits (§7.4).

### M5 — Multi-layer + density profiles (6.1-6.7)  own follow-up task, ~1 week
- `CloudLayer[4]` as float4 arrays in a 512-1024 B CB (or a structured buffer like M4), weather RGBA channels, per-layer
  `exp * exp(e h) + linear h + constant`, layer interval skipping. Each step re-runs `test_cbuffer_layouts.py`.
- **Gate M5 (= 6.7):** two layers at 2 km and 8 km visibly distinct at the `above` and `horizon` poses; cost <= 1.3x M4.

### M6 — Cleanup, docs, tests (7.2-7.5, 8.1-8.5 closure, Tail 1.1-1.3)
- 7.3 parameterise the remaining literals (`kSkyLightCloudBottomVisibility`, UV scales, height-gradient ramps, shadow march
  distance); 7.4 `docs/references.md`; 7.5 `docs/specs/rendering/atmosphere.md`.
- 8.1-8.5 map onto `test_clouds.py` metrics: 8.1 banding + silver + shadow pose, 8.2 `above` pose, 8.3 a `sunset` pose variant
  (sun at 5 deg) for the phase glow, 8.4 M3's cost table, 8.5 the 32-frame CoV plus the orbit sequence.
- Tail 1.2: a gtest that mirrors `test_cbuffer_layouts.py` for `CloudCB` offsets (machine-checked values only).

## 6. Deferrable / independent / follow-up classification

| Class | Items |
|---|---|
| Prerequisite of everything | 0c.1 (M0); 7.1 + hard caps (M1) |
| Minimum ship | M0 + M1: 0c.1, 0c.2*, 0c.4*, 0.B1-0.B3 (closed), 7.1, 1.2-1.9 (+ new 1.0, caps, depth early-out) |
| Deferred without blocking ship | 2.x (M2), 3.3/3.4/3.6/3.7 (M2), 5.x (M2), 1.1 STBN (M2), 2.5 Draine, 7.3 rest |
| Own follow-up tasks | 3.1/3.2/3.5/3.8 Bayer upscale (M3, interacts with TAA), 4.1-4.10 BSM (M4, touches the lighting pass and fog), 6.1-6.7 multi-layer (M5), 0c.3 async compute (render-graph multi-queue) |
| Independent of each other | M3, M4, M5 (any order after M2); 7.4/7.5 docs any time |

## 7. Architectural risks to flag

1. **Double temporal filtering (M2/M3 vs TAA).** Clouds composite into `HDR_SceneColor` *before* the TAA/upscaler pass, which
   reads HDR, velocity and depth (`frame_postprocess.cpp:264-275`). Sky pixels have no geometry velocity, so TAA reprojects
   clouds by camera motion only and its neighbourhood clamp will fight a 1/16-coverage Bayer pattern (M3) — the classic flicker
   or smear. Decision: the cloud resolve is complete *in cloud space* (full-res history, variance clip, front-depth
   reprojection) before the composite, so TAA sees a converged image; M3 ships behind a CVar and its gate is the CoV metric.
   Fallback if the gate fails: composite clouds **after** TAA using the unjittered matrices (the clouds already use them,
   `frame_atmosphere_passes.cpp:216-220`), accepting a 1-px silhouette shimmer from the jittered depth mask.
2. **Root-constant budget.** 24 DWORDs today (`dx12_pso_creation.cpp:186`), 9 used by deferred lighting; BSM adds 2. Clouds must
   not add any: new cloud fields go into the 512 B `CloudCB`. If the budget ever changes, the overflow corrupts the adjacent root
   CBVs silently — add a `static_assert(kCloudRootConstantCount <= 12)` and keep the lighting count asserted against the
   signature constant.
3. **`PerFrameCB` is full** (`deferred_lighting_ps.hlsl:48`). Anything the lighting PS needs from clouds travels by bindless index
   (§5 M4 data-path decision), never by growing `PerFrameCB`.
4. **Concurrent edits.** The working tree has uncommitted changes in `lighting_pass.cpp/.h`, `frame_lighting_pass.cpp`,
   `render_graph_builder.h`, `scene_renderer_init.cpp`, `atmosphere_renderer.h`, `scene_renderer_frame.h` (git status
   2026-10-14). M0 touches `scene_renderer_init.cpp:679-680, 1667` (`m_cloudCmdList`) and `scene_renderer_impl.h`: land M0
   after that work is committed, or leave the `m_cloudCmdList` deletion for M1. M4's lighting-pass integration must be rebased on it.
5. **Same-UAV consecutive writes get no barrier** (`render_graph_builder.cpp:515`). Every temporal resolve is a ping-pong pair;
   the review checklist for each new cloud pass is "which texture does it write, who read it last".
6. **Unbounded adaptive loops** are the only way the rewrite can bring the TDR back (§2 rank 3): `kMaxIterations` is
   non-negotiable and the worst-case pose is in every gate from M1 on.
7. **Noise assets license.** `runtime/content/atmosphere/LICENSE-three-geospatial.txt` covers the four data files; any new
   volume (blue noise, STBN, weather) is generated by our own script or carries its own notice in `docs/references.md`.
8. **Harness blind spots** (`clouds-reference-baseline.md:49-55`): the screenshot CoV under-reads HDR flicker 3-6x (an HDR region
   dump over frames 150-181 is the stronger temporal gate from M2 on), and the cloud mask has no transmittance channel.

## 8. Verification protocol (headful only on this box)

- **G0 (every increment):** `runtime\build\bin\uzengine.exe --project sample\projects\cloud_test.uzproject --frames 180
  --gpu-validation` -> exit 0; no `DEVICE_REMOVED` / `DEVICE_HUNG` / `D3D12 ERROR` in the log; registry shutdown summary with no
  `Cloud_*` claim mismatch or double import. Never `--flip`, never headless viewport capture (both hang here). Build check per
  `uzengine-build-workflow` memory (zig build exit code + timestamp; embedded shaders need a rebuild; kill a locked exe).
- **Harness:** `python scripts\rendering\test_clouds.py` (default run -> `.screenshots/flicker-elimination/test-clouds/after`,
  compared with `baseline`; `--baseline` re-captures when the reference scene itself changes, e.g. the new `shadow`, `sunset` and
  `orbit` variants in M4/M6 — regenerate projects with `scripts\rendering\cloud_reference.py`). Gates per milestone in §5; the
  thresholds are relative to the 2026-10-08 baseline table (`clouds-reference-baseline.md:31-38`).
- **CB layout:** `python scripts\rendering\test_cbuffer_layouts.py` after every CB edit.
- **Visual review:** JPEG montage per milestone opened with `code.cmd -r` (user reviews visually; see `image-review-preference`).
- **Screenshots:** `--screenshot-frames N` (one frame; "N" means "N-N" since the 2026-10-08 parser fix).

## 9. Decisions this plan takes (say so if you want them reversed)

1. 0.B1-0.B3 are closed as historical with the §1 evidence after one re-verification run; no Worley work.
2. 0c.2 / 0c.4 are rescoped to "same-frame in-graph dispatch at cost parity"; 0c.3 is deferred to a render-graph multi-queue task.
3. Density comes from the loaded Perlin-Worley volume; the procedural FBM and its six CVars are deleted after one milestone of A/B.
4. New cloud parameters live in a 512 B `CloudCB`; root constants stay at 12 DWORDs.
5. Jitter starts with 2D blue noise + R2 offset; the STBN volume waits for M2.
6. BSM cascade data travels as a bindless structured buffer; the froxel fog consumes it in the same change as the lighting pass.
7. Bayer upscale ships behind `r.Cloud.TemporalUpscale` and is gated by the temporal CoV metric.
8. M0 lands after the current uncommitted lighting/render-graph edits are committed.

## Appendix — file:line index

- Dispatch outside the graph: `runtime/engine/renderer/scene/scene_render_frame.cpp:137-160`; pending-input hand-off
  `runtime/engine/renderer/frame/frame_atmosphere_passes.cpp:283-290`; composite with undeclared reads `:292-359`.
- `CloudPass::Execute` raw barriers `runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp:547-563, 692-697`; root constants
  `:623-677`; leftover UAV table `:683-684`; noise upload raw barriers `:356-372, 442-453`; CB layout `:473-520`.
- Members to delete: `runtime/engine/renderer/scene/scene_renderer_impl.h:219-223`; `scene_renderer_init.cpp:679-680, 1667`.
- Pass inspector targets: `runtime/engine/renderer/frame/frame_lighting.cpp:603-615`.
- Shader: root constants `runtime/shaders/runtime/atmosphere/cloud_raymarch_cs.hlsl:24-38`; CB `:44-87`; sampler `s0` `:89`;
  FBM `:244-254`; density `:268-318`; light march cap `:333`; 50 km literal and the stale TDR comment `:384-395`; march loop
  `:416-465`; AP blend `:467-476`. Composite mask and fake tint `cloud_composite_ps.hlsl:56-58, 68-80`. Temporal (unwired)
  `cloud_temporal_cs.hlsl:15-18, 105-107`.
- Render graph: `ImportTrackedTexture` contract `render_graph/render_graph_builder.h:151-160`; `ScheduleBarriers`
  `render_graph_builder.cpp:496-531`; `Execute` + registry guard `:537-606`; registry API `render_graph_states.h:92-131`.
- Template pass: `passes/atmosphere/volumetric_fog_pass.cpp:108-237`. Tracked HDR/depth/velocity imports and unjittered VP:
  `frame/frame_setup.cpp:456-494`. TAA reads: `frame/frame_postprocess.cpp:264-275`.
- Root signature: `runtime/engine/rhi/dx12/dx12_pso_creation.cpp:178-247` (24 DWORDs, slot 4 UAV table u0-u3, slot 5 `b3`
  pixel-only, static samplers s0-s3 `:263-323`). Lighting root constants: `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl:38-50`,
  `passes/lighting/lighting_pass.cpp:169`.
- Component: `runtime/engine/ecs/environment/volumetric_clouds_component.h`.
- History: a89dd09a (2026-03-19 bake + uncapped march), 8332def6 (2026-03-20 disk noise), 11474bbe (2026-03-20 the 0.B notes),
  981a3ed6 (CVars via root constants), 8851f37e (2026-10-06 CB packing fix, block B.4), 70772229 (2026-10-08 noise assets),
  9735d401 / 466df62e (state registry).
- Docs and scripts: `docs/analysis/atmosphere-sky/clouds/clouds-noise-assets.md`, `.../clouds-reference-baseline.md`,
  `docs/analysis/_references/engine-comparison/three-geospatial-clouds-adaptation.md`, `scripts/rendering/test_clouds.py`,
  `scripts/rendering/cloud_reference.py`, `scripts/rendering/test_cbuffer_layouts.py`, `scripts/rendering/test_cloud_noise_assets.py`.
