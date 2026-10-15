# 12. Rendering correctness - implementation design (F-001, F-002, F-004, F-007)

Return to the [index](README.md). Design only: no code, build or engine run was
performed. Line numbers refer to the working tree on 2026-10-15 and must be
rechecked before editing. Each section ends with the order and risks it assumes.
Findings and prior evidence: [02](02-rendering-correctness.md),
[10](10-backlog-reconciliation.md); format mirrors [11](11-runtime-and-editor-lifecycle-design.md).

Shared facts used below:

- Pass order per frame: `SetupFrameData` -> shadows -> geometry (G-buffer, decals,
  **velocity**) -> decals -> lighting -> translucent -> particles -> post-process -> overlay
  ([scene_render_frame.cpp](../../../runtime/engine/renderer/scene/scene_render_frame.cpp):107,183-190).
  Everything registered after `SetupFrameData` reads `FrameContext ctx` (:95-107).
- TAA jitter is an NDC translation of the combined view-projection
  ([frame_setup.cpp](../../../runtime/engine/renderer/frame/frame_setup.cpp):458-470,
  `ApplyTaaJitter`); clip `w` is unchanged by the jitter, which is what makes the
  interpolant-based derivations in F-002 and F-004 exact. `ctx.unjitteredViewProj` is
  stored at :456, `ctx.taaWriteIdx/taaReadIdx` at :561-562.
- `PerFrameCB` is 1024 bytes and full (last slot `componentReplaySrv` at 1020,
  [constant_buffers.h](../../../runtime/engine/renderer/core/params/constant_buffers.h):300-321);
  `PerObjectCB` is 256 bytes and full (:366-430). Neither can take a new field
  without growing the block and its layout tests (`test_constant_buffers.cpp`,
  `scripts/rendering/test_cbuffer_layouts.py`). `cameraPos` (c4) and
  `timeParams` (c5, `x = totalTime, y = dt, z = frameIndex`,
  [frame_constants.cpp](../../../runtime/engine/renderer/frame/frame_constants.cpp):191)
  are already in `PerFrameCB`.
- Mesh VS push constants are `{ vertexBufferIndex, usePackedNormals }` at root
  slot 0 ([mesh_vs.hlsl](../../../runtime/shaders/runtime/gbuffer/mesh_vs.hlsl):18;
  [root_slots.h](../../../runtime/engine/renderer/core/pipeline/root_slots.h):17,30
  shows the slot sized for at least 4 values - confirm the root-signature count
  before adding the two constants F-002 and F-004 need).
- `DrawCommand.entityId` is the packed EnTT id of the producing entity
  ([draw_list.h](../../../runtime/engine/renderer/core/gbuffer/draw_list.h):169-172,
  set at [scene_sync.cpp](../../../runtime/engine/renderer/scene/scene_sync.cpp):311);
  the draw list is rebuilt every frame (:95-311). Mobility is on the command (:158).
- Velocity buffer convention today: RG16F, render-resolution pixels,
  `current - previous`, +x right, +y down, sky = 0
  ([velocity_ps.hlsl](../../../runtime/shaders/runtime/postprocess/velocity_ps.hlsl):11-12,100-101,131-132);
  TAA consumes exactly that (`historyUV = uv - vel * texelSize`,
  [taa_resolve_cs.hlsl](../../../runtime/shaders/runtime/postprocess/taa_resolve_cs.hlsl):311-313,
  rescaled to output pixels at :315).
- Existing analogues for history validity: SSGI resets on
  `resetHistory || m_lastFrame + 1 != frameIndex || sceneRevision changed`
  ([ssgi_renderer.cpp](../../../runtime/engine/renderer/gi/ssgi_renderer.cpp):373);
  the path tracer resets on a reset generation and on a view-matrix delta
  ([frame_lighting.cpp](../../../runtime/engine/renderer/frame/frame_lighting.cpp):88-109);
  GTAO keeps `m_historyValid` "false on first frame / after resize"
  ([gtao_pass.h](../../../runtime/engine/renderer/passes/postprocess/gtao_pass.h):307).

---

## F-001 (3.2) - Temporal matrix / content / state validity

### Root cause

Two booleans stand in for three facts, and neither is reset where the facts change:

- `m_prevViewProjValid` ([scene_renderer_impl.h](../../../runtime/engine/renderer/scene/scene_renderer_impl.h):364)
  is set true at the end of **every** post-process registration, whether or not
  TAA ran ([frame_postprocess.cpp](../../../runtime/engine/renderer/frame/frame_postprocess.cpp):374-375),
  and is reset only by `CreateTaaResources`/`DestroyTaaResources`
  ([scene_temporal_resources.cpp](../../../runtime/engine/renderer/scene/scene_temporal_resources.cpp):36,158),
  i.e. on init (`scene_renderer_init.cpp:722`), resize (:1274) and shutdown (:1628).
  It gates five consumers: TAA `firstFrame` (frame_postprocess.cpp:269), the
  velocity pass ([frame_geometry.cpp](../../../runtime/engine/renderer/frame/frame_geometry.cpp):395),
  SSGI reset ([frame_gi_passes.cpp](../../../runtime/engine/renderer/frame/frame_gi_passes.cpp):134),
  SSR first frame (:671) and motion blur (frame_setup.cpp:530). It says "a
  previous matrix exists", not "the previous matrix is one frame old and from
  the same camera".
- `m_taaHistoryWritten[2]` (scene_renderer_impl.h:327) is set at
  frame_postprocess.cpp:349 immediately after `m_upscaler->Execute(cmd, input)`
  (:348), which is `void` ([temporal_upscaler.h](../../../runtime/engine/renderer/passes/postprocess/temporal_upscaler.h):182).
  Every backend has a silent no-write path: TAA returns when the PSO is invalid
  ([taa_upscaler.cpp](../../../runtime/engine/renderer/passes/postprocess/taa_upscaler.cpp):120-121),
  TSR at [tsr_upscaler.cpp](../../../runtime/engine/renderer/passes/postprocess/tsr_upscaler.cpp):185,255,333,
  DLSS at [dlss_upscaler.cpp](../../../runtime/engine/renderer/passes/postprocess/dlss_upscaler.cpp):551
  and its stub (:695), FSR2's stub ([fsr2_upscaler.cpp](../../../runtime/engine/renderer/passes/postprocess/fsr2_upscaler.cpp):563)
  and its dispatch-error path (:529-534). The flag is reset in
  `CreateTaaResources` (:35) but **not** in `DestroyTaaResources` (:142-159)
  and **not** in `SetUpscalerType` ([scene_upscaler.cpp](../../../runtime/engine/renderer/scene/scene_upscaler.cpp):19-47),
  so a TAA->TSR switch (from `ApplyCVars`, `scene_renderer_init.cpp:1062-1066`,
  or Lua, `lua_renderer_api.cpp:367-371`) feeds the old backend's history to the
  new one as valid.
