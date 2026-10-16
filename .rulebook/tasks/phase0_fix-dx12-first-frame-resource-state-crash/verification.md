# First-frame DX12 state audit (2026-10-14)

## Implementation

The archived state-registry task already changed the frame imports to
`ImportTrackedTexture`. No frame-index ternaries or additional runtime barriers
were added by this task.

- `runtime/engine/renderer/frame/frame_setup.cpp:488-510` imports HDR, GBuffer,
  ShadowAtlas, debug lighting targets and DBuffer through the persistent registry.
- `runtime/engine/renderer/render_graph/render_graph_states.cpp:31-36,79-85`
  resolves first use from the backend creation state: Common for colour/UAV
  textures, DepthStencilWrite for depth-stencil textures. This also handles a
  texture first used after frame zero or recreated on resize.
- `runtime/engine/renderer/passes/geometry/gbuffer_pass.cpp:48` requests
  DepthStencilWrite before GBuffer clears/writes. Import state is the previous
  actual state, not the state the first pass wants. ShadowAtlas is a depth
  texture (`shadows/shadow_atlas.cpp:31`), so the original task's first-frame
  Common requirement would itself be incorrect.
- `runtime/engine/renderer/ibl/ibl_baker.cpp:155-193,293-295,333-342,376-395`
  already guards invalid shader/pipeline creation, explicitly transitions
  Common to UAV and UAV to ShaderResource, submits and waits for completion.
  Execute has no HRESULT: RHI and D3D12 queue submission both return void.
  Close's HRESULT is checked in `rhi/dx12/dx12_command_list.cpp:233-238`,
  and debug messages are drained on submission.
- `runtime/engine/renderer/atmosphere/atmosphere_renderer.cpp:387-678` compiles
  out the removed Bruneton shaders/precompute. Active transmittance, AP and sky
  cubemap passes use tracked imports and declare UAV writes; their first-use
  barriers belong to the graph, not to the dispatch callback.

Changes here are documentation in `render_graph_builder.h` and
`atmosphere_renderer.h`, plus two behavioral regression tests in
`tests/engine/renderer/test_render_graph_states.cpp`:

- `RenderGraphStates.ShadowAndGBufferDepthTransitionFromActualPreviousState`
  checks first-use depth creation state and second-frame SRV-to-depth-write
  transitions before depth clears/writes.
- `RenderGraphStates.DBufferFirstUseAfterSkippedFramesStartsInCommon` checks
  all three decal targets first used after a graph that imported but never
  accessed them, followed by their next-frame SRV-to-RT transitions.

## Baseline reproduction

All engine/GPU runs were serialized. Each GPU run had a 180-second monitor
deadline. No shader changes were made.

1. `cd runtime && zig build -Doptimize=ReleaseFast`: exit 0, shader integrity
   reported 139 shaders verified.
2. `cd runtime/demos/vfx_viewport_app && zig build -Doptimize=ReleaseFast`:
   exit 2. Installed Zig 0.16 rejects the shared `demos/common/build_common.zig`
   at line 201 (ArrayListUnmanaged initialization lacks items/capacity) and
   line 439 (Compile.linkSystemLibrary no longer exists). Existing demo and
   VFX editor work was not edited.
3. `runtime/build/bin/uzengine.exe --project sample/projects/sponza.uzproject
   --frames 3 --verbose --gpu-validation`: exit 127, failed before rendering
   at `render_graph_builder.cpp:28`, `UZ_CHECK(id.IsValid())`. Log:
   `%LOCALAPPDATA%/UzEngine/logs/uzengine_2026-10-14_14-15-49.log`.
4. The same engine arguments using
   `scripts/rendering/sky-rebake-reference.uzproject`: exit 127. Log:
   `%LOCALAPPDATA%/UzEngine/logs/uzengine_2026-10-14_14-16-22.log`.
   Lines 18-19 confirm DX12 debug layer and GPU-based validation enabled.
   BRDF LUT bake succeeds at 14:16:23.372 with no BRDF barrier error.
   At line 439, Close fails with `0x80070057`; lines 440-443 report RT
   transitions applied to depth resources and depth-write transitions applied
   to colour resources. Later lines 459 and 508 report `0x80004005`, followed
   by the fatal error loop at line 509. This run DOES reproduce Close failure
   and barrier errors; it is not zero-error verification.

