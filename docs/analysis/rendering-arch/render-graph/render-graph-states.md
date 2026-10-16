# Render-graph resource states across frames

Task: `.rulebook/tasks/phase0_render-graph-state-registry` (stage 0 done 2026-10-09; stages 1-3 open).

## The problem

A D3D12 transition names the state the resource is in before it. The DX12 RHI copies every `stateBefore`
into the barrier verbatim (`dx12_command_list.cpp`) and keeps no state per resource, and until this task the
render graph took an imported resource's state on the caller's word, never restoring it at the end of the
frame. Only HDR, depth, albedo and normal carried their end state into the next frame (`GetFinalState`); every
other persistent resource used a hard-coded or predicted state. The audit of 2026-10-08 (every renderer
subsystem, each claim checked by an adversarial verifier) confirmed 80 cross-frame mismatches out of 89
claims. NVIDIA drivers tolerate most of them; they are undefined behaviour that the D3D12 debug layer would
report, and that layer cannot be enabled on this workstation (the Agility factory returns 0x887A002D).

## The design (option b of the audit)

`rg::RgStateRegistry` (`runtime/engine/renderer/render_graph/render_graph_states.{h,cpp}`), one per
SceneRenderer, is the record of the state every persistent resource is in between graphs:

- **Keyed by the generational handle.** A destroyed and recreated texture is a new key and starts in its
  creation state, so Resize and every other recreation site need no reset hook.
- **No record means the creation state:** DepthStencilWrite for a depth-stencil texture, Common otherwise,
  as `dx12_texture_creation.cpp` creates them; Common for a buffer.
- **Buffers decay to Common when their command list is submitted.** The registry is bound to the frame's
  command list (`BindCommandList`) and drops its buffer records when that list's submit count moves
  (`IRHICommandList::GetSubmitCount`, counted by `DX12CommandList::Execute`). A per-list count rather than
  three `OnQueueSubmit()` hooks: the frame list is submitted at nine places, and an auxiliary list submitted
  while the frame list is still recording executes first and does not reset the frame list's own states.
  `OnQueueSubmit()` remains for lists or backends that do not count.
- **Present is Common** (D3D12_RESOURCE_STATE_PRESENT and COMMON are both 0): comparisons use
  `RgStateRegistry::SameState`.
- **`RgBuilder` uses it in three places:** tracked imports (`ImportTrackedTexture/Buffer`) take their initial
  state from it at Compile, so a registry transition recorded between the import and Compile is honoured, and
  a handle imported twice returns the first id (one barrier chain); explicit imports keep their claim, and the
  observe mode reports a claim that disagrees with the registry; after Execute, the end state of every
  imported resource the graph transitioned is committed (after a barrier the resource is in its stateAfter
  whatever stateBefore claimed; an explicit import the graph never transitioned is in whatever state it was
  in, which its claim may misstate).
- **Code outside the graph** records its raw barriers with `Set` or emits them through `Transition` /
  `UavBarrier`. Calling them on a handle the executing graph imported is a fatal error: the graph would then
  commit a state the callback silently changed.

## Stage 0 (2026-10-09): the registry observes, nothing changes

The engine creates the registry, binds the frame list, records the frame-0 warmup's raw barriers with `Set`
(the same single batched barrier call as before) and passes the registry to the graph. No import is tracked
yet, so every barrier is the one it was.

Gate, registered before the first build (task notes.md), on the Debug build:

| check | result |
|---|---|
| `test_runtime --gtest_filter=RenderGraphStates*` | 13/13: the creation state, the second frame, a recreated handle, double imports, buffer decay with and without a counting list, a transition between import and Compile, a frame without the import, a culled pass, Transition / UavBarrier, the observe report, Present = Common, the builder without a registry, an 8-frame mode matrix with no error, and the negative control |
| `RenderGraphTest*` | 29/29, unchanged |
| identity, `scripts/rendering/test_rg_states_identity.py --compare` against the build before (2cc2e85c) | 7 of 7 cases unchanged: the reflection, fog-shaft, local-shadow, decal, HDRI and path-tracer references bit-identical, Sponza within its own spread at equal pre-load length |
| observe inventory | the audit's graph-visible items, table below |

