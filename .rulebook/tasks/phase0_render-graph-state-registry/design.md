# Design: a central resource-state registry for the render graph

From the state audit of 2026-10-08 (89 claims, 80 confirmed; the audit result is summarised in the proposal). The design below is the audit's recommendation, kept verbatim.

Recommendation: option (b), a persistent state registry keyed by texture/buffer handle. This was a read-only review: nothing was edited, built, run or committed. Paths are relative to `E:\UzmiEngine\uzengine` and line numbers are from the working tree, where `dx12_command_list.cpp` and `dx12_device.cpp/.h` have uncommitted edits.

# Fix design: cross-frame resource states

## 1. Verdict

(b) is the only option that fixes the whole cross-frame class with one mechanism. That includes the first frame, Resize and other recreation, and buffer decay. Its logic lives in the render-graph layer, so a MockDevice gtest can check it.

The plain version in the brief ("filled from GetFinalState after Compile, used at Import, reset on destroy/recreate") needs five changes:

1. **Resolve the import state at Compile, not at Import.** Imports happen in SetupFrameData (`frame_setup.cpp:417-441`). RT build, cloud compute and VFX sim are recorded afterwards (`scene_render_frame.cpp:131-218`). Compile runs at `:242`.
2. **An unknown handle starts in the backend's creation state, so no reset hooks are needed.** Handles are generational: `Allocate` bumps the slot generation every time (`rhi_resource_pool.h:49-59`) and `==` compares index and generation (`rhi_types.h:104`). Every recreated texture is therefore a new key, so no stale entry can ever match it.
3. **Buffers never carry a state across a submit.** Under D3D12 decay they are COMMON again, a rule the repo already records (`.rulebook/archive/2026-09-06-phase0_fix-vfx-spawn-multi-particle/tasks.md:17`).
4. **A `Transition`/`Find` API** for code outside the graph (pre-graph, post-graph, the inspector, undeclared resources inside callbacks), so there is one source of truth.
5. **Dedup by handle**, plus an "observe" mode that reports imports whose claimed state disagrees with the tracked one. This check works without the D3D12 debug layer.

## 2. Facts every option must respect

- **Imports are the caller's claim.** The claimed state is copied into `initialState`/`currentState` (`render_graph_builder.cpp:153-156`, `177-180`).
  - Barriers are computed once, in Compile (`213-224` → `411-445`).
  - A barrier is skipped when the states are equal (`430`) or when the tracked state is Undefined (`431-433`).
  - Execute adds nothing at the end (`451-509`).
- **The DX12 RHI trusts `stateBefore`.** It is copied verbatim into the D3D12 barrier (`dx12_command_list.cpp:672-675`). Neither `DX12Buffer` nor `DX12Texture` stores a state (`dx12_resources.h:74`, `:127`).
- **Creation states:**
  - textures start COMMON, or DEPTH_WRITE when the usage includes DepthStencil (`dx12_texture_creation.cpp:165-167`);
  - Default-heap buffers start COMMON, Upload buffers GENERIC_READ, Readback buffers COPY_DEST (`dx12_buffer_creation.cpp:75-81`).
- **Submits of the frame list:**
  - every frame, at `scene_render_frame.cpp:371-372`;
  - on frame 0, at `frame_setup.cpp:94-98`;
  - on sky-bake frames, at `atmosphere_renderer.cpp:777-779`.
- **Recording is single-threaded** (`render_graph_builder.h:29-30`; `RgParallelExecutor` exists only in comments at `:184` and `:283`). Recording order is therefore a valid state timeline for the direct queue.
- **Unit-test reach:** SceneRenderer can only be constructed un-Init'd in tests (`mesh_asset_lookup_test.cpp:38`). The graph can be driven by mocks (`test_render_graph.cpp:27-191`).

## 3. Option (a): per-resource end-state members (the `m_hdrEndState` pattern)

