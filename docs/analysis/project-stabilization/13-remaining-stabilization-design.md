# 13. Remaining stabilization - implementation design (F-005, F-018, F-022)

Return to the [index](README.md). Design only: no code, build or engine run was
performed. Line numbers refer to the working tree on 2026-10-15 and must be
rechecked before editing. Each section ends with the order and risks it assumes.
Findings and prior evidence: [02](02-rendering-correctness.md),
[05](05-performance-and-maintainability.md), [10](10-backlog-reconciliation.md);
format mirrors [11](11-runtime-and-editor-lifecycle-design.md) and
[12](12-rendering-correctness-design.md).

ID note: checklist item 5.1 is **F-018** (instance batches built while instanced
drawing is disabled, 05). F-013 is the CI clean-checkout finding (04), closed under
1.1. The parent request labelled 5.1 as F-013; this document uses the register id.

Shared facts used below:

- Post-process registration order (frame_postprocess.cpp:255-580): upscaler pass
  (:264-364, gated on `ctx.taaActive && m_upscaler && m_shadowDebugMode != 1`) ->
  SSR composite (:383-449) -> SSR copy-prev-colour (:456-489) -> Bloom (:499-511)
  -> Tonemap (:514-580). Every consumer declares `pb.Read(ctx.hdrRes)`; that
  declaration is what orders them after the upscaler (comment at :464-466). The
  tonemap SRV (:514 `capturedTonemapSrv`), bloom input and size (:500-503) and the
  SSR targets (:388-390, :458-460) are fixed at **registration**; execution
  callbacks capture `&ctx` and `this` (:276, :395, :467, :506, :519), so a value
  stored in `FrameContext` at execution time is visible to later callbacks.
- `ctx.taaActive` (frame_setup.cpp:436-440) = `m_taaEnabled && both history
  textures valid && SM6 && pass "taa" enabled`; jitter is applied iff `taaActive`
  (:442-450). `m_taaEnabled` is set from the **requested** upscaler type
  (scene_upscaler.cpp:43).
- Upscaler producers: `ITemporalUpscaler::Init` is `void`
  (temporal_upscaler.h, about :162) and `Execute` is `void` (:182; F-001 makes it
  `bool`). `CreateTemporalUpscaler` (temporal_upscaler.cpp:19-48) returns stubs
  for FSR2/DLSS in SDK-less builds (fsr2_upscaler.cpp:541-563,
  dlss_upscaler.cpp:670-695; `UZ_FSR2_ENABLED`/`UZ_DLSS_ENABLED` default 0) and
  substitutes TSR for XeSS (:38-42). TSR's three passes compile from
  `tsr_resolve_cs.hlsl`, an empty kernel since bd4db199
  ([known issues](../known-issues-and-partial-implementations.md), "r.TSR - STUB"):
  its PSOs are valid, `Execute` dispatches and returns normally, and nothing is
  written. Only DLSS has `IsAvailable()` (dlss_upscaler.h:88, non-virtual, false in
  the stub) and no selection code calls it.
- Instancing data flow: `SyncDrawListAndInstances` (frame_sync.cpp:26) builds the
  draw list, calls `BuildInstanceBatches` (:35), forces `ctx.useInstancing = false`
  (:45), and sets up GPU-cull bounding spheres only when
  `m_gpuOcclusionCullingEnabled && m_depthPrepassEnabled && instanceCount > 0 &&
  bindless` (:108); both flags default false (`r.GpuCull`, `r.DepthPrepass`,
  scene_renderer_init.cpp:1127-1128; `cv_DepthPrepass` frame_setup.cpp:55).
  Consumers of `ctx.instanceBatches`/`instanceDataVec`: the instanced G-buffer
  path (frame_geometry.cpp:320-341), the stats overlay draw count
  (frame_overlay.cpp:272-274), the cull sphere loop (frame_sync.cpp, about
  :124-176). The G-buffer and shadow instanced PSOs compile `MeshVS` /
  `ShadowDepthVS` with `UZ_INSTANCED=1` (pipeline_shadow.cpp:153-178); **no file
  under `runtime/shaders` references `UZ_INSTANCED`** and the only
  `SV_InstanceID` reader is `particle_mesh_vs.hlsl:120`.
- Root constants at slot 0: mesh VS push = `{ vertexBufferIndex, usePackedNormals }`
  (mesh_vs.hlsl:18); doc 12 fixes the order `{ vb, packed, color, prevVb }` for
  F-002/F-004; shadow VS push = `{ vertexBufferIndex }` (shadow_depth_vs.hlsl:11).
  `root_slots.h:17,30` shows the slot; the 32-bit constant count of the graphics
  root signature must be confirmed (grep `Num32BitValues`) before any addition.
- Layout contracts today: `test_constant_buffers.cpp` checks C++ offsets of
  `PerFrameCB` (3 fields) and `PerObjectCB` (11 fields) on the C++ side only;
  `scripts/rendering/test_cbuffer_layouts.py` (ENTRIES :27-105) compares DXC
  reflection offsets with `zig c++` `offsetof` for seven entries (VolumetricFogCB
  x3, HeightFogCB, CloudCB, ShadowDataCB, LightingCB) with three `before`
  negative-control fixtures; `scripts/rendering/test_binary_layout.py` proves the
  compiled engine writes the region-dump layout the sources declare. The repo
  declares ~140 `cbuffer` blocks across `runtime/shaders` (grep `^cbuffer`) plus
  `ConstantBuffer<T>` root blocks, and ~95 `static_assert(sizeof(...))` size pins
  under `runtime/engine`.

---

## F-005 (3.3) - Upscaler validity guard

### Root cause

- Selection has no capability or initialisation result.
  `SceneRenderer::SetUpscalerType` (scene_upscaler.cpp:19-47) shuts the old object
  down (:23-26), stores the requested type (:28), writes it to `r.AntiAliasingMethod`
  (:31), creates and `Init`s the new object with no return value (:34-40) and
  enables jitter from the request (:43). `CreateTemporalUpscaler` returns a stub
  object for FSR2/DLSS in SDK-less builds and TSR for XeSS, so `GetUpscalerType()`
  (scene_renderer.h:589) reports a producer that does not exist.
- The frame then assumes the producer wrote. With a stub (or a TSR whose kernels
  are empty), `ctx.taaActive` is true, the HDR is rendered jittered, the pass
  transitions `history[write]` to UAV (frame_postprocess.cpp:304-306), `Execute`
  records nothing (:348), `m_taaHistoryWritten[writeIdx] = true` (:349) and the
  tonemap reads `m_taaHistorySrv[writeIdx]` (:360, :556). The slot holds cleared
  or undefined memory on the first frames, or the previous backend's last output
  after a switch (a frozen image), never the current frame. SSR composite
  (:388-390, :402-407), SSR copy (:458-460) and bloom (:500-503) select the same slot
  through `ctx.taaActive` / `m_taaHistoryIndex`.
- Even with F-001's `bool Execute`, the tonemap SRV is captured at registration
  (:514) while the dispatch result is known only inside the upscaler callback, so
  `MarkFailed` alone cannot change what this frame displays.
