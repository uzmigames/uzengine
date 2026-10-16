# Frame-allocation layout dependence — localization and fix (2026-10-13)

## Mechanism

D3D12MA is created with `ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED` (`dx12_device.cpp:704-709`), so
every resource starts with its memory range's previous contents. `RTUVTestOutput` (8.3 MB RGBA8 +
2 descriptors, `rt_uv_test_pass.cpp:84-103`) is created at `scene_renderer_init.cpp:627-634`
whenever `m_rtManager->IsReady()` — note `r.RayTracing.TestPass` (`rt_manager.cpp:23`) gates only
`Execute`, not `Init`/allocation. Removing that allocation relocates everything created after it
(AutoExposure, Bloom, froxel fog, **SkyCubemap** at init:716, HiZ, TAA, SSR, colour-grading LUT,
all scene-load allocations, and every frame-0 render-graph transient, which are created on first
acquire with no clear: `render_graph_resources.cpp:89-116`). A pass that READS a resource before
anything WRITES it then sees different leftover bytes depending on the layout → the one-code-value
moves the identity harness saw on the reflection/fog-shaft/path-tracer references (but not
local-shadow/decal/hdri). The descriptor-index path was ruled out (the only heap-order-dependent
binding, the 4-wide `u0..u3` table, is bound only to single-UAV SM5 shaders). The render-graph
state registry fixes barrier STATE, not CONTENT, so it neither hides nor fixes this.

(Analysis by the architect consultant, claude-fable-5-1; full ranked candidate list in the task
transcript.)

## Fixes implemented (targeted, low-risk — guard the read until the resource is filled)

Both use the existing sticky `*Filled` flags, set by the atmosphere passes that write the
resources; steady state is unchanged (once filled, behaviour is identical), only the pre-fill
frame falls back to the shader's existing sentinel path instead of reading recycled memory.

- **#1 Path tracer sky cubemap.** `frame_lighting.cpp` — `ptDesc.skyCubeSrvIndex` (and the
  TLAS-path `ptDesc.skyCubemapSrv`) now resolve to `UINT32_MAX` until `m_skyCubemapFilled`, so the
  PT shader uses its flat-skylight sentinel (`path_tracer_ps.hlsl:52`) on frame 0 instead of
  sampling the uninitialised cubemap. Without this, the frame-0 garbage sample propagates through
  the progressive accumulation (`(prev*n + s)/(n+1)`, never reset mid-converge) and so shows up at
  the measured frame. HDRI path (`m_hdriCubemapSrv`, uploaded at scene load) is unaffected.
- **#3 Aerial-perspective volume.** `frame_constants.cpp:1004` — `perFrame.apVolumeSrvIndex` is now
  gated on `m_apVolumeFilled` as well as a valid SRV, so deferred lighting / the sky pass do not
  sample the AP volume before its first dispatch (the best-supported candidate for the Sponza
  cross-process one-code-value spread).

EnvMIS_Temporal (`frame_lighting.cpp:~204`) has the same latent shape but is currently guarded by
`EnvMapSampler::m_ready` being false until the frame-0 bake, so it was left untouched.

## Verification status

- Build: vendored zig 0.15.2 exit 0; owned gtests green (see task report).
- **Remaining empirical gate (not yet run):** the pre-registered gate is "with and without a
  diagnostic allocation-layout shift, the reflection/fog-shaft/path-tracer references are
  bit-identical; the other cases unchanged." The RT-UV toggle (`--rt-test`) only skips `Execute`,
  not the `Init` allocation, so it does not move the layout as-is; a clean test needs either an
  Init-skipping lever or — stronger, recommended by the architect — a `r.Debug.PoisonAllocations`
  mode that fills fresh allocations with two different poison patterns and asserts the references
  are bit-identical across them (this fails for the next uninitialised read too, not just this
  one). This GBV/empirical confirmation is deferred (this workstation cannot bring up the DX12
  debug layer, 0x887A002D).
- Also flagged by the architect, not yet addressed (lower priority / different trigger):
  render-graph transients with writer early-outs inside execution callbacks (a class;
  `render_graph_resources.cpp` has no UAV/buffer clear); `auto_exposure.cpp:523-537` accepting a
  never-written readback on the first Execute (r.AutoExposure=1 only); radiance-cache `hasHistory`
  derived from buffer contents. Recommended durable guard: a static "first access is a Read" log in
  `RgBuilder::Compile`.