**Code changes**
- About 17 new `RHIResourceState` members, following `scene_renderer.h:913-919`:
  - position, emissive, velocity;
  - ShadowAtlas, LocalShadowAtlas;
  - one for Light_*, one for DBuffer;
  - PT accum[2];
  - APVolume, TransmittanceLUT, SkyCubemap;
  - the two froxel volumes.
- Capture each after Compile (`scene_render_frame.cpp:244-248`) and use it at its import.
- Reset each one at every recreate site:
  - `SceneRenderer::Resize` (`scene_renderer_init.cpp:1167-1221`);
  - the CSM atlas resize (`frame_constants.cpp:1314-1316`);
  - `VolumetricFogPass::Init`.
- The froxel and atmosphere ids are created inside other classes (`volumetric_fog_pass.cpp:136-139`, `atmosphere_renderer.cpp:245-288`). They must be plumbed back to the capture site, or their end state predicted at registration.
- VFX buffers need a hard-coded Common, not a member.

**Coverage**
- Covers only graph-declared resources, one at a time.
- Does not cover manual barriers outside the graph. Those keep their own trackers, for example `m_clusterListState` (`frame_lighting_pass.cpp:90,114,128`) and `m_rayBufferState` (`ddgi_system.cpp:365,398`), which today break on decay and on recreation.

**Risks**
- (a) is the pattern that produced the bug class:
  - the four existing members have exactly one writer each (`scene_render_frame.cpp:245-248`), and Resize (`scene_renderer_init.cpp:1155-1296`) never resets them;
  - three textures borrow albedo's member (`frame_setup.cpp:422-424`).
- Each new import or recreate site needs three to four coordinated edits.
- Correct first frames depend on the frame-0 warmup (`scene_render_frame.cpp:80-100`), which never reruns after Resize.

**Test**
- The wiring lives in SceneRenderer, which no gtest can drive. (a) can only be checked by engine runs.

## 4. Option (b): central registry (recommended)

### 4.1 New class `rg::RgStateRegistry`

New files `runtime/engine/renderer/render_graph/render_graph_states.h/.cpp`. The directory is already compiled (`build.zig:307`).

```cpp
class RgStateRegistry {
public:
    explicit RgStateRegistry(IRHIDevice* device);
    std::optional<RHIResourceState> Find(RHITextureHandle) const;   // tracked entry only
    RHIResourceState Get(RHITextureHandle) const;                   // entry, else CreationState(GetTextureDesc(h))
    RHIResourceState Get(RHIBufferHandle) const;                    // entry set since last submit, else Common
    void Set(RHITextureHandle, RHIResourceState);                   // bookkeeping after a raw barrier
    void Set(RHIBufferHandle, RHIResourceState);
    void Transition(IRHICommandList*, RHITextureHandle, RHIResourceState after); // before = Get(); no-op if equal
    void Transition(IRHICommandList*, RHIBufferHandle,  RHIResourceState after);
    void UavBarrier(IRHICommandList*, RHIBufferHandle / RHITextureHandle);       // explicit {UAV,UAV}
    void OnQueueSubmit();                                           // decay: every buffer entry -> Common
    void Forget(RHITextureHandle);                                  // optional, memory only
    static RHIResourceState CreationState(const RHITextureDesc&);   // DepthStencil ? DSW : Common
};
```

- Have `dx12_texture_creation.cpp:166-167` call `CreationState` (or a shared RHI helper) so the two cannot drift.
- No age-based pruning: a stale entry is unreachable anyway.
  - Evicting a live entry would silently revert it to the creation state, for example the SkyCubemap between captures.
  - `GetTextureDesc` is not a reliable liveness test: `Free` does not bump the generation (`rhi_resource_pool.h:90-93`), so a stale handle still resolves until its slot is reused.

### 4.2 Changes to `RgBuilder` (`render_graph_builder.h` / `.cpp`)