- Runtime switching: `ApplyCVars` (scene_renderer_init.cpp:1063-1066) re-requests
  whenever the CVar differs from `m_upscalerType`; Lua `renderer.setAntiAliasing`
  (lua_renderer_api.cpp:367-371) exposes None/TAA/TSR; quality presets and project
  files can request 3/4/5. None of these callers can learn that the request was
  not honoured.
- TSR is the case F-001 cannot catch: `tsr_upscaler.cpp:183-187, :253-257, :331-335`
  return only when a PSO is invalid; with the stub shader the PSOs are valid and
  three no-op dispatches are recorded. A C++ `Execute` cannot know a kernel is
  empty, so "produces output" must be declared at the backend, not inferred.

### Options

| | A. Selection-time capability + execution-time display source + failover (recommended) | B. Execution-time fallback only (per-frame HDR when `Execute` is false) | C. Selection-time only (reject/fallback at `SetUpscalerType`) | D. Copy-pass fallback (`CopyTexture(hdr -> history[write])` on failure) |
|---|---|---|---|---|
| Stub FSR2/DLSS/XeSS selected | never runs; effective producer is TAA, reason recorded | runs every frame: jitter with no resolve (1 px shimmer), warning per frame, `GetUpscalerType` lies | same as A | same as B |
| TSR stub | unavailable by declaration; TAA | `Execute` returns true (dispatches recorded) -> stale slot displayed | unavailable by declaration | - |
| Runtime dispatch failure (SDK error, PSO lost) | that frame shows the current HDR; after N frames the producer is replaced | that frame shows the current HDR, forever if persistent | stale slot displayed | needs equal sizes; render != output when screen percentage != 100 -> invalid copy |
| Observable result | `UpscalerSelection {requested, effective, reason}` + one log line + stats line | log spam | log line | - |
| Code | interface: 2 signatures + 1 method; selection ~60 lines; 4 consumer edits in frame_postprocess.cpp | 4 consumer edits | selection only | 1 pass + barriers inside a failure path |

B fails "a valid producer is restored"; C fails "test initialization failure and
runtime switching"; D cannot handle screen percentage. A is C plus the
execution-time part of B plus a bounded failover.

### Recommended change (Option A)

**1. Producer contract** (temporal_upscaler.h, next to :155-182):

```cpp
/// True iff this backend can produce output in this build on this device class:
/// SDK linked, feature present, shader implemented. Stubs return false. No device needed.
[[nodiscard]] virtual bool IsAvailable() const noexcept = 0;
/// Creates GPU resources. Returns false when the backend cannot run on `device`; the
/// object is then in the Shutdown state and must not be executed.
[[nodiscard]] virtual bool Init(IRHIDevice*, PipelineLibrary*, CBRingAllocator*) = 0;
/// (F-001) True iff a history write was recorded on `cmd`.
[[nodiscard]] virtual bool Execute(IRHICommandList*, const UpscalerInput&) = 0;
```

Per backend:

| Backend | `IsAvailable` | `Init` returns | `Execute` false at |
|---|---|---|---|
| TAA (taa_upscaler.cpp:94) | `true` | `m_pipelines->GetTaaResolve().IsValid()` (pipeline_temporal.cpp:43 creates it; call it in `Init` so a compile failure is known before the first frame) | :121 |
| TSR (tsr_upscaler.cpp:52) | `kTsrImplemented` = `false` with the comment "tsr_resolve_cs.hlsl is an empty kernel since bd4db199 (known issues, r.TSR); flip when the shader lands" | three PSOs valid | :185, :255, :333 |
| FSR2 full | `true` | false when `ffxGetInterfaceDX12` or context creation fails (today the error is logged and the object is kept) | :533 (dispatch error) |
| FSR2 stub (:541-563) | `false` | `false` (keep the warning) | `false` |
| DLSS full | existing `IsAvailable()` becomes the override | false when NGX init fails | :551 |
| DLSS stub (:670-695) | `false` | `false` | `false` |
| XeSS | factory returns `nullptr` (temporal_upscaler.cpp:38-42: stop substituting TSR) | - | - |

**2. Selection** (scene_upscaler.cpp:19-47 becomes):

```cpp
struct UpscalerSelection {              // scene_renderer.h, next to GetUpscalerType (:589)
    UpscalerType requested = UpscalerType::TAA;
    UpscalerType effective = UpscalerType::TAA;
    const char*  reason    = "ok";      // "not built with SDK" | "not implemented" | "init failed" | "unknown type" | "failover after N failed frames"
};
const UpscalerSelection& SceneRenderer::SetUpscalerType(UpscalerType requested);
[[nodiscard]] const UpscalerSelection& GetUpscalerSelection() const noexcept;
```