- TAA activity has three gates - `m_taaEnabled` and the pass inspector
  (frame_setup.cpp:436-440), `m_shadowDebugMode != 1` (frame_postprocess.cpp:264)
  - and none touches either flag. When TAA resumes after N inactive frames,
  `firstFrame` is false (matrix "valid", `written[readIdx]` still true) and the
  resolve reprojects N-frame-old content through a one-frame-old matrix.
  `m_taaHistoryIndex` flips only when TAA runs (:363), so the read slot's age is
  unbounded.
- Camera cuts: nothing detects them for TAA. `CameraManager::SetActiveCamera`
  ([camera_manager.h](../../../runtime/engine/renderer/camera/camera_manager.h):51)
  has no renderer link; the PT's delta test (frame_lighting.cpp:99-108) is the
  only cut detector and it is PT-local.
- Scene changes: `uz_load_scene` loads into the world and re-scans Lua
  ([runtime.cpp](../../../runtime/engine/app/runtime.cpp):863-869) without any
  renderer call; the renderer only sees a different draw list the next frame.
- **Already satisfied - resource state.** Since 466df62e the history ping-pong
  transitions go through the registry (`m_rgStates->Transition`,
  frame_postprocess.cpp:304-306 and :354) and fresh textures derive their state
  from the descriptor (`render_graph_states.cpp:31-36`, per 10). No barrier
  assumes a previous state any more. Part (c) of 3.2 needs no new mechanism;
  the only residual is the missing reset in `DestroyTaaResources`.

### Options

| | A. `TemporalHistoryState` value type with a per-frame key (recommended) | B. Keep both flags, add resets at every toggle site | C. Shader-side history rejection only |
|---|---|---|---|
| Single truth | yes: one object decides matrix and content validity | no: 5+ sites (`SetUpscalerType`, pass inspector, shadow debug, scene load, camera switch) and each future gate | no |
| Covers gaps in TAA activity | automatic (frame continuity) | only if every gate is instrumented | no |
| Camera cut / scene change | key fields + explicit `Invalidate` | explicit calls only | partial (disocclusion) |
| Testable headless | yes (header-only, pure) | only by GPU run | no |
| Code | ~150 lines header + ~12 call-site edits | ~10 scattered edits | shader work, does not fix invalid reads |

A is the same shape as doc 11's `OwnerCommandQueue`: a header-only engine
primitive with a contract, exercised by `test_runtime.exe`.

### Recommended change (Option A)

**1. Primitive** `runtime/engine/renderer/scene/temporal_history_state.h`
(header-only, no RHI includes; math types only):

```cpp
namespace uz {
enum class TemporalReset : uint8_t {
    None, FirstUse, ResourcesRecreated, FrameGap, UpscalerChanged, OutputSizeChanged,
    SceneChanged, CameraCut, ExecuteFailed, Explicit };

struct TemporalFrameKey {           // filled once per frame at the end of SetupFrameData
    uint32_t frameIndex;            // ctx.frameIndex
    uint32_t upscalerType;          // static_cast<uint32_t>(m_upscalerType)
    uint32_t outputW, outputH;      // history size = ctx.vpWidth/vpHeight
    uint32_t cameraId;              // m_cameraManager->GetActiveCameraId()
    Vec3     cameraPos, cameraFwd;  // ctx.perFrame.cameraPos; forward from the view matrix
    Mat4     unjitteredViewProj;    // ctx.unjitteredViewProj
};
struct TemporalThresholds { float cutDistanceCm = 300.f; float cutCosAngle = 0.866f; /* 30 deg */ };

struct TemporalDecision {
    bool matrixValid  = false;      // prevViewProj is one frame old, same camera, same size, no cut
    bool contentValid = false;      // history[readSlot] was written last frame by this upscaler
    TemporalReset matrixReason = TemporalReset::FirstUse, contentReason = TemporalReset::FirstUse;
    Mat4 prevViewProj;              // meaningful iff matrixValid
    [[nodiscard]] bool firstFrame() const noexcept { return !(matrixValid && contentValid); }
};

class TemporalHistoryState {
public:
    void ResetAll(TemporalReset why);                 // Create/DestroyTaaResources
    void Invalidate(TemporalReset why);               // explicit, consumed by the next Advance:
                                                      //   SceneChanged -> content; CameraCut -> matrix+content;
                                                      //   UpscalerChanged -> content
    /// Once per frame. Decides for this frame, then records `key` as the previous frame.
    TemporalDecision Advance(const TemporalFrameKey& key, uint32_t readSlot, const TemporalThresholds&);
    /// Only after ITemporalUpscaler::Execute returned true.
    void MarkWritten(uint32_t slot);
    /// Execute returned false: slot stays invalid, counter + reason for diagnostics.
    void MarkFailed(uint32_t slot);
    // introspection for tests/logs: SlotFrame(slot), SlotUpscaler(slot), FailureCount(), LastDecision()
private:
    struct Slot { bool written = false; uint32_t frame = 0, upscaler = 0, w = 0, h = 0; };
    Slot m_slot[2]; bool m_hasPrev = false; TemporalFrameKey m_prev{}; TemporalReset m_pending = TemporalReset::None;
    uint32_t m_failures = 0;
};
}
```

Decision rules inside `Advance` (the contract; document in the header):

- `matrixValid` iff `m_hasPrev && m_prev.frameIndex + 1 == key.frameIndex`
  (else `FrameGap`) `&& same outputW/H` (else `OutputSizeChanged`)
  `&& m_prev.cameraId == key.cameraId && |pos - prevPos| <= cutDistanceCm && dot(fwd, prevFwd) >= cutCosAngle`
  (else `CameraCut`) `&& pending != CameraCut`.
- `contentValid` iff `matrixValid && slot[readSlot].written && slot.frame + 1 == key.frameIndex`
  (else `FrameGap` - this is the TAA-toggle case) `&& slot.upscaler == key.upscalerType`
  (else `UpscalerChanged`) `&& slot size == key size && pending != SceneChanged`.
- `Advance` then stores `key` as `m_prev`, clears `m_pending`. `MarkWritten(slot)`
  stamps the slot with `m_prev.frameIndex/upscalerType/size`. `ResetAll` clears
  both slots and `m_hasPrev`.
- Scene revision is deliberately **not** part of the key: the GDF revision
  (`global_distance_field.cpp:655`) bumps on every mesh-set change, and meshes
  stream in over many frames at startup (`scene_sync.cpp:109` retries unloaded
  meshes), so keying TAA on it would reset history on every streamed mesh - a
  startup-flicker regression. Scene change is an explicit `Invalidate(SceneChanged)`.

**2. Producer contract**: `virtual bool Execute(IRHICommandList*, const UpscalerInput&)`
in temporal_upscaler.h:182 - true iff a history write was recorded. Return
`false` at taa_upscaler.cpp:121, tsr_upscaler.cpp:185/255/333, dlss_upscaler.cpp:551
and :695, fsr2_upscaler.cpp:533 and :563; `true` at the end of each real path.
`UpscalerInput::firstFrame` (:93) keeps feeding TAA `blendWeight` (:177),
TSR `theoricBlendFactor` (:370), DLSS `NVSDK_NGX_Parameter_Reset` (:627) and
FSR2 `reset` (:513).