The source declarations already contain the missing states; the wrong resource
types in the first errors are consistent with a stale mixed `FrameContext`
layout after the unrelated uncommitted addition of two Brixelizer resource IDs.
A fresh-cache engine rebuild was started to test that hypothesis without
changing or reverting the user's renderer edits. It was stopped after over
three minutes with no compiler progress in the log. A bounded incremental
rebuild with compiler command output followed, then
`zig build -Doptimize=ReleaseFast -j8 --summary all` completed with exit 0 and
`Build Summary: 47/47 steps succeeded`. The installed engine and test binaries
were newly linked at 17:26:19 and 17:26:24 UTC. The test binary lists both new
regression tests, rather than silently testing an older installed executable.

The exact layout change is visible in the user's existing diff:
`scene_renderer_frame.h:122-123` inserts `brixDiffuseRes` and `brixSpecularRes`
between `hdrRes` and `depthRes`, moving every subsequent ID by eight bytes.
The old setup layout's albedo slot is the new depth slot, and its ShadowAtlas
slot is the new emissive slot. Thus a mixed binary asks for depth-write on
GBuffer_Albedo and render-target on ShadowAtlas. The new debug MRT slots also
read the old DBuffer IDs, leaving the physical Debug_IndirectLight uncleared
by any graph transition. This matches the first invalid resource-type
barriers and the later named invalid clears in the reproduction log.

The identical three-frame headful sky GBV command against the newly linked
engine exits 0. Log
`%LOCALAPPDATA%/UzEngine/logs/uzengine_2026-10-14_14-28-42.log:18-19`
confirms debug layer and GPU-based validation. There are zero Close failures,
zero barrier errors (ids 527/538/607/942), and no shader compile failures.
At line 567: `Render loop finished: 7 frames, 61.5ms total, 8.78ms/frame
(114 FPS)` (the three requested frames follow preload frames).
No runtime import, barrier, FrameContext field, or unrelated renderer code
was changed to compensate for the mixed layout. The minimal fix is rebuilding
the mixed artifacts coherently.

The run is NOT free of every D3D12 error: lines 490-495 report id 1422 on all
six SkyCubemap subresources during SH `CopyTextureRegion`, and lines 522/537
report id 1422 on GBuffer_Velocity reads. These are CREATE_NOT_ZEROED
render-target first-use initialization requirements, not state-before/state-after
barrier mismatches. They were exposed after the crashing mixed-layout
barriers were removed and are not suppressed or represented as fixed.

The standalone VFX demo build was also attempted with the same ReleaseFast
command and a 180-second bound; it exits 2 with the same shared Zig 0.16
incompatibilities at `build_common.zig:201,439`.

The installed CPU-only suite `test_runtime.exe --gtest_filter=RenderGraphStates.*`
passes all 16 tests, including the two new tests (delta +2 from 14). Both the
positive eight-frame matrix and its negative control pass. Full-suite counts
are recorded below.

The 120-frame headful main-binary GBV run also exits 0 within its 180-second
bound. It uses the same sky project and adds `--frames 120 --screenshot
E:/UzmiEngine/uzengine/runtime/build/first-frame-state-sky80.png
--screenshot-frame 80`. Log `uzengine_2026-10-14_14-31-56.log` reports:

```text
[2026-10-14 14:31:56.591] [RHI] [info] DX12 debug layer enabled
[2026-10-14 14:31:56.591] [RHI] [info] DX12 GPU-based validation enabled (performance will be significantly reduced)
[2026-10-14 14:33:30.744] [Screenshot] [info] Screenshot saved: path='E:/UzmiEngine/uzengine/runtime/build/first-frame-state-sky80.png' (640x360)
[2026-10-14 14:33:30.900] [Engine] [info] Render loop finished: 124 frames, 350.6ms total, 2.83ms/frame (354 FPS)
```

The 124 total includes four preload frames. No Close failures, FATAL errors,
barrier errors or shader compile failures occur. The same eight non-barrier
id 1422 initialization errors remain. The saved image was read and inspected:
sky and ground are visible, not black. This sky-only scene does not prove
VFX particles; the main-engine VFX Showcase capture below supplies that evidence.

## Commit coherence (0f066e97)

No FrameContext source fix was applied in this task. Its before/after layout
refers to the committed header versus the user's pre-existing worktree edit:

```cpp
// 0f066e97: existing committed layout
rg::RgResourceId bbRes{};
rg::RgResourceId hdrRes{};
rg::RgResourceId depthRes{};
rg::RgResourceId normalRes{};
rg::RgResourceId albedoRes{};
// ... position, emissive, velocity, shadow, debug and DBuffer IDs

// Existing worktree layout, kept unchanged by this task
rg::RgResourceId bbRes{};
rg::RgResourceId hdrRes{};
rg::RgResourceId brixDiffuseRes{};   // scene_renderer_frame.h:122
rg::RgResourceId brixSpecularRes{};  // scene_renderer_frame.h:123
rg::RgResourceId depthRes{};
rg::RgResourceId normalRes{};
rg::RgResourceId albedoRes{};
// ... same remaining IDs, shifted by eight bytes
```

`git grep` against the actual commit proves
`0f066e97:frame_gi_passes.cpp:403-404,409-410` already assigns those two
FrameContext fields and declares the Brixelizer diffuse/specular atlas UAV
writes. Its committed `scene_renderer_frame.h` has neither member. That source
tree cannot compile this function as written, even when the CVar defaults off:
runtime gating does not make missing C++ members legal. A separate checkout
build was not needed or run to discover this source inconsistency.

The header edit is therefore REQUIRED with the Brixelizer change, not a
disposable ABI workaround. The current uncommitted follow-up also needs:

- `frame_lighting_pass.cpp:197-198` transfers those graph IDs into LightingPassDesc.
- `passes/lighting/lighting_pass.h:60-61` declares brixDiffuseResource/brixSpecularResource.
- `passes/lighting/lighting_pass.cpp:44-46` declares SRV reads after the GI UAV writes.
- `scene_renderer_init.cpp:688-695` restores CVar-gated Brixelizer initialization;
  the committed code was still commented out.
- `frame_constants.cpp:867-871` gates the diffuse SRV on r.BrixelizerGI.

These six pre-existing files should form a coherent Brixelizer follow-up.
None was modified by this task. The FrameContext fields are graph IDs, NOT
shader heap indices. The committed shader has `brixGIDiffuseSrv` at line 152
and samples it at lines 1330-1331; there is no `brixGISpecularSrv` shader
field/read in the inspected commit. Its comment documents specular = diffuse+1,
but that is not a second sampled field in this deferred shader.

## Frame-80 VFX capture

The main binary renders the existing `sample/projects/vfx_showcase.uzproject`
with SkyAtmosphere and its Sparks emitter; no scene/demo source was changed.
The first authored-camera capture showed sky but did not establish visible
particles. The corrected capture uses:

```text
uzengine.exe --project sample/projects/vfx_showcase.uzproject --frames 120 --verbose
  --editor --camera-pos 0,-650,250 --camera-look 0,0,350
  --screenshot E:/UzmiEngine/uzengine/runtime/build/first-frame-state-vfx80.png
  --screenshot-frame 80 --cvar r.RadianceCache=0 --cvar r.GI.FinalGather=0
```

The editor flag leaves camera-driving Lua scripts dormant. The 180-second
bounded run exits 0; log `uzengine_2026-10-14_14-40-56.log` reports:

```text
[2026-10-14 14:40:56.607] [Engine] [info] Camera override: pos=(0,-650,250) look=(0,0,350)
[2026-10-14 14:40:57.309] [Screenshot] [info] Screenshot saved: path='E:/UzmiEngine/uzengine/runtime/build/first-frame-state-vfx80.png' (1920x1080)
[2026-10-14 14:40:57.699] [Engine] [info] Render loop finished: 125 frames, 781.1ms total, 6.25ms/frame (160 FPS)
```

The image was read and inspected: a large brown/olive billboard plume overlays
the blue sky and ground. To establish that this was genuinely the VFX output,
not atmosphere, a same-camera capture adds `--cvar r.Show.Particles=0` and saves
`first-frame-state-vfx80-no-particles.png`. It exits 0 and removes the plume,
leaving sky and checker ground. Both images were inspected; neither is black.
No shader compile failure occurs. These capture runs are visual verification,
not additional GBV runs; the sky-project GBV runs above supply barrier evidence.

## Outstanding verification

The requested `GTEST_FILTER="-RuntimeViewportTest.*" zig build test
-Doptimize=ReleaseFast --summary all` completed in 33.591 seconds:
7675 tests from 753 suites, 7655 passed, 17 skipped and three failed:

```text
VfxEditorSlate.WorkbenchHasTwoColumns
VfxEditorSlate.TitleBarReflectsEmitterName
VfxEditorSlate.StatusBarBindingsReflectTelemetry
```