- **Constructor:** `RgBuilder(IRHIDevice*, RgResourcePool*, RgStateRegistry* states = nullptr)` (h:65, cpp:69-74). The triangle demo (`triangle_demo.cpp:471`) and the existing tests stay source-compatible.
- **Tracked imports:** new `ImportTrackedTexture(name, handle)` and `ImportTrackedBuffer(name, handle)`, which assert that a registry is present.
  - Add a `stateFromRegistry` flag to `RgResourceEntry` (`render_graph_types.h:105-124`).
  - The existing three-argument imports stay as explicit claims; only the backbuffer should keep using them.
- **Dedup:** a map from (index, generation) to `RgResourceId` in both imports (cpp:144-186). A second import of the same handle returns the first id.
  - This cannot create cycles: edges always run from an earlier-declared pass to a later one (cpp:254-264).
- **Compile** (cpp:213-224), before `ScheduleBarriers`:
  - tracked entries get `initialState = m_states->Get(handle)`;
  - for explicit entries whose claim differs from `Get(handle)`, record `{name, claimed, tracked}` (exposed through `GetImportClaimMismatches()`) and log it once (observe mode).
- **Execute:** after the pass loop (before cpp:508), commit `m_states->Set(handle, res.currentState)` for every imported entry. Imported entries are never culled (cpp:383-385, 394-396) and culled passes never change `currentState` (cpp:420).
  - Debug guard: while Execute runs, the registry asserts if `Transition`/`Set` is called on a handle that this graph imported. This catches a callback silently re-transitioning a declared resource.

### 4.3 SceneRenderer wiring

- Add `std::unique_ptr<rg::RgStateRegistry> m_rgStates` next to `m_rgPool` (`scene_renderer_impl.h:41`). Create it at `scene_renderer_init.cpp:165` and reset it at `:1696`.
- Pass it to `rg::RgBuilder` at `scene_render_frame.cpp:113`.
- Call `OnQueueSubmit()` after each submit of the frame list:
  - `scene_render_frame.cpp:372`;
  - `frame_setup.cpp:95`;
  - `atmosphere_renderer.cpp:778` (needs a new `states` field in `AtmosphereFrameContext`, `atmosphere_renderer.h:41`).
- Delete:
  - the frame-0 warmup (`scene_render_frame.cpp:80-100`);
  - the end-state export (`:244-248`);
  - the four members (`scene_renderer.h:913-919`);
  - the fallbacks (`frame_setup.cpp:410-415`).

### 4.4 Coverage

- **Graph imports:** every import is exact automatically, on the first frame, in steady state, and after Resize or any other recreation. This includes `ShadowAtlas::Resize` (`frame_constants.cpp:1314-1316`), which runs before the import at `frame_setup.cpp:425`.
- **Buffers:** decay is modelled centrally.
- **Code outside the graph:** exact as soon as it uses the `Transition` API.
- **Vulkan is not regressed:** it takes `oldLayout` from its own `tex->currentLayout` and uses `stateBefore` only for stage/access masks (`vulkan_command_list.cpp:837-845`), which become more accurate.

### 4.5 Risks and mitigations

1. **An undeclared bindless read can promote a COMMON texture before its first declared access in the same list.** The graph's explicit COMMON→X barrier is then wrong. Known cases:
   - velocity on frame 0 and after Resize: `m_prevViewProjValid` is false, so the velocity pass is skipped, and GTAO/TAA read it undeclared before `VFX_ParticleRender` declares RT (`particle_render_pass.cpp:153-156`);
   - HDR after Resize, read by AutoExposure (`frame_constants.cpp:608-610`).

   Mitigation: declare those reads, and clear `m_hdrHasContent` on Resize (stage 1).
2. **A callback that changes a declared resource without restoring it** makes the registry commit a false state, exactly as `GetFinalState` does today. The one case is `SSR_CopyPrevColor` on the non-TAA path (`frame_postprocess.cpp:487-489`); it is fixed in stage 1.
3. **Raw barriers on tracked textures outside the graph must migrate to the registry.** These are the IBL bake, `ProjectCubemapToSH`, the pixel-debug read and the inspector (see 7.2).
4. **Mid-frame submits must call `OnQueueSubmit()`.** If one is missed, buffer entries will not decay; this does not affect texture entries.
5. **Ownership:** one registry per SceneRenderer is enough today. It could move into the RHI later, as the field Vulkan already has, once every manual site uses it. Doing that first would silently replace wrong `stateBefore` values and hide them from the debug layer.