**3. `SceneRenderer` wiring**:

- Member `TemporalHistoryState m_temporal;` replaces `m_prevViewProj`,
  `m_prevViewProjValid` (scene_renderer_impl.h:329,364) and `m_taaHistoryWritten`
  (:327). `FrameContext` gains `TemporalDecision temporal;`
  ([scene_renderer_frame.h](../../../runtime/engine/renderer/scene/scene_renderer_frame.h):143-148 area).
- frame_setup.cpp, after :562: build the key, `ctx.temporal = m_temporal.Advance(key, ctx.taaReadIdx, thresholds)`;
  thresholds from new CVars `r.TAA.CameraCut.Distance` (cm, 300) and
  `r.TAA.CameraCut.Angle` (deg, 30), registered next to `cv_TAAJitterSeqLen`.
  Motion blur gate (:530) reads `ctx.temporal.matrixValid`.
- frame_postprocess.cpp: :269 becomes `const bool firstFrame = ctx.temporal.firstFrame();`;
  :348-349 become `if (m_upscaler->Execute(cmd, input)) m_temporal.MarkWritten(writeIdx); else m_temporal.MarkFailed(writeIdx);`
  with a one-shot `UZ_LOG(Renderer, Warning, ...)` on the first failure per
  upscaler; delete :374-375 (the store now lives in `Advance`); :369 reads
  `ctx.temporal.prevViewProj`.
- frame_geometry.cpp:395-397, frame_gi_passes.cpp:134,671,724 and
  frame_particles.cpp:462 read `ctx.temporal.matrixValid` / `ctx.temporal.prevViewProj`.
  A camera cut therefore also zeroes velocity and resets SSGI/SSR that frame,
  which is the correct behaviour for those passes.
- scene_temporal_resources.cpp: `CreateTaaResources` :34-36 and
  `DestroyTaaResources` :157-158 call `m_temporal.ResetAll(ResourcesRecreated)`.
  scene_upscaler.cpp `SetUpscalerType`, after :28: `m_temporal.Invalidate(UpscalerChanged)`
  (the key check would catch it anyway; the explicit call carries the reason).
- Public: `void SceneRenderer::InvalidateTemporalHistory(TemporalReset why)` in
  scene_renderer.h next to `ResetExposureHistory` (:175). Call it with
  `SceneChanged` from `uz_load_scene` after runtime.cpp:863 (and from any other
  non-test caller of `uz::LoadScene` - grep `LoadScene(` under `runtime/engine`,
  `runtime/app`; the F-011 loader transaction in doc 11 is the natural commit
  point) and with `CameraCut` from a Lua `renderer.notifyCameraCut()` binding
  in lua_renderer_api.cpp (optional; the key's `cameraId` already covers
  `SetActiveCamera`, the thresholds cover teleports).
- Diagnostics: CVar `r.TAA.LogDecisions` (bool, 0) logs
  `frame, matrixValid/reason, contentValid/reason, read, write` each frame; the
  GPU harness below asserts on this line.

### Acceptance criteria

1. Frame 0 and the first frame after resize, upscaler switch, or
   `InvalidateTemporalHistory` have `firstFrame == true`; the frame after a
   successful write with an unchanged key has `firstFrame == false`.
2. TAA inactive for N >= 1 frames then active: the first active frame is a
   first frame (`contentReason == FrameGap`); velocity/SSGI/SSR see
   `matrixValid` true if the camera was continuous (the matrix did not go stale).
3. An `Execute` that returns false leaves its slot unwritten; the next frame is
   a first frame (`contentReason == FrameGap`), `FailureCount()` increments and
   exactly one warning is logged per upscaler.
4. Camera id change, a position jump above the threshold, or a forward-vector
   turn above the threshold in one frame give `matrixValid == false`
   (`CameraCut`); SSGI/SSR/motion blur/velocity react through `ctx.temporal`.
5. After `uz_load_scene`, the next frame has `contentValid == false`
   (`SceneChanged`) while `matrixValid` is unchanged.
6. `DestroyTaaResources` + `CreateTaaResources` leave no slot written and no
   previous key. No remaining reader of `m_prevViewProjValid`,
   `m_taaHistoryWritten` or `m_prevViewProj` outside the primitive (grep).
7. GPU (deferred): the decision log and the region dump agree on the first
   frames listed in 1-5, and a scene swap shows no ghost of the previous scene
   in the TAA output (difference against a no-history reference frame below
   the existing region-dump threshold).

### Test plan

`runtime/tests/engine/renderer/test_temporal_history_state.cpp` (pure, headless):

- `TemporalHistoryState.FirstFrame_NoPrev_BothInvalid`
- `TemporalHistoryState.SecondFrame_WrittenSameKey_BothValid`
- `TemporalHistoryState.ExecuteFailed_SlotNotWritten_NextFrameIsFirst` -
  negative control: the current code marks the slot written regardless.
- `TemporalHistoryState.TaaGap_ReadSlotStale_ContentInvalid_MatrixValid` -
  negative control for the current two-flag logic (which would reproject).
- `TemporalHistoryState.UpscalerChanged_ContentInvalid`
- `TemporalHistoryState.OutputSizeChanged_BothInvalid`
- `TemporalHistoryState.CameraIdChange_IsCut`, `TeleportBeyondDistance_IsCut`,
  `TurnBeyondAngle_IsCut`, `SmallMotion_IsNotCut`
- `TemporalHistoryState.InvalidateSceneChanged_ContentOnly`
- `TemporalHistoryState.ResetAll_ClearsSlotsAndPrev`
- `TemporalHistoryState.PingPong_ReadSlotAgeTracksLastWrite`
- `test_fsr2_upscaler.cpp` (exists): stub `Execute` returns `false`.
- `test_render_graph_states.cpp` already covers fresh/recreated handle states
  (10); no new state test is needed for part (c).

GPU harness (deferred verification; needs the engine): a Lua-driven sequence
using `renderer.setAntiAliasing` (lua_renderer_api.cpp:367-371), a viewport
resize, two `uz_load_scene` calls and a scripted teleport, run with
`r.TAA.LogDecisions=1` and a region dump; a script
`scripts/rendering/check_temporal_decisions.py` asserts the expected
`firstFrame` frames and the no-ghost criterion (7).

### Dependencies and risks

Do this first; 3.3 (F-005) consumes the `bool Execute` and `MarkFailed` to
select a valid tonemap input - F-001 does **not** change the tonemap selection
at frame_postprocess.cpp:360 (that stays with 3.3). Risks: false-positive cuts
at very high camera speed cause a one-frame history reset (tunable CVars); the
velocity/SSGI/SSR consumers change behaviour on cuts (intended, but visible in
region dumps - re-baseline the flicker programme's reference frames after
landing). Shader-visible fields are unchanged, so no layout test moves.

---

## F-002 (3.4) - Previous-frame rigid / skinned / deformation velocity inputs