Those are exactly the supplied pre-existing VFX editor failures; no test was
modified or excluded to hide them. Build-test exits 1 because of those
failures. The test step resolves the executable with b.getInstallPath to the
full `E:/UzmiEngine/uzengine/runtime/build/bin/test_runtime.exe` path.

Four SplineUzfx sample tests skip specifically because `zig build test` runs
from `runtime/`, while `test_spline_data_interface.cpp:72-76` searches
`../../sample`, `../../../sample`, `../../../../sample` and `sample/`.
None resolves this checkout's `../sample` from that CWD.

The full-path run from the repository root completed with exit 1 in 30.486
seconds and establishes the comparable baseline count:

```text
E:/UzmiEngine/uzengine/runtime/build/bin/test_runtime.exe --gtest_filter="-RuntimeViewportTest.*"
[==========] 7675 tests from 753 test suites ran. (30486 ms total)
[  PASSED  ] 7659 tests.
[  SKIPPED ] 13 tests
[  FAILED  ] 3 tests
```

The failed names are the identical three VfxEditorSlate tests above. This is
7657 baseline passes + the two added state tests, with no new failures.
The remaining 13 skips are the existing encoder/platform/shader-backend gates;
none of the added tests skipped. No source/test edits were made between the
two full runs, only the CWD changed.

## Files changed by this task

Source/documentation edits and creation (commit scope):

- `E:/UzmiEngine/uzengine/runtime/engine/renderer/render_graph/render_graph_builder.h`
- `E:/UzmiEngine/uzengine/runtime/engine/renderer/atmosphere/atmosphere_renderer.h`
- `E:/UzmiEngine/uzengine/runtime/tests/engine/renderer/test_render_graph_states.cpp`
- `E:/UzmiEngine/uzengine/.rulebook/tasks/phase0_fix-dx12-first-frame-resource-state-crash/tasks.md`
- `E:/UzmiEngine/uzengine/.rulebook/tasks/phase0_fix-dx12-first-frame-resource-state-crash/verification.md`

Generated capture artifacts (not source changes):

- `E:/UzmiEngine/uzengine/runtime/build/first-frame-state-sky80.png`
- `E:/UzmiEngine/uzengine/runtime/build/first-frame-state-vfx80.png`
- `E:/UzmiEngine/uzengine/runtime/build/first-frame-state-vfx80-no-particles.png`

Builds also generated the normal build outputs/caches and the isolated
`runtime/.zig-cache-first-frame-state/` cache. No git commit, archive or
reversion of pre-existing work was performed.

The Application-specific debug run and standalone demo regression remain
blocked by the shared demo build incompatibility. The 120-frame criterion
is verified through the main binary by user direction. Engine headless mode
creates a device only and is not a substitute for a rendered run.
LSP diagnostics could not run because clangd is not installed; the C++ build
and test runner provide compiler/runtime verification instead.

Final task ledger: 27 of the original 29 items are closed. Only 5.1
(Application-demo debug-layer gate) and 5.4 (standalone demo regression) remain
unchecked with the shared Zig 0.16 build blocker. The separate main-binary
crash acceptance items 8.1 and 8.2 are both closed. The three supplied
VfxEditorSlate test failures and eight non-barrier initialization messages
are recorded explicitly, not hidden or treated as regressions fixed here.


## 5.1 / 5.4 blocker — precise root cause (2026-10-14)

The vfx_viewport_app demo build failure is a TOOLCHAIN INCONSISTENCY, not a dx12-crash code issue:
- The project pins Zig 0.15.2 (vendored at .tools/zig-x86_64-windows-0.15.2; runtime/build.zig requires 0.15.2), but the machine PATH has Zig 0.16.0. demos/common/build_common.zig:201 (std.ArrayListUnmanaged(...){}) and :439 (build API) are 0.15.2-only and FAIL to compile under 0.16.
- Building the demo with the vendored 0.15.2 gets past build_common.zig (7/10 steps) but then fails at link: `lld-link: undefined symbol: std::__1::__hash_memory` — runtime.lib was compiled with 0.16.0's libcxx (engine built with PATH zig this session), which is ABI-incompatible with the 0.15.2 libcxx the demo links against.
- Fix path (separate build-system task): standardize the toolchain — build BOTH the engine (runtime.lib) and the demos with the SAME zig (the pinned vendored 0.15.2), or port build_common.zig + the engine to 0.16. Out of scope for the crash fix, which is verified on the main uzengine binary (GBV 120-frame, zero barrier errors).