### 4.6 Test

Mostly with a MockDevice gtest (see 8.1), which no other option allows.

## 5. Option (c): end-of-graph transitions back to the import state

**Code changes**
- In `ScheduleBarriers`, for each imported resource flagged "restore" whose `currentState` differs from `initialState`, add an exit barrier.
- Execute emits the exit barriers after the last pass (before cpp:508). `GetFinalState` then equals the import state.
- Every import site gets a constant "resting" state.
- Because textures are born COMMON, every create and recreate site needs a pending creation-transition queue. That queue replaces the warmup and must also cover Resize (`scene_renderer_init.cpp:1167-1221`), VolumetricFogPass, the PT accum, sky, AP volume and LUT.

**Coverage**
- Steady-state consistency for graph imports.
- Nothing for manual resources.
- Post-graph code sees the resting states, which changes current assumptions:
  - the pixel-debug compute assumes the GBuffer and HDR are in SRV (`scene_render_frame.cpp:311-314`);
  - AutoExposure samples HDR before the graph (`frame_constants.cpp:608-610`).

  So HDR and the GBuffer colours must rest in SRV, which costs extra barriers in some modes. Light_* resting in SRV costs two barriers per lit frame, against zero in (b).

**Risks**
- The same "forgot a recreate site" failure mode as (a).
- Resting in COMMON, which would avoid the creation queue, may force render-target or depth decompression on some GPUs. This is external hardware guidance, not checked in the repo.

**Test**
- The exit-barrier logic can be tested with a gtest. The creation queue and the choice of resting states can only be checked by engine runs.

## 6. Comparison

| | (a) members | (b) registry | (c) restore at end |
|---|---|---|---|
| Graph imports across frames | per member | all, automatic | all (steady state) |
| First frame | per-member fallback plus warmup | creation state from the descriptor | needs a creation queue |
| Resize / recreate | manual reset per site (forgotten today) | automatic (generational key) | queue entry per site |
| Buffers (decay) | hard-coded Common | automatic | resting Common (redundant exit barrier) |
| Manual barriers outside the graph | no | yes, through `Transition` | no |
| Extra barriers | none | none | mode-dependent |
| Size (rough estimate) | ~17 members, ~50 edits in 8 files | ~200 new lines; ~25 one-line import edits; deletions | ~60 lines plus resting states plus creation queue |
| MockDevice-testable | no | yes | partially |

## 7. Exact per-resource changes (option b)

### Rollout

- **Stage 0 (no behaviour change).** Add the registry, the commit and observe mode. Route the warmup through `m_rgStates->Transition`. The observe log is then a measured inventory of stale imports.
- **Stage 1.** Switch the imports in 7.1, make the fixes in 7.2, and delete the old code.
- **Stage 2.** Declare the missing reads.
- **Stage 3.** Migrate the manual-only resources (7.3).

### 7.1 Graph imports: switch to `ImportTracked*`

**HDR_SceneColor** (`frame_setup.cpp:418`)
- Required with it: in `frame_postprocess.cpp:487-489`, set `copySourceState = RHIResourceState::ShaderResource`.
  - The pass already declares `Read(hdr, SRV)` at `:494`.
  - The TAA output is already SRV after `:365-373`.
- Required in `SceneRenderer::Resize`: after the HDR recreation (`scene_renderer_init.cpp:1192-1203`), set `m_hdrHasContent = false`.
- Optional: `m_rgStates->Transition(ctx.cmdList, m_hdrSceneColor, ShaderResource)` before `frame_constants.cpp:609`.
- Result: Common→RT on frame 0 and after Resize; SRV→RT in steady state; the non-TAA SSR path becomes SRV→CopySrc→SRV.