### Root cause

- The velocity buffer is produced by one fullscreen depth-reprojection pass
  (frame_geometry.cpp:391-419, velocity_ps.hlsl:110-132); it encodes camera
  motion only. No previous object transform exists anywhere: `PerObjectCB`
  carries `model` only (constant_buffers.h:377), `DrawCommand` carries
  `worldMatrix` only (draw_list.h:51), and the draw list is rebuilt from the
  ECS each frame (scene_sync.cpp:95-311) with nothing retained.
- Skinned meshes: GPU skinning writes the animated vertices **into the mesh's
  own vertex buffer** every frame (frame_setup.cpp:147-160,
  [mesh.h](../../../runtime/engine/renderer/mesh/mesh.h):132-137,
  `SkeletalMeshSubsystem::DispatchSkinningGpu` at
  [skeletal_mesh_subsystem.cpp](../../../runtime/engine/animation/skeletal_mesh_subsystem.cpp):579),
  so the previous pose is destroyed before the G-buffer draws.
- WPO: the extension API already defines the previous-frame splice
  (`MaterialInjectionPoint::VS_PrevFrame`, "WPO must also deform the prev-frame
  position for correct TAA/DLSS",
  [material_compiler_extension.h](../../../runtime/engine/material/compiler/material_compiler_extension.h):57-59)
  and `WPOMaterialExtension` emits `VfxWPOSplicePrevVS`
  ([wpo_material_extension.cpp](../../../runtime/engine/material/compiler/wpo_material_extension.cpp):28),
  but no production mesh VS has a previous-position path for it to land in.
- Consumer convention is not shared: particles compute per-primitive motion
  vectors from `prevPositionCS` but in **UV units**
  ([vfx_motion_vectors.hlsli](../../../runtime/shaders/runtime/vfx/vfx_motion_vectors.hlsli):7-10,69-72;
  `particle_billboard_ps.hlsl:305,473-474`) and write them into the same
  `m_gbuffer.velocity` target ([frame_particles.cpp](../../../runtime/engine/renderer/frame/frame_particles.cpp):387-393)
  that TAA reads in render pixels (taa_resolve_cs.hlsl:311-313): particle
  motion is attenuated by ~renderWidth. The vendor adapters pass a motion-vector
  scale of +1 (dlss_upscaler.cpp:623-624, fsr2_upscaler.cpp:488-492) while both
  SDKs define the vector as pointing from the current pixel to its
  previous-frame location (`prev - curr`), the opposite sign of this engine's
  `curr - prev` - source-level only, not runtime-verified here (no SDK).

### Options

| | A. Object-velocity overlay pass, moved objects only, depth-test Equal (recommended) | B. Velocity as a 5th G-buffer MRT for every draw | C. Object id in the G-buffer + prev-transform lookup in the fullscreen pass |
|---|---|---|---|
| G-buffer PSO / layout change | none | MRT 4->5 in `GetGBuffer`, `BuildGBufferPipelineDesc` ("must match exactly", material_pipeline.cpp:406-412), decal 3-RT PSO interplay, registry state timeline | new object-id target |
| Handles skinning / WPO | yes (VS reads prev VB / prev splice) | yes | no (rigid only) |
| Extra GPU work | one extra draw per **moving** object (`Equal` depth, 2-channel RT) | none extra, but every draw pays the MRT | none |
| Risk to the GBV/registry baselines of 10 | low (one new RT write pass, same target) | high | medium |
| Precedent | UE4 `FVelocityRendering` separate pass; this repo's `AddVelocityPass` plumbing (`velocity_pass.cpp/h`) | UE5 base-pass velocity | - |

### Recommended change (Option A)

**1. Previous transforms** - `runtime/engine/renderer/core/gbuffer/prev_transform_cache.h`
(header-only):

```cpp
class PrevTransformCache {           // keyed by DrawCommand.entityId (packed EnTT id)
public:
    /// Returns the matrix recorded for `id` at frame-1, else `current` (first sight / gap -> zero object velocity).
    Mat4 Lookup(uint32_t id, const Mat4& current, uint32_t frame) const;
    void Record(uint32_t id, const Mat4& current, uint32_t frame);
    void Sweep(uint32_t frame);      // once per frame: drop entries older than frame-1
    [[nodiscard]] size_t Size() const noexcept;
};
```

`DrawCommand` gains `Mat4 prevWorldMatrix; uint32_t prevVertexBufferSrvIndex = 0; bool hasObjectMotion = false;`
(CPU-side struct, no GPU layout impact). In the sync loop (scene_sync.cpp, where
`cmd.entityId` is set at :311): for `EMobility::Movable` commands
`prevWorldMatrix = cache.Lookup(entityId, worldMatrix, frame); cache.Record(...)`;
for Static/Stationary `prevWorldMatrix = worldMatrix`.
`hasObjectMotion = (prevWorldMatrix != worldMatrix beyond 1e-6) || mesh.gpuSkinned || materialHasWpo`.
`Sweep(frame)` at the top of the sync.

**2. Skinned previous pose** - in `DispatchSkinningGpu`, before each instance's
dispatch: `CopyBuffer(vb -> prevVb)` where `prevVb` is created beside the
skinned VB with the same `UnorderedAccess | StructuredBuffer` usage plus an
SRV, transitions through `m_rgStates` (buffers are registry-tracked,
scene_render_frame.cpp:77). First frame copies the bind pose already seeded by
`Bind()` (skeletal_mesh_subsystem.cpp:331) -> zero skinned velocity on first
sight, which is correct. Expose `prevVbSrvIndex` on the instance/`GPUMesh` and
copy it into `cmd.prevVertexBufferSrvIndex`. Ping-pong VBs would avoid the copy
but would alternate `mesh.vbSrvIndex` for every consumer (G-buffer, shadows,
BLAS); the copy is the smaller change at typical character sizes (10-30 k
vertices x 48 B).

**3. Pass** - `AddVelocityObjectPass` next to `AddVelocityPass` in
velocity_pass.cpp/h, registered in frame_geometry.cpp right after :418 when any
command has `hasObjectMotion` and `ctx.temporal.matrixValid` (F-001):

- `pb.Write(ctx.velocityRes, RenderTarget)`; `pb.Read(ctx.depthRes, DepthStencilRead)`
  (the translucent pass does the same at frame_translucent.cpp:127-129).
- PSO `PipelineLibrary::GetVelocityObject(bool twoSided)` in
  pipeline_geometry.cpp: VS `velocity_object_vs.hlsl`, PS `velocity_object_ps.hlsl`,
  RT0 RG16F, depth D32 read-only with comparison **Equal** (add the comparison
  to `RHIDepthStencilDesc` if absent), cull Back / None matching the
  G-buffer PSO of the draw (materialFlags bit 3), no blend.