Algorithm: `if (requested == m_selection.requested && !m_forceReselect) return
m_selection;` then shut down the current object; for `type` in
`{ requested, TAA, None }`: `None` -> effective None (no object); otherwise
`up = CreateTemporalUpscaler(type)`; `!up` -> reason "not implemented";
`!up->IsAvailable()` -> "not built with SDK" (or the backend's own reason string);
`!up->Init(m_device, m_pipelines.get(), m_cbRing.get())` -> `up->Shutdown()`,
"init failed"; success -> `m_upscaler = std::move(up); OnResize(m_width, m_height)`
and stop. Then `m_upscalerType = effective; m_taaEnabled = (effective != None);
m_temporal.Invalidate(TemporalReset::UpscalerChanged)` (F-001). The CVar
`r.AntiAliasingMethod` keeps the **requested** value (the user's intent survives a
session; nothing is written back as TAA), and `ApplyCVars` (:1063-1066) compares
`aaMethod != m_selection.requested`. One `UZ_LOG(Renderer, Warning, "Upscaler {}
unavailable ({}); using {}")` per selection change when `effective != requested`;
the stats overlay line reads `AA: TAA (requested FSR2: not built with SDK)`.
`GetUpscalerType()` returns the effective type. Lua gains
`renderer.getAntiAliasing()` -> `{ requested, effective, reason }` if a getter is
wanted; the setter is unchanged.

Why TAA before None: TAA is the engine's own compute shader on every SM6 device,
and the stochastic GI passes rely on it (known issues, "r.TAA"); None is the last
resort. The failover is the same chain minus the failed entry.

**3. Display source** (frame_postprocess.cpp): one struct decided per frame and
overwritten at execution time on failure, read by every consumer **inside its
callback**:

```cpp
struct DisplaySource {                  // scene_renderer_frame.h, FrameContext::display
    uint32_t srv = UINT32_MAX; RHITextureHandle texture; uint32_t uav = UINT32_MAX;
    float width = 0.f, height = 0.f; bool temporal = false;
};
```

- Registration (:262): `ctx.display = taaActive ? { m_taaHistorySrv[write],
  m_taaHistory[write], m_taaHistoryUav[write], vpW, vpH, true } :
  { m_hdrSceneColorSrv, m_hdrSceneColor, UINT32_MAX, renderW, renderH, false }`.
- Upscaler callback (:348-349): `if (m_upscaler->Execute(cmd, input)) {
  m_temporal.MarkWritten(writeIdx); m_upscalerFailStreak = 0; } else {
  m_temporal.MarkFailed(writeIdx); ctx.display = HdrDisplay(ctx);
  ++m_upscalerFailStreak; LogOnce(...); }`. The write slot's UAV->SRV transition
  (:354) stays; F-001's `Advance` sees `written == false` next frame.
- SSR composite (:402-409): `targetTex = ctx.display.texture; targetUav =
  ctx.display.uav; if (targetUav == UINT32_MAX) return;` (the raw-HDR skip is
  unchanged); delete the registration-time `compositeTargetUav` (:388-392) and keep
  the pass registered whenever SSR is active.
- SSR copy (:458-461): choose `copySource = ctx.display.texture` inside the
  callback (HDR is in `ShaderResource` there; it was declared read).
- Bloom (:500-508): `m_bloomPass->Execute(cmd, ctx.display.srv,
  (uint32_t)ctx.display.width, (uint32_t)ctx.display.height, exposure)` inside the
  callback; the `bloomFromDisplayRes` arithmetic moves with it.
- Tonemap (:556): `indices[0] = ctx.display.srv`; the one-shot log (:562) prints
  `display=history|hdr`.
- Failover (frame_setup.cpp, before :436): `if (cv_UpscalerFailoverFrames > 0 &&
  m_upscalerFailStreak >= cv) { m_forceReselect = true; SetUpscalerType(next); }`
  with `next = (effective != TAA) ? TAA : None`, reason "failover after N failed
  frames". CVar `r.Upscaler.FailoverFrames` (int, default 3; 0 = never), registered
  next to `cv_TAAJitterSeqLen`.
- Diagnostics: F-001's `r.TAA.LogDecisions` line gains `execute=ok|failed
  display=history|hdr`.

Why the current HDR and not the previous valid frame: the previous slot freezes
motion for the failed frame and, after a switch, belongs to another backend; the
HDR is this frame's true content. Its one defect - it is jittered - lasts one
frame per failure and ends in failover when failures persist. Document both in
the header comment of `DisplaySource`.

### Acceptance criteria

1. SDK-less build, `r.AntiAliasingMethod = 3` (FSR2) or `4` (DLSS) or `5` (XeSS)
   at startup or at runtime: `GetUpscalerSelection()` is `{ requested, TAA,
   "not built with SDK" | "not implemented" }`, exactly one warning is logged, no
   stub object is executed, the CVar still reads the request, and the backbuffer
   equals the `r.AntiAliasingMethod = 1` backbuffer of the same scene and frame.
2. `r.AntiAliasingMethod = 2` (TSR) while `kTsrImplemented == false`: effective
   TAA, reason names the stub; `GetUpscalerType()` never reports a type without a
   running producer.
3. `Execute` forced false for one frame (debug CVar `r.Upscaler.ForceExecuteFail`,
   or a mock producer in the headless test): that frame's tonemap, bloom and SSR
   copy read the HDR SRV at render size; the next frame is a first frame
   (F-001 `contentReason == FrameGap`); no barrier error on that frame under GBV.
4. Forced failure for 3 consecutive frames: failover to TAA (or None when TAA was
   the failing producer), logged once, reflected in the selection and the stats
   overlay; subsequent frames show the fallback producer's output.
5. Sequence TAA -> TSR -> FSR2 -> None -> TAA through Lua/CVar at runtime: no
   crash, every switch yields effective in `{ requested, TAA, None }`, history is
   invalidated at each switch (F-001), no frame displays a slot not written in
   that frame.
6. `Init` failure injection (`r.Upscaler.ForceInitFail = <type>` debug CVar, or
   mock): the type is skipped with "init failed", `Shutdown()` ran on the failed
   object, the chain continues.
7. `test_fsr2_upscaler.cpp` passes with the stub's `IsAvailable/Init/Execute`
   all returning false.

### Test plan

- Pure, `runtime/tests/engine/renderer/test_upscaler_selection.cpp`: extract the
  chain into `ResolveUpscalerSelection(UpscalerType requested, const Probe&)`
  (scene_upscaler.cpp, `Probe` = two callables `available(type)`, `init(type)`) so
  it runs without a device: `Fsr2Unavailable_FallsToTaa`,
  `DlssUnavailable_FallsToTaa`, `Xess_NotImplemented_FallsToTaa`,
  `Tsr_Unavailable_FallsToTaa`, `TaaInitFails_FallsToNone`,
  `None_IsNone_NoProbe`, `RequestedAndReason_Recorded`,
  `SameRequest_NoReselect`, `Failover_SkipsFailedEntry`.
- `test_fsr2_upscaler.cpp` (exists) and a sibling `test_dlss_upscaler.cpp` (add if
  absent): `StubBuild_IsAvailable_False`, `StubBuild_Init_ReturnsFalse`,
  `StubBuild_Execute_ReturnsFalse` - the negative control for today's "void
  Execute wrote nothing, slot marked written".
- `test_temporal_history_state.cpp` (F-001):
  `ExecuteFailed_SlotNotWritten_NextFrameIsFirst` already covers the slot.
- `test_postprocess.cpp` (exists): `DisplaySource.Temporal_WriteSlotAtOutputSize`,
  `DisplaySource.Hdr_RenderSize_NoUav` (pure builders).
- GPU-gated, `GTEST_SKIP` without a device, pattern of
  `test_headless_frame_capture.cpp` (deferred): `UpscalerGuard.StubFsr2_EqualsTaaCapture`,
  `UpscalerGuard.ForcedExecuteFail_DisplaysCurrentHdr` (compare with
  `r.AntiAliasingMethod = 0` and `r.TAA.JitterSequenceLength = 1` so jitter does
  not enter the comparison), `UpscalerGuard.Failover_AfterThreeFrames`,
  `UpscalerGuard.SwitchSequence_NoFrozenFrame` (consecutive-frame difference
  never zero while the camera moves). Negative control recorded once with the
  current binary: `r.AntiAliasingMethod = 3` shows a cleared or frozen slot.
- SDK builds: `Init` returning false on context-creation failure is verified only
  under `phase2_fsr2-full-integration`; image quality stays there.

### Dependencies and risks

After F-001 (consumes `bool Execute`, `MarkWritten/MarkFailed`,
`Invalidate(UpscalerChanged)`, `ctx.temporal`); independent of F-002/F-004/F-007.
Risks: the `bool Init` signature touches four backends and one test; the DLSS
`IsAvailable()` becomes an override; a project file requesting FSR2 keeps its
CVar value and gets TAA with a logged reason (intended); `kTsrImplemented` is a
declaration that must be flipped by the TSR shader task - record it in the
capability matrix (F-022) so it cannot be forgotten; execution order of the
consumers relies on the existing `pb.Read(ctx.hdrRes)` declarations (keep them).

---

## F-018 (5.1) - Instance-batch preparation and the instanced path

### Root cause

1. **Dead CPU work (default configuration).** `BuildInstanceBatches` (frame_sync.cpp:35)
   runs every frame; `:45` forces `useInstancing = false`; with `r.GpuCull` and
   `r.DepthPrepass` both false (scene_renderer_init.cpp:1127-1128) the cull-sphere
   setup (:108) does not run and the stats overlay reads `drawList.Count()`
   (frame_overlay.cpp:272-274). Every byte the batcher produces is discarded. The
   cost is O(commands): two `push_back`s and a 128-byte copy (instance_batcher.cpp:30-36)
   per command, plus `MakeBatch` per material change. Unmeasured (deferred).
2. **Dead GPU work (`r.GpuCull=1 r.DepthPrepass=1`).** The Phase-1 cull dispatch
   is registered (frame_geometry.cpp:128-146) and writes `visibleIndexBuffer` /
   `indirectArgsBuffer`, but the only consumer is `gbDesc.useIndirectDraw` inside
   `if (ctx.useInstancing)` (:329-339). Already recorded as "two-phase occlusion
   culling never engages" in
   [postprocess-chain-status](../post-process/postprocess-chain-status.md).
3. **Why instancing "produces invisible geometry" (frame_sync.cpp:42-44).** The
   path is unimplemented on the shader side, not merely buggy:
   - `pipeline_shadow.cpp:161,175` compile `MeshVS`/`ShadowDepthVS` with
     `UZ_INSTANCED=1`; neither mesh_vs.hlsl nor shadow_depth_vs.hlsl contains the
     macro, so the "instanced" PSOs run the per-draw shaders.
   - gbuffer_pass.cpp:143-146 pushes `{ mesh0.vbSrvIndex, instanceBufferSrvIndex }`
     into `PushConstants { vertexBufferIndex, usePackedNormals }` (mesh_vs.hlsl:18):
     a non-zero SRV index is read as "packed normals" and 48-byte `Vertex`s are
     decoded as 32-byte `VertexPacked` (mesh_vs.hlsl:47-54) - garbage positions.
   - `instModel = model` from `PerObjectCB` (mesh_vs.hlsl:65-66), which the
     instanced loop never fills (gbuffer_pass.cpp, about :155-171: material
     indices only) - every instance at identity. The shadow loop binds a zeroed
     `ShadowPerObjectParams{}` (shadow_pass.cpp, about :197) - same defect.
   - Every batch draws `desc.meshes[0]` (gbuffer_pass.cpp, about :128-139;
     shadow_pass.cpp, about :141-147 and :190-208) while `BuildInstanceBatches`
     keys batches by `meshId` (instance_batcher.cpp:20); the comment "which the
     InstanceBatcher already enforces" (gbuffer_pass.cpp, about :130) is false.
   - `IsSameBatch` (instance_batcher.cpp:18-27) compares mesh and six texture
     indices only: `materialFlags` (blend mode, masked, two-sided bit 3, decal
     bit 4, shading model bits 24-27), `materialGraphPipeline`, tints,
     multipliers, UV scale/offset, opacity, `alphaClipThreshold`,
     `opacitySrvIndex`, `lodIndexByteOffset/lodIndexCount`, `mobility`
     (draw_list.h:62-172) are ignored, so a masked and an opaque draw with the
     same textures merge under the one opaque back-cull instanced PSO
     (pipeline_shadow.cpp:198-216).
   - `kMaxInstances = 16384` drops the remainder silently (instance_batcher.cpp:80-89);
     the per-draw path has no limit.
   - The indirect path prefills index counts from `m_meshes[0]`
     (frame_geometry.cpp:130-132), draws indirect only when `batchCount == 1`
     (gbuffer_pass.cpp, about :176), and the visible-index
     `AppendStructuredBuffer<uint>` (gpu_cull_pass.h:21) has no vertex-shader
     consumer: an indirect draw with `instanceCount = visible` reads instances
     `0..visible-1`, not the visible indices.
   - D3D12 `SV_InstanceID` starts at 0 for each draw regardless of
     `StartInstanceLocation`, so `batch.firstInstance` must reach the shader.

### Options

| | A. Stage: gate the dead work now; implement + prove parity before enabling; then measure (recommended) | B. Delete the instanced path (batcher, instanced PSOs, cull plumbing) | C. Fix forward and enable |
|---|---|---|---|
| Default-config cost | zero after stage 1 | zero | zero (instanced) |
| Keeps the Hi-Z / cull infrastructure (SSR reads the Hi-Z pyramid, frame_geometry.cpp:344-347) | yes | Hi-Z stays, cull/batcher go | yes |
| Honours "validate before enabling" (05, 5.1) | yes | n/a | no |
| Code | stage 1: ~15 lines; stage 2: 2 shaders, batcher key, 2 pass loops | removals | stage 2 without the gate |
| Risk | lowest; stage 2 is gated by parity tests | throws away work that a later task must redo | regressions on masked/decal/graph draws |

### Recommended change (Option A)

**Stage 1 - remove the unused preparation (behaviour-preserving, lands now).**

- CVar `r.Instancing` (bool, default false), read in `ApplyCVars` next to `r.GpuCull`
  (scene_renderer_init.cpp:1128) into `m_instancingEnabled`, with
  `SetInstancingEnabled/IsInstancingEnabled` beside `SetGpuOcclusionCullingEnabled`
  (scene_renderer.h:511-514).
- frame_sync.cpp:34-45:

  ```cpp
  ctx.useInstancing = m_instancingEnabled && ctx.bindless && !m_depthPrepassEnabled;  // B.4c prepass is per-draw
  if (ctx.useInstancing) {
      BuildInstanceBatches(ctx.drawList, ctx.instanceBatches, ctx.instanceDataVec, ctx.instanceRemainder);
  } else {
      ctx.instanceBatches.clear(); ctx.instanceDataVec.clear(); ctx.instanceRemainder.clear();
  }
  ```

  and the cull setup (:108) gains `&& ctx.useInstancing` (its only consumer).
  The instance and cull buffers are already created lazily (:52, about :118);
  nothing else changes. Delete the comments at :39-44 and state the real
  status: "instanced VS not implemented; see F-018".
- Measurement of the removed cost (deferred, engine): the existing
  `UZ_PROFILE_SCOPE("BuildInstanceBatches")` (instance_batcher.cpp:72) and
  `UZ_PROFILE_FUNCTION()` of `SyncDrawListAndInstances` before and after stage 1
  on Sponza and on a 10 000-draw synthetic scene; report microseconds per frame.

**Stage 2 - correctness before enabling** (one unit; `r.Instancing` stays false by
default until stage 3 decides).

1. Eligibility and key, owned by `instance_batcher.{h,cpp}`:

   ```cpp
   struct InstanceMaterialPayload {   // every DrawCommand field that reaches GBufferPerObjectParams except model/normalMatrix
       uint32_t albedoSrvIndex, normalSrvIndex, roughnessSrvIndex, metallicSrvIndex, aoSrvIndex, emissiveSrvIndex;
       float emissiveColorR, emissiveColorG, emissiveColorB, emissiveIntensity;
       float baseColorTintR, baseColorTintG, baseColorTintB, roughnessMultiplier, metallicMultiplier, normalIntensity;
       float uvScaleX, uvScaleY, uvOffsetX, uvOffsetY, aoIntensity, opacity, alphaClipThreshold;
       uint32_t opacitySrvIndex, materialFlags;
   };
   [[nodiscard]] InstanceMaterialPayload ExtractPayload(const DrawCommand&) noexcept;   // used by the key AND by the per-batch CB fill
   [[nodiscard]] bool IsInstanceable(const DrawCommand& c) noexcept {
       return (c.materialFlags & 0x3u) == 0u && (c.materialFlags & 0x10u) == 0u && c.twoSided == 0u
           && !c.materialGraphPipeline.IsValid() && c.lodIndexCount == 0u && c.mobility != EMobility::Movable;
   }
   ```

   `IsSameBatch` = same `meshId` and `memcmp` of the two payloads. `InstanceBatch`
   carries `meshId` (already), `payload` and `mobility`. `BuildInstanceBatches`
   gains `std::vector<uint32_t>& outRemainder`: ineligible commands and commands
   beyond `kMaxInstances` go to the remainder (never dropped; one warning when
   the limit is hit). Movable objects stay per-draw until stage 3 adds
   `prevWorld` to `InstanceData` for F-002.
2. Shaders. `mesh_vs.hlsl` and `shadow_depth_vs.hlsl` gain:

   ```hlsl
   #ifdef UZ_INSTANCED
   struct InstanceData { row_major float4x4 world; row_major float4x4 normalMatrix; };   // == instance_batcher.h (128 B)
   struct PushConstants { uint vertexBufferIndex, usePackedNormals, colorBufferIndex, prevVertexBufferIndex,
                          instanceBufferIndex, firstInstance, visibleIndexBufferIndex; };   // slots 0-3 per doc 12; 4-6 instanced
   ... main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
       uint slot = push.firstInstance + instanceID;
       if (push.visibleIndexBufferIndex != 0) { StructuredBuffer<uint> vis = ResourceDescriptorHeap[push.visibleIndexBufferIndex]; slot = vis[instanceID]; }
       StructuredBuffer<InstanceData> instances = ResourceDescriptorHeap[push.instanceBufferIndex];
       float4x4 instModel = instances[slot].world; float4x4 instNormal = instances[slot].normalMatrix;
   #else
       float4x4 instModel = model; float4x4 instNormal = normalMatrix;
   #endif
   ```

   The shadow VS uses the same `PushConstants` layout and ignores slots 1-3.
   Confirm the root signature's slot-0 count is >= 7 before this lands (shared
   check with F-002/F-004).
3. Pass loops. gbuffer_pass.cpp instanced block: per batch bind
   `desc.meshes[batch.meshId]` (skip invalid, as the per-draw loop does), push
   `{ mesh.vbSrvIndex, packed, 0, 0, instanceBufferSrvIndex, batch.firstInstance, 0 }`,
   fill `GBufferPerObjectParams` from `batch.payload` through the same
   `ExtractPayload` the key used, `DrawIndexed(mesh.indexCount, batch.instanceCount, 0, 0, 0)`
   (`firstInstance` travels in the push constant, not in the draw, so the shader
   and the draw agree). Then run the existing per-draw two-phase loop over
   `ctx.instanceRemainder` only (opaque/masked first, decals second). Same shape
   in shadow_pass.cpp with the mobility filter applied per batch (static-only /
   movable-only cache modes). The indirect/culled branch stays disabled in stage 2
   (`gbDesc.useIndirectDraw` requires per-batch counters; `visibleIndexBufferIndex`
   is pushed as 0) and the capability matrix records "GPU occlusion culling:
   dispatch without consumer".
4. Stats: `drawCount = instanceBatches.size() + instanceRemainder.size()` at
   frame_overlay.cpp:272, plus a once-per-capture log line
   `DrawCalls: n (instanced batches m, remainder k)` under `--diag`.

**Stage 3 - measure and decide the default** (deferred, engine):
`scripts/rendering/compare_instancing.py` runs the engine with
`r.Instancing = 0/1` on `sample/projects/sponza.uzproject` and on a new
`scripts/rendering/instancing-reference.uzproject` (10 000 static cubes, 4
materials, 1 masked), with `r.GpuProfiler.Log = 1` and `--diag`; collects the draw
count, CPU time of `SyncDrawListAndInstances`, GPU time of the G-buffer and
shadow passes, and the frame difference (same build configuration, as
`test_postprocess_chain.py` requires). The default flips only if the instanced
numbers are better and the difference is within the parity bound below.

### Acceptance criteria

1. Default configuration after stage 1: `BuildInstanceBatches` is not called
   (profiler scope absent), `ctx.instanceBatches.empty()`, no instance or cull
   buffer is created, no cull pass is registered, and the frame is pixel-identical
   to before (region dump or screenshot, same build configuration).
2. `r.Instancing = 1` on a bindless device: G-buffer targets, depth and the shadow
   atlas of (a) one mesh x N instances, (b) mixed meshes, (c) mixed materials
   including masked, two-sided, mesh-decal, material-graph, translucent and
   LOD-overridden draws, (d) static + movable mix, differ from `r.Instancing = 0`
   in at most 0.01 % of pixels (opaque draw order within one PSO is the only
   permitted difference) and never in geometry coverage.
3. Draw count: one G-buffer draw per (mesh, payload) batch plus the remainder;
   for (a) with one material, exactly one batch.
4. 20 000 eligible commands: the stats vertex count equals the per-draw path's
   (nothing dropped); one warning.
5. Shadow cache modes keep their static/movable filtering under instancing.
6. Movable objects are drawn per-draw in stage 2 (no object-velocity regression
   for F-002).
7. Stage 3 numbers recorded for both scenes before any default change; no
   assumed speedup.

### Test plan

- Pure, `runtime/tests/engine/renderer/test_instance_batcher.cpp` (none exists
  today; grep found no batcher test): `InstanceBatcher.EmptyList_NoBatches`,
  `SameMeshSamePayload_OneBatch_CountN_ContiguousFirstInstance`,
  `DifferentMesh_SplitsBatch_KeepsMeshId`, `MaskedVsOpaque_NotMerged` (negative
  control: current `IsSameBatch` merges them), `TwoSided_Decal_Translucent_GoToRemainder`,
  `MaterialGraph_Lod_Movable_GoToRemainder`, `OverLimit_RemainderNotDropped`
  (negative control: current code drops), `InstanceData_Is128Bytes_WorldThenNormal`,
  `ExtractPayload_FillsPerObjectParamsIdentically` (fill `GBufferPerObjectParams`
  from the payload and from the `DrawCommand`, `memcmp` the 256-byte scalar section).
- Shader checks without a device, in `scripts/rendering/test_cbuffer_layouts.py`
  (see F-022): compile `mesh_vs.hlsl` and `shadow_depth_vs.hlsl` with
  `-D UZ_BINDLESS=1 -D UZ_INSTANCED=1` and assert the reflection lists an
  `SV_InstanceID` input and an `instances` SRV, and that `InstanceData`'s stride
  is 128 (negative control: today both variants reflect identically to the
  per-draw shader).
- `test_pipeline_library.cpp`: the instanced PSO's VS blob differs from the
  per-draw VS blob (negative control: identical today).
- GPU-gated (`GTEST_SKIP` without a device; deferred):
  `InstancingParity.SingleMesh_GBufferAndDepthWithinBound`, `...MixedMesh...`,
  `...MixedMaterial...`, `...ShadowAtlas...`, `...OverLimit_NoMissingGeometry`,
  `...ShadowCache_FilterPreserved`, `...Movable_StaysPerDraw`.
- Stage 3 script as above (deferred).

### Dependencies and risks

Stage 1 is independent of every other finding and can land immediately. Stage 2
after F-004 (fixes push-constant slots 0-3) and before F-002's stage 3 (`prevWorld`
per instance); the checklist places 5.1 after 4.1, which stage 3 respects.
Risks: slot-0 root-constant budget (7 DWORDs); per-batch CB allocations on a
scene where most batches are singletons cost what per-draw costs today (no gain,
no loss - stage 3 decides); the shadow instanced PSO must keep the per-draw
depth bias and cull state (compare `GetShadowDepthInstanced` with
`GetShadowDepth`); the depth prepass and instancing stay mutually exclusive
until an instanced depth-only PSO exists (matrix row).

---

## F-022 (5.3) - Layout contracts, capability matrix, module boundaries

### Root cause

**(a) Layout contracts are hand-mirrored in several places and the two
most-used buffers have no HLSL-vs-C++ check.**

- `PerFrameCB` (constant_buffers.h, 1024 B, tail asserts :312-321) is duplicated
  field by field into `PerFrameParams` (per_frame_params.h:93-320, tail asserts
  :323-333, `irradianceCubemap` at 1024) - about 190 fields in two declarations
  whose header still says "512 bytes" (:3-6, :48-51, about :89). HLSL mirrors of
  `PerFrameCB` exist in 27 shader files, partial and positional
  (mesh_vs.hlsl:29-31) or with `packoffset` (gbuffer_ps.hlsl:26-44).
- `PerObjectCB` (constant_buffers.h:366-430) is duplicated into
  `GBufferPerObjectParams` (gbuffer_per_object_params.h:81-146) with `_pad2[7]`
  where the CB has seven POM fields (so the active binder path uploads zeros at
  224-251: POM is off on that path - a matrix row, not a change here) and five
  HLSL mirrors with renamed members (`obj_model`, `matOpacity`,
  gbuffer_ps.hlsl:46-75). Checks: C++ offsets only (test_constant_buffers.cpp:73-96)
  and `offsetof(albedoTexture) == 256`.
- `test_cbuffer_layouts.py` covers seven entries; the inventory from the shared
  facts has ~140 cbuffer blocks and ~25 structured-buffer element types with
  size-only `static_assert`s (e.g. `GpuCullConstants` gpu_cull_pass.h:70,
  `VelocityCB` velocity_pass.h:70, `HistoryCB` ssgi_renderer.cpp:761,
  `RadianceCacheTraceConstants` radiance_cache.h:453, `VFXBillboardCB`
  particle_render_pass.h:166, `InstanceData` instance_batcher.h:42) or none
  (`TaaCB`, a function-local struct in taa_upscaler.cpp, about :123-135). Nothing
  fails when a new cbuffer is added without a mapping.
- Stale comments: "512 bytes" (above); "blendMode" for `materialFlags`
  (gbuffer_per_object_params.h, layout table); "which the InstanceBatcher already
  enforces" (gbuffer_pass.cpp, about :130).

**(b) Capability claims.** [known-issues-and-partial-implementations.md](../known-issues-and-partial-implementations.md)
is the living registry (OFF/PARTIAL/STUB/BUG with file links) but has no
backend, tested-revision or evidence columns, and the partial-support facts
established by docs 10-13 (velocity camera-only, WPO rigid-part only, glass
sphere-only/thin-only, material context dummy, DLSS/FSR2 deferred, TSR stub,
POM off on the binder path, instancing unimplemented, GPU cull unconsumed,
inert post effects, PT not an oracle) live only in analyses. `docs/ARCHITECTURE.md`
already carries a deprecation banner pointing to `docs/engine/`; the March
"production-ready" claim is at `docs/analysis/rendering-arch/overview/uzengine-audit.md:7-15`.

**(c) Module boundaries.** Today's largest sources: vfx_editor_overlay.cpp 4,631
(the user's VFX-editor WIP with three failing tests - excluded), dx12_device.cpp
2,114, fbx_importer.cpp 2,085, scene_renderer_init.cpp 1,807 (`Init` :85-964,
`RegisterCVars` :965-1041, `ApplyCVars` :1042-1188, `Resize` :1189-1337,
`Shutdown` :1362-1790), lua_light_extended_api.cpp 1,657, deferred_lighting_ps.hlsl
1,570, runtime.cpp 1,567, against the project's own "max 1500 lines, split at
~1000" (ARCHITECTURE.md, Clean Code). `SceneRenderer` is already split by
concern across `frame_*.cpp` / `scene_*.cpp`; that is the pattern to extend.

### Options

| | (a) layouts | (b) matrix | (c) boundaries |
|---|---|---|---|
| **1 (recommended)** | extend `test_cbuffer_layouts.py` with tiers + an inventory gate; de-duplicate the CPU side by inheritance | add a matrix section with the missing columns to the existing known-issues registry | three ownership-driven moves tied to findings in flight, each with a mechanical no-change proof |
| **2** | generate HLSL from C++ metadata (`shader_parameter_hlsl.h` exists) | new `docs/reference/rendering-capability-matrix.md` | split every file over 1,500 lines |
| **3** | runtime DXC reflection check at PSO creation | - | none |
| Why not 2/3 | 27 hand-written partial mirrors with `packoffset` would have to become generated includes (embedded-shader rebuilds, SPIR-V packoffset-order constraints, gbuffer_ps.hlsl:35-36); runtime check needs the engine and duplicates the script | two registries to keep in sync | churn in files no finding touches, conflicts with the user's WIP |

### Recommended change

**(a) Layout contracts - three tiers in `scripts/rendering/test_cbuffer_layouts.py`.**

Tier A (DXC reflection vs `offsetof`, the existing mechanism): add ENTRIES for
every `cbuffer` at b1-b8 that has a C++ struct of 16 bytes or more. Required
entries and the maps verified in this session; the others follow the same shape
and the script fails loudly on an unknown name:

| Entry | Shader (profile) | Header / struct | Map (HLSL -> C++) |
|---|---|---|---|
| PerFrameCB | gbuffer/gbuffer_ps.hlsl (ps_6_6) | constant_buffers.h / PerFrameCB | viewProj, cameraPos, timeParams, lightDir, lightColor, ambientColor, mipBias, materialMapMask, materialRoughnessOverride, materialMetallicOverride (same names) |
| PerFrameCB | lighting/deferred_lighting_ps.hlsl, postprocess/tonemap_ps.hlsl, postprocess/velocity_ps.hlsl, ssgi/ssgi_surface.hlsli entry shader | PerFrameCB | every member the mirror declares |
| PerObjectCB | gbuffer/gbuffer_ps.hlsl | constant_buffers.h / PerObjectCB | albedoIndex..emissiveIndex, alphaClipThreshold, opacitySrvIndex, obj_model->model, obj_normalMatrix->normalMatrix, emissiveColor->emissiveColorR, emissiveIntensity, baseColorTint->baseColorTintR, roughnessMultiplier, metallicMultiplier, normalIntensity, uvScale->uvScaleX, uvOffset->uvOffsetX, aoIntensity, matOpacity->opacity, displacementScale, heightmapSrvIndex, pomMinLayers, pomMaxLayers, pomFadeStart, pomFadeEnd, pomSelfShadow, materialFlags |
| PerObjectCB | gbuffer/mesh_vs.hlsl (vs_6_6), lighting/shadow_depth_vs.hlsl (vs_6_6), gbuffer/mesh_ps.hlsl, gbuffer/translucent_ps.hlsl | PerObjectCB | model, normalMatrix (+ what each mirror declares) |
| ShadowCB | lighting/shadow_depth_vs.hlsl | shadow_pass_params.h / ShadowCascadeParams | shadowViewProj->viewProj, light position/radius per header |
| CullConstants | compute/gpu_cull_cs.hlsl (cs_6_6) | gpu_cull_pass.h / GpuCullConstants | viewProjection, viewMatrix, viewToClip, instanceCount, hizEnabled, hizWidth, hizHeight, hizMipCount, g_HiZMipSrvIndices->hizMipSrvIndices (names per shader) |
| VelocityCB | postprocess/velocity_ps.hlsl | velocity_pass.h / VelocityCB | per header |
| SkyAtmosphereCB, APVolumeCB, CloudTemporalCB, cloud ShadowCB->CloudShadowCB, DecalObjectCB, TaaCB (move the local struct to taa_upscaler.h first), HistoryCB (ssgi, 592), TraceConstants->RadianceCacheTraceConstants (one entry per entry-point shader), VFXBillboardCB, VfxCameraCB, SsrTemporalPrevVP, editor Gizmo/Grid/Outline/Overlay CBs | as listed in the shared-facts inventory | per header |

Script changes: `struct_text` accepts indented declarations (`^[ \t]*struct`, so
function-local structs in .cpp files resolve as the CloudCB entry already does);
STUBS gain any stand-in a new struct needs (`Vec2`, `uint2`); the include paths
gain `compute`, `postprocess`, `ssgi`, `gi`, `vfx`, `gbuffer`.

Tier B (root-constant blocks at b0 and `ConstantBuffer<T>` push blocks): the DXC
reflected DWORD count equals `sizeof(struct) / 4`; mapped by name only when the
struct is in a header (GTAO, SSR, SVGF, bloom, exposure, Hi-Z, decal-apply,
particle constants).

Tier C (structured-buffer element types): DXC stride from the resource metadata
equals `sizeof(T)` and a gtest pins the field order (`InstanceData`, `LightGPU`,
`ReflectionProbeGPU`, `GdfMeshSdfGPU`, `SdfObjectGpuData`, `GIMeshMaterialGPU`,
`Vertex`, `VertexPacked`, `SkinnedVertex`).

Inventory gate: `--list` enumerates every `cbuffer`/`ConstantBuffer<` declaration
under `runtime/shaders` with file:line and register; the run fails when a
declaration is neither mapped nor in `UNTRACKED = { (file, name): reason }` (a
reason such as "root constants, 6 DWORDs, static_assert at hiz_pyramid_pass.cpp:56"
is mandatory). New negative-control fixtures: `gbuffer_ps.perobject.before.hlsl`
(`float3 emissiveColor` moved one slot) and `gpu_cull_cs.before.hlsl`
(`uint g_HiZMipSrvIndices[12]`: HLSL pads each array element to 16 bytes, the
defect the script's own docstring describes).

CPU de-duplication: `struct PerFrameParams : PerFrameCB { RHITextureHandle
irradianceCubemap, prefilteredSpecular, brdfLut; static const ShaderParameterMetadata&
GetMetadata(); };` and `struct GBufferPerObjectParams : PerObjectCB { handles...;
GetMetadata(); };` - delete the duplicated field lists, keep every tail
`static_assert`, add `static_assert(std::is_standard_layout_v<PerFrameCB>)`,
`static_assert(offsetof(PerFrameParams, irradianceCubemap) == sizeof(PerFrameCB))`
and the per-object pair. Behaviour: identical bytes for PerFrame; for PerObject
the binder path now uploads `PerObjectCB`'s POM defaults (8, 32, 1000, 2000) where
`_pad2` was zero - `displacementScale` stays 0 so POM stays off and the G-buffer is
pixel-identical; state this in the commit and in the matrix row. Fix the three
stale comments.

**(b) Capability matrix.** Add a section "Capability matrix" at the top of
`docs/analysis/known-issues-and-partial-implementations.md` (the existing
per-item sections become the notes behind each row); link it from
`docs/engine/README.md` and from this programme's README. Columns:
`Feature | Status (OFF/PARTIAL/STUB/BUG/LANDED) | Backend (DX12 SM6.6 / SM5 /
Vulkan / Metal) | Integration point (file:line) | Validation evidence (revision +
test or script) | Known limits | Owner task`. "untested" is a legal evidence
value; blank is not. Required rows and their current values:

| Feature | Status | Evidence / limits |
|---|---|---|
| TAA | OFF by default (`r.TAA`), implemented, SM6 only | flicker-programme region dumps; camera-only velocity until 3.4 |
| TSR | STUB | `tsr_resolve_cs.hlsl` empty since bd4db199; F-005 declares it unavailable (`kTsrImplemented`) |
| FSR2 | STUB without SDK; SDK path untested here | owner `phase2_fsr2-full-integration`; F-005 fallback |
| DLSS | PARTIAL (SDK builds): upscale works per known issues, RR/FG not integrated; discovery fixed 2.6 (2026-10-14) | motion-vector coverage partial (F-002) |
| XeSS | not implemented | factory returned TSR; F-005 returns null |
| Object velocity | camera-only (F-002 designed in 12) | rigid/skinned = stage 1; WPO rigid part only = stage 2; particle vectors in UV units |
| Material-graph context | dummy inputs (F-004 designed in 12) | after landing: vertex colour via side-stream, SM5 white |
| Glass (shading model 9) | sphere-only today; thin-slab after F-007 | no volume transmission |
| POM | fields exist; binder path uploads zeros at 224-251 | verify in gbuffer_ps.hlsl POM block; small follow-up task |
| Mesh decals | LANDED 513e2983 | bit 4 of materialFlags |
| GPU instancing | OFF; VS unimplemented | F-018 stages |
| GPU occlusion culling (`r.GpuCull`) | OFF; dispatch has no consumer | F-018 stage 2 note |
| Depth prepass (`r.DepthPrepass`) | OFF; per-draw only | mutually exclusive with instancing |
| Post-process chain | Bloom + Tonemap (+ TAA, SSR composite/copy); CAS/FXAA/motion blur/DOF/lens flare/LUT/chromatic aberration inert | [postprocess-chain-status](../post-process/postprocess-chain-status.md) |
| Path tracer | diagnostic, not an oracle | 10, F-006 |
| Shadow cache static/movable | per-draw loop only | instanced loop (stage 2) must filter per batch |
| SM5 / non-bindless | TAA off, instancing n/a, vertex colour white | frame_setup.cpp:436-440 |
| Vulkan, Metal | untested in this programme | sources exist (vulkan_command_list.cpp, metal_command_list.cpp) |
| Lighting block B items (froxel fog, contact shadows, layered SSR, DDGI dedup, clustered lights, env-MIS, sky capture) | LANDED | revisions from the block-B index under `docs/analysis/lighting`; copy the SHAs from there, do not re-derive |

Annotate `rendering-arch/overview/uzengine-audit.md` with one dated line under
its title ("historical, 2026-03; not evidence for the current tree - see the
capability matrix"); do not rewrite it.

**(c) Module boundaries - three moves, each paying for itself.**

1. `scene_renderer_init.cpp`: `RegisterCVars` + `ApplyCVars` (:965-1188) ->
   `scene_renderer_cvars.cpp`; `Resize` (:1189-1337) -> `scene_renderer_resize.cpp`.
   Review benefit: F-005's selection and F-001's resize resets are then read in
   one file each; the remaining file (Init/Shutdown, ~1,430 lines) is the lifetime
   pair. CVar statics stay in their TUs, so static-init order is unchanged.
2. `frame_postprocess.cpp`: `RegionFile*` structs with their `static_assert`s and
   `WriteRegionDumpFile` (about :60-253) -> `region_dump_writer.{h,cpp}`. Review
   benefit: F-005 adds the display-source logic to the chain file and the chain
   file becomes the chain only. Check `scripts/rendering/test_region_format.py`
   and `test_binary_layout.py` for the paths they parse before moving.
3. `instance_batcher.{h,cpp}` owns eligibility, key and payload (F-018 stage 2),
   so `gbuffer_pass.cpp` and `shadow_pass.cpp` consume one contract instead of
   re-deriving material state - the false "already enforces" comment is the
   boundary failure this fixes.

Excluded on purpose: vfx_editor_overlay.cpp (user WIP), deferred_lighting_ps.hlsl
(a split changes the embedded blobs; if done later, prove it with DXIL
byte-identity), dx12_device.cpp, fbx_importer.cpp, runtime.cpp (doc 11's
owner-thread work restructures the C API; splitting it twice is waste).

Proof of unchanged behaviour for every move: build exit 0; `test_runtime.exe`
count and the same three pre-existing failures; `git diff -M --color-moved=dimmed-zebra`
shows moves and includes only (reviewer gate); Sponza region dump or screenshot
bit-identical within one build configuration (deferred, engine); no shader
touched, so the embedded shader set is unchanged.

### Acceptance criteria

1. `python scripts/rendering/test_cbuffer_layouts.py` passes with at least 20
   Tier A entries including every row of the table above, the two new negative
   controls fail on their fixtures, and the inventory gate reports zero unmapped,
   unlisted declarations (every `UNTRACKED` entry carries a reason).
2. Tier B and Tier C checks pass for the listed structs; `InstanceData` is in
   Tier C before F-018 stage 2 lands.
3. `PerFrameParams` and `GBufferPerObjectParams` derive from their CB structs; all
   existing tail asserts, `test_shader_parameters.cpp` and `test_constant_buffers.cpp`
   pass; the three stale comments are gone.
4. The matrix section exists with every required row, each with evidence or
   "untested"; docs 11-13's partial-support notes are rows; the March audit has
   its historical note; `docs/engine/README.md` links the matrix.
5. The three moves land with the proofs listed; `test_region_format.py` passes;
   `test_binary_layout.py` (engine) is recorded as deferred.

### Test plan

- `test_constant_buffers.cpp`: `PerFrameParams.IsPerFrameCB_WithHandlesAt1024`,
  `GBufferPerObjectParams.IsPerObjectCB_WithHandlesAt256`,
  `PerObjectCB.PomRegionOffsets_224_To_248`, `InstanceData.Layout_128_WorldThenNormal`.
- `scripts/rendering/test_cbuffer_layouts.py`: the entries, the `--list` gate and
  the two fixtures; run it in the quality gate next to `test_region_format.py`.
- `test_shader_parameters.cpp`: unchanged (regression that the metadata still
  describes the same bytes).
- Documentation: the Rulebook link check that 0.3 used (118 local links) on the
  matrix; no prose-pinning test.
- Module moves: the build/gtest/diff gates above; GPU identity deferred.

### Dependencies and risks

(a) and (c) have no code dependency on 4.1 and can land before it; (b) must be
updated as each finding lands (rows flip from "designed" to "LANDED @ sha").
Risks: `offsetof` on a derived type is conditionally supported (all three
compilers in use evaluate it; the `is_standard_layout` assert on the base keeps
the contract explicit); DXC drops unused `packoffset` members from the layout
comment, so a mirror that declares a field it never reads shows fewer fields
(the script's `checked > 0` guard stands; add the field to `UNTRACKED` with that
reason rather than weakening the gate); `zig c++` stand-ins must stay
byte-compatible with the engine's `Vec*`/`Mat4`; the matrix is only as honest
as its last update - make "update the matrix row" an acceptance line of every
rendering task from now on.

---

## Order of work and cross-finding notes

1. **F-018 stage 1** - the `r.Instancing` gate and the cull-setup gate: independent,
   behaviour-preserving, lands first.
2. **F-022 (a)** - layout tiers and the CPU de-duplication: no dependency; lands
   before any CB-touching change so later findings extend a complete contract.
3. **F-001** (doc 12) then **F-005** - producer contract, selection, display source,
   failover. F-005 also records the TSR declaration in the matrix.
4. **F-022 (c)** moves 1 and 2 - after F-005 so the moved files are moved once.
5. **F-004, F-002** (doc 12) - fix slot-0 push constants; then **F-018 stage 2**
   (slots 4-6, batcher contract = F-022 move 3), then **F-018 stage 3** after 4.1.
6. **F-022 (b)** - the matrix is written now with today's values and amended at
   each landing.

Shared implementation checks: the graphics root signature's slot-0 32-bit
constant count (>= 7 once F-002/F-004/F-018 all land); the DXC include paths of
the layout script; the scripts that parse `frame_postprocess.cpp` before move 2.
Every GPU-gated test and every measurement above is deferred verification and
must be recorded as such; the headless gtests, the DXC reflection script and the
moved-only diffs are the evidence producible without a device.

---

## Status (2026-10-15)

Design-complete; **not implemented** (open items 3.3/5.1/5.3 remain `[ ]`).
None of the three is satisfied on inspection: F-005 has the DLSS `IsAvailable()`
and F-001's designed `bool Execute` as building blocks but no selection or
display contract; F-018's dead preparation is unmeasured and its instanced path
has no vertex shader; F-022 has seven reflection entries, C++ offset tests and a
living registry, but not the two most-used buffers, an inventory gate, a single
CPU declaration, or evidence columns. Four adjacent facts were surfaced and
folded in: TSR is a stub with valid PSOs (F-005 must declare it unavailable),
the GPU-cull dispatch has no consumer (F-018 stage 1 gates it), the binder path
uploads zeros in the POM region (matrix row), and `PerFrameParams` duplicates
`PerFrameCB` with "512 bytes" comments (F-022 de-duplication).