**GBuf_Albedo, GBuf_Normal** (`frame_setup.cpp:419-420`)
- Behaviour is unchanged, except that the frame after Resize now gets Common→RT.

**GBuf_Depth** (`frame_setup.cpp:421`)
- Creation state is DEPTH_WRITE (`gbuffer.cpp:108-109`, `139-147`), so the stale SRV→DSW after Resize disappears.
- Stage 2: add `pb.Read(ctx.depthRes, ShaderResource)` in the upscaler pass (`frame_postprocess.cpp:271`); TAA samples depth at `:324`.

**GBuf_Position, GBuf_Emissive, GBuf_Velocity** (`frame_setup.cpp:422-424`)
- Each gets its own tracked state instead of albedo's.
- Stage 1, required for velocity (see 4.5 risk 1): add `pb.Read(ctx.velocityRes, ShaderResource)` in
  - GTAO (`frame_ao_passes.cpp:68-69`);
  - the upscaler (`frame_postprocess.cpp:271`).

**ShadowAtlas** (`frame_setup.cpp:425`)
- First frame: DEPTH_WRITE, so no barrier before Shadow.
- Forward, PT and GBuffer-only modes no longer get a stale SRV→DSW.
- A recreated atlas starts in DEPTH_WRITE.
- Stage 2: give the path tracer an `RgResourceId shadowAtlas` in `PathTracerPassDesc` (`path_tracer_pass.h:36-42`), declare its read in `path_tracer_pass.cpp:23-30`, and set it in `frame_lighting.cpp:116-139`.

**LocalShadowAtlas** (`frame_shadows.cpp:339-340`)
- Store the id as `localShadowAtlasRes` in FrameContext (next to `scene_renderer_frame.h:128`).
- Stage 2:
  - add `localShadowAtlasResource` to `LightingPassDesc` (`lighting_pass.h:77`);
  - declare `Read(SRV)` next to `lighting_pass.cpp:51-53`;
  - set it at `frame_lighting_pass.cpp:221`.
- Result: no barrier on frame 0; correct DSW→SRV before lighting and SRV→DSW before the next `LocalShadowDepth`.

**StaticShadowAtlas** (`frame_shadows.cpp:113-114`, `299-300`)
- Dedup merges the two imports into one id.
- The path is unreachable today: `ctx.shadowCacheEnabled` is only ever assigned false (`frame_constants.cpp:1063`) and read at `frame_shadows.cpp:57`.

**Light_Direct / Indirect / Shadow** (`frame_setup.cpp:431-433`)
- Common→RT on frame 0 and after Resize (today these textures are never transitioned); SRV→RT after debug views.

**DBuffer_A / B / C** (`frame_setup.cpp:438-440`)
- Common→SRV on the first frame with decals, then nothing; Common again after Resize.
- Separate content bug: nothing writes the DBuffer, yet DecalApply blends it (`frame_lighting.cpp:150` → `frame_ao_passes.cpp:34-56`).

**PT accum** (`frame_lighting.cpp:107-114`)
- Import both curr and prev as tracked and delete `currImportState`.
- Add `accumPrev` to `PathTracerPassDesc` and declare `pb.Read(desc.accumPrev, ShaderResource)` at `path_tracer_pass.cpp:24`.
- Fix the comment at `frame_lighting.cpp:104-106`.
- Result: SRV→RT plus RT→SRV in steady state; Common→RT and Common→SRV on the first frame.

**APVolume** (`frame_lighting.cpp:196-197`, `atmosphere_renderer.cpp:680-681`)
- Both import as tracked; dedup gives them one id.
- HDRI, no-sky and `r.ShadowDebug` frames stop getting a stale UAV→SRV.

**TransmittanceLUT** (`atmosphere_renderer.cpp:97-101`)
- Drop the `filled` ternary.
- Result: Common→UAV on the first fill, UAV→SRV, then no further barriers.