- Constants: b1 = the frame's `ctx.perFrameAlloc` (same `viewProj` as
  mesh_vs.hlsl:29-31 - required for `Equal` to hit); b2 =
  `VelocityObjectCB { row_major float4x4 model, prevModel, unjitteredViewProj, prevViewProj; float2 screenSize; float2 pad; }`
  (272 B, mirrored in C++ with a `static_assert` and added to
  `test_constant_buffers.cpp`). Root constants slot 0:
  `{ vertexBufferIndex, usePackedNormals, prevVertexBufferIndex }`
  (0 = same buffer as current; the fourth slot is taken by F-004 - agree the order
  `{ vb, packed, color, prevVb }` once).
- VS: fetch `p` (and `pPrev` from `prevVertexBufferIndex` when non-zero, same
  packed/unpacked decode as mesh_vs.hlsl:47-63); `worldPos = mul(float4(p,1), model)`;
  `o.positionCS = mul(worldPos, viewProj)` **with the identical expression order
  as mesh_vs.hlsl:78-80**; `o.clipCurr = mul(worldPos, unjitteredViewProj)`;
  `o.clipPrev = mul(mul(float4(pPrev,1), prevModel), prevViewProj)`; both as
  perspective-correct `float4` interpolants (exact: clip x/y/w are affine in
  object space, and jitter leaves w unchanged).
- PS: `if (clipPrev.w <= 1e-6) return 0;` then
  `return NdcToRenderPixels(clipCurr.xy / clipCurr.w, screenSize) - NdcToRenderPixels(clipPrev.xy / clipPrev.w, screenSize);`
  with `NdcToRenderPixels(ndc, size) = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5) * size`
  in a new `shaders/runtime/common/velocity_common.hlsli`, also used by
  velocity_ps.hlsl:122-129 (replace the inline math) so the convention has one
  definition.
- Draw loop mirrors frame_translucent.cpp:171-239 (bind VB/IB per mesh change,
  per-draw CB from the ring, `DrawIndexed`), filtered to `hasObjectMotion`.

**4. Particles** - `ComputeMotionVector(currentCS, prevCS, renderSize)` returns
render pixels (`(currentUV - prevUV) * renderSize`); fix the convention comment
at vfx_motion_vectors.hlsli:7-10 and pass the render size through the particle
CB (`particle_render_pass.h:110` already carries `prevViewProj`; add the size
if no field exists).

**5. WPO (stage 2 of this finding)** - the `MaterialPipeline` builds a
velocity-VS variant for WPO materials: `VS_PreViewProj` splice on the current
position and `VS_PrevFrame` splice on the previous one, evaluated with
`totalTime - deltaTime` (`timeParams.x - timeParams.y`) and `prevModel`. Until
stage 2 lands, WPO draws keep `hasObjectMotion = true` (prev = rigid prev) and
the capability matrix (5.3) lists "WPO velocity: rigid part only".

**6. Documented consumer convention** (velocity_pass.h:18,51-62,117 and
`velocity_common.hlsli` header):

> Velocity texel = position of the surface point in the **current frame's render
> pixels** minus its position in the previous frame expressed in the same pixel
> grid, both from **unjittered** matrices; +x right, +y down; no geometry or
> `clipPrev.w <= 0` = 0. Consumers reproject with `historyUV = uv - vel * texelSize`
> (TAA/TSR). Output-resolution consumers scale by `output / render` (taa_resolve_cs.hlsl:315).
> Vendor adapters that define the vector as `prev - curr` (DLSS, FSR2) pass scale
> `-1` on both axes; confirm the unit expectation (pixel vs. UV) against the SDK
> header comment in the FSR2/DLSS tasks. A resolution change invalidates the
> previous frame through F-001 rather than through the encoding.

Set `MV_Scale_X/Y = -1.0f` at dlss_upscaler.cpp:623-624 and
`motionVectorScale = {-1, -1}` at fsr2_upscaler.cpp:491-492 in the same change,
flagged as deferred verification.

### Acceptance criteria

1. Rigid cube translating `d` cm/frame, fixed camera: at the cube's pixels the
   velocity equals the CPU-projected displacement within 0.1 px (RG16F has
   ulp <= 1/128 px below 16 px), zero elsewhere; with jitter on and off.
2. Same cube, moving camera: velocity = object + camera term within 0.1 px;
   a static cube under a moving camera equals the fullscreen pass alone
   (negative control: the object pass did not redraw it - draw count unchanged).
3. Skinned mesh rotating one bone 2 deg/frame: per-pixel velocity within 0.25 px
   of CPU skinning of the same vertex for both poses.
4. Particle with constant screen motion: velocity in render pixels matches the
   billboard's projected displacement (today it is ~1000x too small).
5. A WPO sine offset of amplitude `A` (stage 2): velocity equals the projected
   delta between `t` and `t - dt` within 0.25 px.
6. Screen percentage 50 % and 100 % give the same velocity in output pixels
   after the TAA rescale; the frame after a percentage change is a first frame
   (F-001), not a wrong-scale reprojection.
7. Objects absent for one frame (culled/hidden) reappear with zero object
   velocity, never with a stale matrix.

### Test plan

`runtime/tests/engine/renderer/test_velocity_pass.cpp`:

- Pure: `PrevTransformCache.FirstSight_ReturnsCurrent`,
  `PrevTransformCache.NextFrame_ReturnsRecorded`,
  `PrevTransformCache.GapOfOneFrame_ReturnsCurrent_AndSweepEvicts`,
  `PrevTransformCache.StaticMobility_NeverRecorded` (via the sync helper),
  `VelocityConvention.NdcToRenderPixels_MatchesFullscreenPassFormula`
  (C++ mirror of `velocity_common.hlsli` against the values velocity_ps.hlsl
  produced for a known matrix pair), `VelocityObjectCB.LayoutIs272Bytes`
  (+ the existing reflection script `scripts/rendering/test_cbuffer_layouts.py`
  gains the new cbuffer).
- GPU-gated (`GTEST_SKIP` without a device, pattern of
  `test_headless_frame_capture.cpp`): `VelocityObjectPass.TranslatingCube_FixedCamera_MatchesProjection`,
  `..._MovingCamera_SumsCameraAndObject`, `..._StaticCube_NotRedrawn`,
  `..._SkinnedBoneRotation_MatchesCpuSkinning`,
  `..._ParticleMotion_InRenderPixels`, `..._ScreenPercentage50_RescalesToOutput`,
  reading back `m_gbuffer.velocity`. Stage 2 adds `..._WpoSine_MatchesPrevSplice`.
- Vendor sign: covered only when the FSR2/DLSS SDK builds run
  (`phase2_fsr2-full-integration`); record as deferred.

### Dependencies and risks

After F-001 (the pass is gated on `ctx.temporal.matrixValid` and the previous
VP comes from `ctx.temporal`). Coordinate the root-constant order with F-004
and the instancing work of 5.1 (an instanced path must carry `prevModel` per
instance). Risks: `Equal` depth needs bit-identical clip positions - share the
expression and the same `viewProj` CB, and if a driver still drops pixels fall
back to `GreaterEqual` with the velocity VS's own depth; the per-instance VB
copy costs bandwidth on crowds (ping-pong is the fallback); extra draws for
moving objects (profile with the GPU profiler, `r.GpuProfiler.Log`).