The gtests use two mocks: `GenerationalMockDevice` (destroy and create reuse a slot with a new generation)
and `D3D12TruthCommandList`, which keeps the state every watched resource is really in and records the
errors the debug layer would report: a barrier whose stateBefore is not the actual state, and a pass that
accesses a resource in a state it is not in and cannot be implicitly promoted to (texture promotion from
Common to read-only and copy states, buffer promotion to any state, and their decay at submit). The negative
control replays the audit's patterns with today's explicit claims and fails on every one of them: the froxel
volume claimed UnorderedAccess, Light_Direct claimed RenderTarget, velocity with albedo's end state, HDR with
its old end state after a Resize, and the path tracer's accumulation written while still in Common.

### What the observe mode found (counts per run, final binary)

| import | claimed | tracked (origin) | reflection | fog_shaft | local_shadow | decal | hdri | path_tracer | sponza |
|---|---|---|---|---|---|---|---|---|---|
| APVolume | UnorderedAccess | Common (creation state) | 1 | 1 | 1 | 1 | 1 | 126 | 1 |
| APVolume | UnorderedAccess | ShaderResource (committed by a graph) |  |  |  |  | 125 |  |  |
| FroxelFog_Inscatter | UnorderedAccess | ShaderResource (committed by a graph) |  | 124 |  |  |  |  |  |
| FroxelFog_Integrated | UnorderedAccess | ShaderResource (committed by a graph) |  | 124 |  |  |  |  |  |
| GBuf_Albedo / Normal / Position / Emissive, HDR_SceneColor | ShaderResource | Common (creation state) | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| GBuf_Velocity | ShaderResource | RenderTarget (set outside the graph) | 123 | 122 | 124 | 123 | 123 | 123 | 1 |
| Light_Direct / Indirect / Shadow | RenderTarget | Common (creation state) | 126 | 125 | 127 | 126 | 126 |  | 283 |
| LocalShadowAtlas | ShaderResource | DepthStencilWrite (creation state) |  |  | 127 |  |  |  |  |
| PT_AccumCurr | Common | RenderTarget (committed by a graph) |  |  |  |  |  | 124 |  |
| ShadowAtlas | ShaderResource | DepthStencilWrite (creation state) | 1 | 1 | 1 | 1 | 1 | 126 | 1 |
| SkyCubemap, TransmittanceLUT | UnorderedAccess | Common (creation state) | 1 | 1 | 1 | 1 |  | 1 | 1 |

Plus, from `sample/projects/decal_test.uzproject` (the decal reference uses mesh decals and never imports
the DBuffer): DBuffer_A/B/C claimed RenderTarget, tracked Common twice and ShaderResource 132 times.

Reading it against the audit:

- **Every frame:** the froxel volumes (block B.4b's own code: imported as UnorderedAccess, left in
  ShaderResource), the Light_* targets (claimed RenderTarget, never transitioned), velocity (imported with
  albedo's end state; its real state is the RenderTarget the warmup left), the local shadow atlas, the AP
  volume on the HDRI path and in path-traced frames, the shadow atlas in path-traced frames, the path tracer's
  current accumulation target, and the DBuffer.
- **Once per run:** the first fills of the transmittance LUT and the sky cubemap, and HDR plus the G-buffer
  after the resize at the start of the render loop (the four end-state members are not reset by Resize).
- The 1-3 mismatches per case that read "creation state" on the AP volume, shadow atlas and the G-buffer are
  that same render-loop resize and first frame.

Not exercised by stage 0: SkyCubemap re-captures, the VFX mesh buffers (no mesh emitter in these scenes) and
the manual barriers outside the graph (TAA, SSR, Hi-Z, bloom, GTAO, the env-MIS history, the radiance cache,
DDGI, auto-exposure, the light-cull lists), which stage 3 moves to `Transition`.

### Found on the way: the frames depend on the allocation layout

The first identity run failed: the reflection, fog-shaft and path-tracer references moved by one code value
at 50, 997 and 1 pixels, while the local-shadow, decal and HDRI references were bit-identical. The registry
was not the cause: four of the five shaders the build recompiled were byte-identical to their cached
compilations (the fifth had no cached copy of its size, consistent with the debug-view shader, whose case 18
did change and which these captures do not run), and the build also carried a wrong payload qualifier that
made the RT UV test pass fail to compile and skip its 1920x1080 UAV and two descriptors. With the qualifier fixed, every case was
bit-identical again. So some pass's output depends on where later resources land: an uninitialised read or
an index-dependent path. Recorded in known-issues ("Capture reproducibility"), owner
`phase0_capture-preload-determinism`.

## Next stages

1. Switch the graph imports to tracked imports (the audit's 7.1), with its required fixes: SSR_CopyPrevColor
   on the non-TAA path, `m_hdrHasContent` cleared at Resize, the velocity reads declared; delete the frame-0
   warmup and the four end-state members. Gate: the observe log empty except the backbuffer, identity within
   the same rule.
2. Declare the missing reads (path tracer atlas, local shadow atlas, depth in the upscaler).
3. Move the manual barriers to `Transition` (the audit's 7.3).

The clouds' move into the graph (0c.1) and the path tracer's Phase A build on stage 1.

## Stages 1-3 (2026-10-13): shipped

All three stages are implemented and accepted on this workstation; the `--gpu-validation` GBV
matrix (design §8.2) is deferred to a machine with a working DX12 debug layer (the Agility device
factory returns `0x887A002D` here, so the layer never comes up).

- **Stage 1 (item 2.1).** 33 graph imports switched to `ImportTracked*`; the option-a machinery
  removed (`m_{hdr,depth,albedo,normal}EndState`, the frame-0 warmup barriers, the `GetFinalState`
  exports). §7.2 wiring landed: `PassInspector` and the owner classes take the registry; the
  pixel-debug inputs transition through it; `AtmosphereFrameContext` carries `states`, a read-only
  `SkyCubemap_Readable` pass keeps the cubemap in SRV, `PostFrameAtmosphereBake` transitions it, and
  the SM5 IBL UAV→SRV barriers are deleted. Required companions: SSR `copySource` = ShaderResource,
  `m_hdrHasContent` cleared at Resize, GTAO+upscaler velocity reads, PT accum-prev tracked.
- **Stage 2 (item 3.1).** The three missing reads declared: GBuf_Depth in the upscaler, ShadowAtlas
  in the path tracer (`PathTracerPassDesc::shadowAtlas`), LocalShadowAtlas in the deferred lighting
  pass (`LightingPassDesc::localShadowAtlasResource`, guarded on import validity).
- **Stage 3 (item 3.2).** Manual barriers migrated to `RgStateRegistry` in the seven owner classes
  (GTAO, Bloom, EnvMIS, AutoExposure, PixelDebug, DDGI, RadianceCache — wired via `SetStateRegistry`
  from `SceneRenderer::Init`) and the frame-local sites (TAA history/clamp, SSR history/trace/resolved/
  counter, SSR prev + translucent scene colour, HiZ/Bloom mips, cluster list, cull args, fallback
  textures). Per-object state trackers deleted, including DDGI's `m_firstDispatch` and RadianceCache's
  six trackers. §7.4 keep-list untouched. Raw barriers remain only where state-neutral, round-trip, or
  paired with `Set` (gpu-cull indirect args, SSR/translucent copy sources, debug region dump).

**Acceptance (option A, this workstation).** `zig build` exit 0; MockDevice gtest
`RenderGraphStates*`+`RenderGraphTest.*` 42/42 (truth-list + negative control); the identity harness
bit-identical on the pre-load-matched cases (reflection/hdri/path_tracer) with the observe mode
reporting 0 claim-disagreements across 126-261 graphs per case — the stage-0 stale inventory is now
empty. The remaining identity cases are UNMATCHED by pre-load drift (phase0_capture-preload-determinism),
not a stage change. GBV id 942 clearance awaits the `--gpu-validation` matrix on a DX12-debug-layer-capable
machine.