**SkyCubemap** (`atmosphere_renderer.cpp:245-246`, `287-288`)
1. After `:284` and `:308`, add an empty pass with `pb.Read(cubeRes, ShaderResource)`. This is the Present-pass idiom (`frame_overlay.cpp:573-577`); read-only passes are never culled (`render_graph_builder.cpp:372-373`).
2. In `PostFrameAtmosphereBake`, before `End` at `:777`, call `states->Transition(cmdList, ctx.skyCubemap, ShaderResource)`.
3. Delete the non-bindless UAV→SRV barriers in `ibl_baker.cpp:504-511` and `:729-736`.
4. Fix the comments in `sh_projection.cpp:561-569`; its SRV→CopySrc→SRV at `:570-587` becomes correct.

**ScatteringLUT3D_CubeCapture** (`atmosphere_renderer.cpp:279-281`)
- Import as tracked. Unreachable today: its producer is under `#if 0` (`:381-668`).

**FroxelFog_Inscatter / Integrated** (`volumetric_fog_pass.cpp:136-139`)
- No new members needed.
- Result: Common→UAV once, then SRV→UAV and UAV→SRV with true before-states.

**VFX mesh buffers** (`frame_particles.cpp:224-229`, `250-251`)
- Import as tracked buffers and delete the `firstCycle` ternaries (`216-222`, `247-249`).
- Result: Common→UAV, then UAV→SRV or UAV→IndirectArgument.

**Backbuffer** (`frame_setup.cpp:417`)
- Keep the explicit Present import.

### 7.2 Code outside the graph that touches tracked textures

- **Pixel debug.** Before the dispatch (`scene_render_frame.cpp:315-335`), transition albedo, normal, depth, emissive and HDR to SRV through the registry. This makes the assumption at `:311-314` true.
- **Inspector.** `PassInspector` gets a `const RgStateRegistry*`. Dump and validate use `Find(tex).value_or(target.lastState)` as both the before and the restore state (`inspector_pass_dump.cpp:80-91`, `inspector_pass_validate.cpp:186-200`). Every graph-imported target becomes exact; the hard-coded states at `frame_lighting.cpp:256-293` and `361-425` remain only for self-tracked targets.
- **Shadow-atlas readback** (`scene_debug.cpp:84-97`): has no callers. Migrate it to the registry if it is ever revived.

### 7.3 Manual-only resources: replace the hard-coded before-state with `m_rgStates->Transition`

**TAA history**
- `frame_postprocess.cpp:307-320`: `Transition(hist[w], UAV)` and `Transition(hist[r], SRV)`.
- `:365-373`: `Transition(hist[w], SRV)`.
- Also SSR_Composite at `:429-436` and `:464-471`.
- Separate content fix: treat a history that was never written as `firstFrame`.

**TAA clamp box:** `:289-305`.

**SSR resources** (`frame_gi_passes.cpp`)
- SSR history: `:714-723`, `:767-773`.
- Trace output / hit distance: `:558-567`, `:622-632`.
- Resolved output: `:646-652`, `:681-687`, `:796-802`, `:834-840`.
- Ray-counter buffer: `:470-476`, `:536-542`.

**Other textures**
- SSR prev scene colour: `frame_postprocess.cpp:502-504`, `:513-515`.
- Translucent scene colour: `frame_translucent.cpp:111-113`, `:122-124`.
- HiZ mips: `hiz_pyramid_pass.cpp:93-107`.
- Bloom mips: `bloom_pass.cpp:421-426`, `:464-470`.
- EnvMIS history: `env_mis_pass.cpp:130-135`.
- GTAO textures: `gtao_pass.cpp:785-831`, `:910-917`, `:1045-1074`.