---

## F-004 (3.5) - Real generated-material context

### Root cause

[gbuffer_ps.hlsl](../../../runtime/shaders/runtime/gbuffer/gbuffer_ps.hlsl):124-133
calls `ComputeMaterialOutputs` with `float4(1,1,1,1)` for `vertexColor`,
`float3(0,0,0)` for `cameraPosition` and `objectPosition`,
`input.positionCS.xy / float2(1920, 1080)` for `screenUV` and `0.0` for
`totalTime`. The translator consumes every one of them: `ObjectPosition`
([hlsl_material_translator.cpp](../../../runtime/engine/material/compiler/hlsl_material_translator.cpp):428-430),
`VertexColor` (:436-438), `CameraVector` (:440-444), `ScreenPosition` (:446-448),
`Time` (:450-452), `Fresnel` (:507-513), `Panner` (:515-522); the signature is
emitted at :768-776 and injected at the `material_body.hlsli` marker by
[material_pipeline.cpp](../../../runtime/engine/renderer/material/material_pipeline.cpp):386-404.
The bake path passes the same constants ([material_baker_gpu.cpp](../../../runtime/engine/material/material_baker_gpu.cpp):77-80).

What is already available and simply not passed: `cameraPos` (c4) and
`timeParams` (c5) are declared in this very shader's `PerFrameCB`
(gbuffer_ps.hlsl:28-29) and populated every frame (frame_constants.cpp:191);
the object translation is `obj_model[3].xyz` (:55, the same read
translucent_ps.hlsl:205 makes). What is genuinely missing: the render size
(no field in either CB, both full) and a vertex colour stream - `Vertex` (48 B)
and `VertexPacked` (32 B) have none ([vertex.h](../../../runtime/engine/renderer/mesh/vertex.h):39-46,68-76);
`VertexFormatFlags::HasColor` is only a layout flag
([vertex_layout.h](../../../runtime/engine/renderer/mesh/vertex_layout.h):41).

### Options

| | A. Pass what exists; derive `screenUV` from clip space in the VS; optional per-mesh colour side-stream (recommended) | B. Grow `PerFrameCB` to 1280 B with `renderSize`; widen `Vertex` to 64 B with colour | C. Pass what exists, keep colour white, declare VertexColor unsupported |
|---|---|---|---|
| CB layout tests | untouched | `static_assert`s at constant_buffers.h:313-321, every `packoffset` mirror, `test_cbuffer_layouts.py` | untouched |
| Mesh assets / BLAS / skinning | untouched (colour is a separate SRV, 4 B/vertex, only when present) | every importer, `GPUMesh`, BLAS build, `gpu_skinning_cs` stride | untouched |
| Resolution-independent `screenUV` | by construction (NDC -> UV) | yes | yes |
| Meets 3.5's "vertex colors" acceptance | yes (procedural/test meshes carry a stream; importer export is a follow-up) | yes | **no** |

### Recommended change (Option A)

**1. Vertex shader** (mesh_vs.hlsl): add to `VSOutput` (:4-10)
`noperspective float2 screenUV : TEXCOORD2;` and `float4 color : COLOR0;`
(the PS variants that do not read them - `mesh_ps.hlsl`, `translucent_ps.hlsl` -
keep their shorter `PSInput`; D3D12 permits a PS input subset). Compute
`float4 clip = mul(worldPos, viewProj); output.screenUV = (clip.xy / clip.w) * float2(0.5, -0.5) + 0.5;`
(screen-linear, hence `noperspective`, hence exact at every pixel centre; equals
`SV_Position.xy / renderSize` including this frame's jitter, at any
resolution). Colour: push constant slot 2 `colorBufferIndex`;
`output.color = push.colorBufferIndex != 0 ? UnpackRgba8Unorm(colors[vertexID]) : float4(1,1,1,1);`
(`StructuredBuffer<uint>` RGBA8 UNORM, sRGB-decoded at import, linear in the
buffer). SM5 non-bindless keeps white (documented downlevel limitation).

**2. Pixel shader** (gbuffer_ps.hlsl:124-133):

```hlsl
ComputeMaterialOutputs(
    uv, input.positionWS, normalize(input.normalWS),
    input.color,                 // COLOR0 or (1,1,1,1) without a colour stream
    cameraPos.xyz,               // PerFrameCB c4
    obj_model[3].xyz,            // object world translation
    input.screenUV,              // render-viewport UV, pixel centre, [0,1], y down
    timeParams.x,                // seconds since renderer start
    materialParams, albedo, metallic, roughness, graphNormal, emissive, opacity, ao);
```

`PSInput` (:7-13) mirrors the two new interpolants.

**3. Mesh data**: `Mesh::vertexColors` (`std::vector<uint32_t>`, empty = none,
mesh.h next to :129) uploaded as a `StructuredBuffer<uint>` with an SRV
(`GPUMesh::colorSrvIndex`, 0 = none) when non-empty; `RegisterProceduralMesh`
accepts it; the G-buffer draw loop pushes `{ vb, packed, colorSrv, prevVb }`
(order agreed with F-002). Importer export of `COLOR_0` is a separate task;
the acceptance fixture is a procedural mesh.

**4. Contract** (document at the signature, hlsl_material_translator.cpp:768-776,
and in gbuffer_ps.hlsl):

| input | source | units / notes |
|---|---|---|
| `uv` | `TEXCOORD0 * uvScale + uvOffset` | material UV |
| `positionWS` | interpolated world position (after WPO) | cm |
| `normalWS` | normalized vertex normal, no normal map | world |
| `vertexColor` | `COLOR0` stream or `(1,1,1,1)` | linear RGBA |
| `cameraPosition` | `cameraPos.xyz` | cm, world |
| `objectPosition` | `obj_model[3].xyz` | cm, world |
| `screenUV` | VS-derived viewport UV | [0,1], +y down, render resolution, includes TAA jitter |
| `totalTime` | `timeParams.x` | seconds, float32 (1 ms resolution after ~4.6 h) |
| bake (`material_baker_gpu.cpp:77-80`) | time 0, white, `uv` as `screenUV`, camera at origin | documented snapshot values |

The translator additionally reports `MaterialCompileResult::contextMask`
(bits: VertexColor, Camera, ObjectPosition, ScreenPosition, Time) so the baker
and editor can flag graphs whose baked result is a time/camera snapshot.

### Acceptance criteria

1. A graph `BaseColor = float3(ScreenPosition, 0)` rendered at 1280x720 and
   1920x1080 (and at 50 % screen percentage) writes albedo equal to the pixel
   centre's viewport UV within 1/renderSize at every sampled pixel; the two
   resolutions agree at corresponding normalized positions (today the 1080p
   divisor makes them differ).
2. `BaseColor = frac(Time)` advances by `dt` per frame and matches
   `timeParams.x` of the same frame (region dump or readback).
