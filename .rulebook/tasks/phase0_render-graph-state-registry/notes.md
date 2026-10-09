# Notes — render-graph state registry

## Stage 0 gate, registered 2026-10-09 before the first build of the registry

Build before: Debug of 2cc2e85c (uzengine.exe fnv1a64:c4cf401970c78226), captured with
`python scripts/test_rg_states_identity.py --capture .screenshots/rg-states/before-2cc2e85c`
(7 cases x 2 processes; the five small scenes bit-identical between their processes, pre-load 3-5 frames;
Sponza pose W at frame 237 with both processes at a 44-frame pre-load, max 1 code value apart).

S0 passes when all of these hold on the Debug build with the registry:
1. `test_runtime --gtest_filter=RenderGraphStates*` all pass, including the negative control (today's
   claims fail on every audited pattern and the observe mode names them);
2. `test_runtime --gtest_filter=RenderGraphTest*` unchanged (all pass);
3. `python scripts/test_rg_states_identity.py --compare .screenshots/rg-states/before-2cc2e85c <after>`:
   7 of 7 cases unchanged by the harness rule (a process paired with a before process of the same
   pre-load length; bit-identical where the before processes are, else within their spread);
4. `--observe <after>` lists the audit's graph-visible items in the cases built for them: the froxel
   volumes (fog_shaft), the local shadow atlas (local_shadow), the DBuffer (decal), the path tracer's
   accumulation (path_tracer), the GBuffer emissive / velocity borrowing the albedo state (every case).
   Items the stage cannot show (HDR after Resize, SkyCubemap re-captures, the VFX mesh buffers) are
   reported as not exercised, not as passed.

## Decisions taken while writing stage 0 (2026-10-09)

- **Buffer decay is counted per command list, not per device or per queue.** The design asked for an
  `OnQueueSubmit()` call at three submit sites; the frame list is submitted in at least six more
  (frame_setup.cpp x4, frame_vfx_inline.cpp x2, scene_texture_assets.cpp, the inspector), and a
  missed call leaves stale buffer states. A device-wide counter would be wrong the other way: an
  auxiliary graphics list submitted while the frame list is still recording executes first and does not
  reset the states the frame list's own earlier barriers set. So `IRHICommandList::GetSubmitCount()`
  (DX12CommandList counts its `Execute` calls) and `RgStateRegistry::BindCommandList(frameList)`; the
  registry drops its buffer records when that count moves. `OnQueueSubmit()` stays for lists or backends
  that do not count (the default returns 0).
- **Dedup applies to tracked imports only in stage 0.** Two explicit imports of one handle (APVolume
  today) keep two barrier chains, the behaviour before, and are reported as a double import; stage 1
  makes them tracked, which merges them.
- **Only imports the graph transitioned are committed.** After a barrier the resource is in its
  stateAfter whatever stateBefore claimed, so the end state is known; an explicit import the graph never
  transitioned is in whatever state it was in, which its claim may misstate. For a handle imported
  twice, the chain whose last barrier executed last wins.
- **The observe mode checks only imports a non-culled pass accesses**, and logs at Info (each distinct
  name / claim / tracked state once, a summary at shutdown). Stage 0 expects a known inventory every run;
  once stage 1 empties it, a new line is a regression and can be raised to Warning.
- **The frame-0 warmup records its raw barriers with `Set`** instead of emitting them through
  `Transition`: the same states, but one batched barrier call as before, so the command stream is
  unchanged.

## Stage 0 results (2026-10-09)

Debug build of the registry commit (uzengine.exe 10:11, the binary of the final runs):

1. `RenderGraphStates*` 13/13 and `RenderGraphTest*` 29/29 (with `AverageRootConstants.*` 3/3: 45/45).
2. Identity against the build before (`.screenshots/rg-states/before-2cc2e85c` vs `after-final`): **7 of 7
   cases unchanged** (five references bit-identical, the path tracer bit-identical, Sponza within its
   spread at equal pre-load length).
3. Observe inventory: every item the gate named, in its case, except the DBuffer, which the decal
   reference never imports (it uses mesh decals); `sample/projects/decal_test.uzproject` lists DBuffer_A/B/C
   (claimed RenderTarget, tracked ShaderResource 132 times). Table in docs/analysis/render-graph-states.md.
   Not exercised: SkyCubemap re-captures, the VFX mesh buffers, the manual barriers (stage 3).

Gate S0 passes. Two runs on the way failed and are recorded, not hidden:

- **The first identity run failed (3 of 7):** the reflection, fog-shaft and path-tracer references moved by
  one code value at 50, 997 and 1 pixels. The cause was a wrong payload qualifier in the same build
  (parity-06 E8: `read(caller, closesthit, miss)` does not compile, so the RT UV test pass skipped its
  1920x1080 UAV and two descriptors), not the registry: four of the five shaders the build recompiled were
  byte-identical to their cached compilations (the fifth had no cached copy of its size, consistent with the
  debug-view shader, whose case 18 did change and which these captures do not run), and with the qualifier
  fixed every case was bit-identical.
  That the frames depend on where later resources land is itself a finding (known-issues, "Capture
  reproducibility", owner phase0_capture-preload-determinism).
- **The first observe inventory reported the backbuffer** (claimed Present, tracked Common): the same D3D12
  state. Comparisons now use `RgStateRegistry::SameState`, with a gtest.

**Overlap with `phase0_fix-dx12-first-frame-resource-state-crash`** (11/29, pending, from the
feat/vfx-application-integration merge): its items 1 and 3 hand-set first-frame import states (HDR, GBuffer,
ShadowAtlas; the atmosphere LUTs), which stage 1 replaces with tracked imports; its items 2 and 4 (the IBL BRDF
LUT barrier, the VFX aliveCount copy) are manual-barrier sites for stage 3. Stage 1 should close or re-scope it.