**Buffers** (these become Common at first use by construction)
- LightCull cluster list: `frame_lighting_pass.cpp:112-116`, `:123-128`.
- AutoExposure: `auto_exposure.cpp:184-189`, `:409-414`.
- Pixel debug: `pixel_debug.cpp:117-130`, `:142-155`.
- DDGI ray buffers: `ddgi_system.cpp:360-368`, `:390-398`.
- Radiance cache: `radiance_cache.cpp:340`, `:375-381`, `:439-441`, `:725-751`.
- GPU-cull args: `frame_geometry.cpp:80-85`.

**Fallback textures** (`frame_setup.cpp:88-90`)
- After each upload, `Set(tex, CopyDst)`, then `Transition(tex, SRV)` before `:94`.
- Use their own one-shot flag.

**Owner classes** (GTAOPass, BloomPass, EnvMISPass, DDGISystem, RadianceCache, AutoExposure, PixelDebugSystem) receive the registry pointer at Init. The per-object trackers listed above can then be deleted.

### 7.4 Keep outside the registry (use the audit's per-site fixes)

- **Streamed textures finalized after a copy-queue upload:** the before-state at `scene_texture_assets.cpp:330` becomes Common, because work on the copy queue decays.
- **IBL temporaries** in `ibl_baker.cpp`: add an explicit Common→UAV before each dispatch.
- **VFX attribute, free-list and indirect-arg buffers** in `frame_vfx_inline.cpp` (`:91-103`, `:566-587`, `:591-597`): these are also used on a separate init list, so the registry would need an extra submit hook there.
- **SdfAtlas and DFS:** dead paths.

## 8. Tests

### 8.1 MockDevice gtest

New file `runtime/tests/engine/renderer/test_render_graph_states.cpp`, picked up automatically (`build.zig:1244`). It follows `test_render_graph.cpp` (mocks in an anonymous namespace, fixture as at `:175-191`).

**Mocks**
- `GenerationalMockDevice`: backed by `uz::ResourcePool<Slot>` (`rhi_resource_pool.h:27-103`), so destroy-then-create reuses the slot with a bumped generation. Its `GetTextureDesc` returns the stored descriptor (the existing mock returns `{}`, `:68`).
- `D3D12TruthCommandList`: the `MockCommandList` stubs (`:101-142`) plus a per-handle "actual" state.
  - The state is seeded with the creation state.
  - Every barrier checks `stateBefore` against it and records any mismatch.
  - `Execute()` resets buffers to Common.

**Cases**
1. The second frame's import starts from the first frame's end state.
2. Fresh textures start in their creation state: RT/SRV textures get Common→RT; DepthStencil textures get no barrier for a DSW write and DSW→SRV for a read.
3. A recreated handle (same slot, new generation) starts in its creation state.
4. The same handle imported twice yields one id and one barrier chain.
5. Buffers import as Common every frame.
6. A registry transition recorded after Import and before Compile is honoured.
7. A frame that does not import a resource keeps its state.
8. A culled pass does not change the committed state.
9. `Transition` with an equal state is a no-op; `UavBarrier` emits {UAV,UAV}.
10. An explicit claim that disagrees with the registry is reported.
11. An 8-frame "mode matrix": GBuffer → lighting; debug view on frames 3-4; a PT-like frame 5; Resize at frame 6; a froxel-like UAV/SRV volume; mesh-like UAV→IndirectArgument. Expect 0 mismatches.
12. Negative control: the same frames with today's claims (UAV every frame, Light_* in RT, velocity using albedo's state, no reset at resize) must report at least one mismatch per pattern.

**Run**

```
runtime/build/bin/test_runtime.exe --gtest_filter=RenderGraphStates*:RenderGraphTest.* --gtest_brief=1
```

**Build:** run `cd runtime && zig build > build.log 2>&1; echo $?`, then check the timestamps of both executables.

### 8.2 Engine runs with `--gpu-validation`

**Command.** Run from the repository root, as `scripts/test_gi_energy_gpu.ps1:17-26` does:

```
runtime/build/bin/uzengine.exe --project <p> --frames 240 --verbose --gpu-validation [--cvar ...] [--view-mode ...] > gv_<case>.log 2>&1
```