3. `Fresnel`/`CameraVector` outputs change when only the camera moves and are
   consistent with `normalize(cameraPos - positionWS)` at a probed pixel
   (today constant).
4. `ObjectPosition` follows the entity's world translation when only the
   object moves.
5. A procedural mesh with a colour stream renders `VertexColor` exactly; the
   same graph on a mesh without a stream renders white; `contextMask` has the
   VertexColor bit in both cases.
6. No CB layout test changes; all existing material/gbuffer/pipeline tests pass;
   the embedded shader set is rebuilt (VS output signature changed).

### Test plan

- `runtime/tests/engine/material/test_material_compiler_extension.cpp`
  (exists): `MaterialTranslator.ContextMask_ReflectsReferencedInputs` (one graph
  per bit; negative control: a texture-only graph has mask 0).
- `scripts/rendering/test_material_context.py` (alongside
  `test_cbuffer_layouts.py`): DXC-compile `gbuffer_ps.hlsl` with
  `UZ_MATERIAL_GRAPH` and a graph that uses every context node, then assert via
  reflection that `cameraPos` and `timeParams` are **used** variables of
  `PerFrameCB` and that no `1920`/`1080` literal survives in the DXIL constants -
  negative control: the current shader reports them unused.
- GPU-gated gtests in `runtime/tests/engine/renderer/test_gbuffer.cpp`
  (`GTEST_SKIP` without a device): `MaterialContextDraw.ScreenUV_TwoResolutions_Match`,
  `MaterialContextDraw.Time_AdvancesWithFrames`,
  `MaterialContextDraw.CameraVector_FollowsCamera`,
  `MaterialContextDraw.ObjectPosition_FollowsTransform`,
  `MaterialContextDraw.VertexColor_StreamDecoded_And_WhiteWithoutStream`
  (deferred verification).
- `test_gpu_mesh.cpp`: `GpuMesh.ColorStream_UploadedOnlyWhenPresent`.

### Dependencies and risks

After F-001 only by checklist order; code-wise independent. Shares the VS
push-constant layout and the root-constant count check with F-002. Risks: the
VS output signature change recompiles every mesh PSO (shader cache warm-up;
Sponza frame-60 spread is the known cold-cache symptom) and the embedded
shaders must be rebuilt; `screenUV` includes jitter by design (document it so
nobody "fixes" it into a jitter-free value that disagrees with the raster).

---

## F-007 (3.6) - Supported glass geometry / material contract

### Root cause

[translucent_ps.hlsl](../../../runtime/shaders/runtime/gbuffer/translucent_ps.hlsl):160-253
(smID 9):

- Geometry: the exit point is computed against a sphere whose centre is
  `obj_model[3]` and radius `|row 0|` (:203-206, :209-216). Any non-sphere
  mesh, off-centre pivot or nonuniform scale (rows 1/2 ignored) breaks it; the
  Sponza window panes are planes promoted to ThinTranslucent by
  `transmission > 0` ([scene_loader.cpp](../../../runtime/engine/ecs/loaders/scene_loader.cpp):904-912).
- Transmission: the "refraction" samples the prefiltered IBL cubemap (:233-241).
  The scene behind the glass is never sampled, although
  `TranslucentCopyScene` copies the HDR target into `m_translucentSceneColor`
  every frame a translucent draw exists ([frame_translucent.cpp](../../../runtime/engine/renderer/frame/frame_translucent.cpp):92-124;
  texture at scene_temporal_resources.cpp:301-317) and injects its SRV into
  `heightmapSrvIndex` (:194-195, :223-224) - the shader declares that field
  (:77) and never reads it. Dead copy.
- Compositing: the branch returns alpha 1.0 (:252) under the premultiplied
  `(One, InvSrcAlpha)` blend ([pipeline_geometry.cpp](../../../runtime/engine/renderer/core/pipeline/pipeline_geometry.cpp):337-347),
  so glass fully replaces the background; the header claims "Alpha = Fresnel" (:11-13).
- Authoring: `transmission`, `ior`, `thicknessScale` exist on the asset
  ([material_asset.h](../../../runtime/engine/renderer/material/material_asset.h):134-148);
  `transmission` is packed into `materialFlags` bits 28-31 (scene_sync.cpp:505-511)
  and ignored by the shader; IOR is derived from `specular` (:163-166);
  `thicknessScale` is not transported; blur uses `roughnessMultiplier` (:239).
- Ordering: translucent draws run in draw-list order (:171-239); whether
  `SortKey::Transparent` (draw_list.h:45-48) is applied back-to-front before this
  loop is unverified - check it during implementation.
- Culling: front faces only (pipeline_geometry.cpp:334), compatible with a thin model.

### Options

| | A. Declare sphere-only; fix radius/centre from the mesh bounds x per-axis scale; reject other meshes at authoring | B. Thin-surface screen-space transmission with authored thickness - Three.js `MeshPhysicalMaterial.transmission` / glTF `KHR_materials_transmission` + `KHR_materials_volume` (recommended) | C. General volume transmission (ray-queried exit, Falcor-matched) |
|---|---|---|---|
| Covers existing content (Sponza panes, any mesh) | no | yes | yes |
| Shows the scene behind the glass | no (IBL only) | yes, from the existing copy | yes |
| Reference already cited by the code | - | translucent_ps.hlsl:5, frame_translucent.cpp:7-8 (the parity repo is Three.js-based) | needs research + matched Falcor references (02, F-007) |
| Change surface | shader + validator | shader (smID 9 branch), 2 sync lines, docs | new pass, RT infrastructure, out of 3.6 scope |
| Honest limits | explicit sphere-only | explicit thin-slab semantics (no inversion, no glass-through-glass) | - |

### Recommended change (Option B)

**Contract** (write it in material_asset.h:134-148 and translucent_ps.hlsl:149-159):

> `ThinTranslucent` (smID 9) is thin-surface screen-space transmission. Any
> mesh is supported; it is rendered front-faces-only and each shaded point is
> treated as the entry of a slab of local thickness `thicknessScale` (cm,
> scaled per axis by the model matrix) along the refracted direction. The
> transmitted light is the opaque scene colour captured before the translucent
> pass, sampled where the slab's exit point projects; reflection is the
> Fresnel-weighted prefiltered environment; `transmission` lerps between the
> standard translucent result and the glass result; `ior` is authored. Not
> supported: refraction of other translucent surfaces (not in the copy), true
> volumes (a sphere behaves as a slab - lateral offset, no image inversion,
> no caustics), roughness blur of the transmitted image (needs a mip chain),
> thickness maps, shadows through glass, back faces.

**Shader** (translucent_ps.hlsl, smID 9 branch; `clampSampler : register(s2)`
declared as in gbuffer_ps.hlsl:88; rename the HLSL member `heightmapSrvIndex`
to `sceneColorSrvIndex` and `displacementScale` to `glassThickness` with a
"PerObjectCB layout unchanged; field reuse for smID 9" comment):

```hlsl
float  ior        = <from specular as today, :163-166>;
float  thickness  = glassThickness;                                       // cm, local
float  transm     = float((materialFlags >> 28u) & 0xFu) / 15.0;          // bits 28-31
float3 modelScale = float3(length(obj_model[0].xyz), length(obj_model[1].xyz), length(obj_model[2].xyz));
float3 refr       = refract(-V, N, 1.0 / ior);
float3 exitWS     = input.positionWS + normalize(refr) * thickness * modelScale;   // Three.js getVolumeTransmissionRay
float4 exitClip   = mul(float4(exitWS, 1.0), viewProj);                   // same (jittered) VP as the copy
float2 exitUV     = saturate(exitClip.xy / max(exitClip.w, 1e-4) * float2(0.5, -0.5) + 0.5);
float3 transmitted = sceneColor.SampleLevel(clampSampler, exitUV, 0).rgb * baseColorTint;   // Three.js tints by diffuseColor
float3 reflection  = prefiltered(R) * skyLightIntensity * skyTint;        // :176-184, drop the 0.5 at :186
float3 glass       = (1.0 - fresnel) * transmitted + fresnel * reflection + emission;
float3 standard    = <the non-glass result, :257-279>;
return float4(lerp(standard * matOpacity, glass, transm), lerp(matOpacity, 1.0, transm));   // premultiplied
```

TIR (`refract` returns 0) keeps `exitUV` at the pixel's own UV (zero offset).
Keep the region-dump record (:249-251) with `transmitted` in the `iblDiffuse`
slot so the diagnostics expose the transmission source.

**Sync** (scene_sync.cpp, in the ThinTranslucent branch at :509-511): also
`cmd.displacementScale = mat->thicknessScale;` and, when `ior` is authored,
`specular = ((ior - 1) / (ior + 1))^2 / 0.08` so the existing 8-bit field carries
it (Three.js's own relation, :161-162). The G-buffer loop never draws
`blendMode >= 2`, so the reused fields cannot leak into POM.

**Ordering**: verify the transparent sort; if the list is not back-to-front,
sort the translucent subset by view depth in `RegisterTranslucentPasses`
before the loop (:171).

**Scope note**: Option C (general volume) stays with the path-tracing /
material tasks and the Falcor gate (10); this change claims Three.js parity for
the thin model only.

### Acceptance criteria

1. Flat pane (1 m, `ior` 1.5, thickness 10 cm, checker wall 1 m behind): the
   transmitted sample at the pane's centre pixel comes from the screen location
   predicted by Snell + projection within 1 px; zero offset at normal
   incidence; the checker is visible through the pane (today: sky).
2. Sphere, same material: checker visible, laterally offset, **not** inverted
   (slab semantics), no sphere-centre dependence on the pivot.
3. Nonuniform scale (1, 1, 3): the offset along the scaled axis is 3x the
   uniform case within 1 px; no artefact from `|row 0|`.
4. `transmission = 0` renders the standard translucent path unchanged
   (pixel-identical to before the change); `transmission = 1` renders pure glass.
5. The copy pass is consumed (RenderDoc/PIX or the region dump shows the
   transmitted source) - it is no longer dead work.
6. Sponza window glass: reviewed visually (JPEG montage) against the Three.js
   reference; the parity decision owner signs off the baseline move.

### Test plan

- `runtime/tests/engine/renderer/test_material.cpp` (exists):
  `MaterialFlags.ThinTranslucent_PacksTransmissionAndThickness` (bits 28-31
  and `displacementScale == thicknessScale`), `MaterialFlags.IorAuthored_DrivesSpecularField`.
- `test_pipeline_library.cpp`: translucent PSO still back-cull, depth read-only,
  premultiplied blend (regression that the contract's blend state is unchanged).
- Shader compile check in `scripts/rendering/test_material_context.py` (reuse):
  the translucent PS reflects `sceneColorSrvIndex` as **used** (negative
  control: today `heightmapSrvIndex` is unused).
- GPU-gated `runtime/tests/engine/renderer/test_glass_contract.cpp`
  (`GTEST_SKIP` without a device): `GlassContract.Pane_SceneBehindAtPredictedOffset`,
  `GlassContract.Sphere_LateralOffset_NoInversion`,
  `GlassContract.NonuniformScale_OffsetScalesPerAxis`,
  `GlassContract.TransmissionZero_UnchangedStandardPath`,
  and the negative control `GlassContract.Before_PaneShowsSkyNotScene` recorded
  once with the current shader through the existing LIGHTING_EXTRA records
  (+5 `sourceColor`, +7 `iblDiffuse`, translucent_ps.hlsl:88-117). Deferred verification.
- `DrawListTest.TransparentCommands_SortedBackToFront` if the sort turns out
  to be missing.

### Dependencies and risks

After F-004 by checklist order (shared `PerObjectCB` field-reuse conventions;
no code dependency). Risks: the Sponza window glass changes appearance (parity
baselines move - coordinate with the keep-neutral decision); 4-bit
`transmission` quantization (16 levels, adequate for a lerp weight; documented);
the copy holds only opaque + earlier translucent draws, so overlapping glass
shows un-refracted glass behind it (documented limitation); `SampleLevel` mip 0
only - frosted glass needs the stage-2 mip chain.

---

## Order of work and cross-finding notes

1. **F-001** - `TemporalHistoryState`, `bool Execute`, call-site rewiring,
   explicit scene-load invalidation. Pure gtests close the state-truth half;
   the GPU sequence is deferred. Feeds 3.3 (F-005).
2. **F-004** - VS interpolants, PS call, colour side-stream, `contextMask`.
   Lands the push-constant order `{ vb, packed, color, prevVb }` that F-002 reuses.
3. **F-002** - prev-transform cache, skinned prev VB copy, object-velocity
   pass, particle units, vendor sign; WPO velocity as its stage 2 through the
   existing `VS_PrevFrame` splice. Prerequisite of moving-object temporal
   acceptance in 4.1.
4. **F-007** - thin-transmission contract and shader; consumes the already
   existing copy pass.

Shared implementation checks: the root signature's 32-bit constant count at
slot 0 (F-002/F-004); `RHIDepthStencilDesc` comparison enum and
`IRHICommandList::CopyBuffer` availability (F-002); the transparent sort
(F-007). Every GPU-gated test above is deferred verification and must be
recorded as such; the headless gtests and the DXC reflection scripts are the
evidence that can be produced without a device.

---

## Status (2026-10-15)

Design-complete; **not implemented** (open items 3.2/3.4/3.5/3.6 remain `[ ]`).
Part (c) of 3.2 (resource state) is already satisfied by the registry
(frame_postprocess.cpp:304-306,354; 466df62e) and needs only the
`DestroyTaaResources` reset. Three adjacent defects were surfaced and folded in:
particle motion vectors in UV units on a pixel-unit target (F-002 item 4), the
vendor motion-vector sign (F-002 item 6, SDK-unverified), and the dead
translucent scene-colour copy (F-007). Both constant-buffer blocks are full,
which is why every recommendation uses interpolants, side buffers and existing
bit-fields instead of layout growth.