- `--verbose` sets the Info log level (`uzengine.cpp:1345-1346`); the debug-layer status and summary lines are Info.
- Use a bash redirect. PowerShell `*>` writes UTF-16.

**Precondition: a run counts as evidence only if the debug layer actually came up.**
- Require `"DX12 debug layer enabled"` (`dx12_device.cpp:503`) and `"D3D12 info queue configured"` (`:315`).
- Reject the run if `"D3D12 debug interface unavailable"` appears (`:495-499`). `m_validationEnabled` is set before `EnableDebugLayer` can fail (`:190`, `:198-201`), so the shutdown summary still prints "0 message(s)" (`:166`) with no layer present.
- Project memory (not a repo file) records that on this workstation the Agility device factory returns `0x887A002D` for the debug interface. Run the matrix on a machine where the precondition holds.
- Project memory also notes that the non-bindless (SM5) lighting path does not compile with shadows. Check the deleted SM5 IBL barriers by code review and the gtest rather than an SM5 run.

**Matrix**
1. `sample/projects/sponza.uzproject` with defaults
2. `--cvar r.SSR=1 --cvar r.TAA=0`
3. `--cvar r.TAA=1 --cvar r.GTAO=1`
4. `--cvar r.PathTracer.Enable=1`
5. `--view-mode albedo` (GBuffer-only), and separately `--view-mode lighting` (post-lighting)
6. `sample/projects/local_shadows_test.uzproject`
7. `scripts/fog-shaft-reference.uzproject`
8. `sample/projects/decal_test.uzproject`
9. `scripts/sky-rebake-reference.uzproject`
10. `sample/projects/hdri_test.uzproject`
11. Resize: `--cvar r.DynamicResolution.Enabled=1`, or a Lua script calling `uz.cvar.set("r.ScreenPercentage", 75)` and back (`lua_cvar_api.cpp:39`)
12. A scene using a `mesh_render` emitter (`sample/content/vfx/tests/mesh_sort_shards.uzfx`)
13. `--pass-dump`

**Pass criterion**
- No `"D3D12 debug ERROR (id 527|538|607|942)"` lines. The line format is at `dx12_device.cpp:477`; the IDs are BEFORE_AFTER_MISMATCH, INVALID_SUBRESOURCE_STATE, POSSIBLY_INVALID_SUBRESOURCE_STATE and GPU_BASED_VALIDATION_INCOMPATIBLE_RESOURCE_STATE (`runtime/thirdparty/directx_headers/include/directx/d3d12sdklayers.h:2572`, `:2583`, `:2636`, `:2885`).
- Compare each case with a baseline run of HEAD, using the per-message summary (`dx12_device.cpp:166-168`).
- Expect GBV (ID 942) to clear only after stage 2, since it also reports the undeclared reads.

**Check that works without the debug layer**
- In stage 0, the observe-mode log must list the audit's graph-visible items:
  - froxel ×2, LocalShadowAtlas;
  - ShadowAtlas in Forward, PT and GBuffer-only modes;
  - DBuffer, Light_*;
  - emissive and velocity (borrowing albedo's state);
  - APVolume with HDRI or no sky;
  - PT_AccumCurr, the TransmittanceLUT first fill, SkyCubemap re-captures, the VFX mesh buffers;
  - HDR and the GBuffer after Resize.
- After stage 1, only the backbuffer still uses an explicit claim.

## 9. Corrections to the brief

- **StaticShadowAtlas cannot be imported twice in practice:** the whole shadow-cache path is unreachable (`frame_constants.cpp:1063`, `frame_shadows.cpp:57`).
- **ScatteringLUT3D_CubeCapture is never imported:** its producer is compiled out (`atmosphere_renderer.cpp:381-668`).
- **TransmittanceLUT is imported once per frame:** the sky loop stops after the first enabled component (`atmosphere_renderer.cpp:760`).
- **A false premise in a code comment should be removed:** `ibl_baker.cpp:499-503` says bindless access "doesn't require explicit state tracking".
