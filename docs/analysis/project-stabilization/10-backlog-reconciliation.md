# 10. Stabilization backlog reconciliation (2026-10-14)

Return to the [index](README.md). The [live checklist](../../../.rulebook/tasks/phase0_project-stabilization-audit/tasks.md)
owns every remaining acceptance criterion. This is a scoped reconciliation, not
audit-wide closure. Source inspected at HEAD `f517ecfe`, with existing shared-tree
cloud/VFX/editor changes left untouched. New hierarchy changes are uncommitted;
the parent session owns commits and archival.

## Completed work versus remaining acceptance

### F-003: registry migration complete; graph synchronization still open

`466df62e` landed the registry migration, now in the
[archived registry task](../../../.rulebook/archive/2026-10-14-phase0_render-graph-state-registry/tasks.md).
`runtime/engine/renderer/frame/frame_setup.cpp:488-510` imports HDR, GBuffer,
shadow, debug and DBuffer textures through tracked imports.
`render_graph_states.cpp:31-36,79-84` derives fresh color state as Common and
fresh depth state as DepthStencilWrite from the device descriptor.
`a5ac6275` adds actual-previous-depth-state and delayed-DBuffer-first-use regressions.
Existing `RenderGraphStates` tests cover fresh/recreated handles, deduplication,
submit decay, the eight-frame mode matrix and explicit-claim negative controls.

The [first-frame crash record](../../../.rulebook/tasks/phase0_fix-dx12-first-frame-resource-state-crash/tasks.md)
reports the coherent rebuild and bounded headful main-binary GBV run:
`uzengine_2026-10-14_14-28-42.log`, zero Close failures and barrier IDs
527/538/607/942. Eight initialization-error ID 1422 occurrences remain documented;
this is clean **barrier** validation, not zero errors of every class.
The separate viewport demos remain toolchain-blocked. These are existing recorded
GPU results, not new GPU runs in this reconciliation.

Item 3.1 also explicitly owns work beyond the archived migration:

- `render_graph_builder.cpp:515-530` schedules barriers only for different
  states; `:572` skips equal-state barriers at execution. The explicit registry
  `UavBarrier` helper/test is not automatic graph ordering for dependent UAV passes.
- `CreateTexture`/`CreateBuffer` start at Undefined (`:96-97,134-135`).
  Acquisition follows compilation (`:547`); pool reuse returns physical handles
  without state (`render_graph_resources.cpp:92-99,144-147`), and state commit
  excludes non-imported resources (`render_graph_builder.cpp:646`).
- `frame_lighting_pass.cpp:113-117` still declares a depth read to order
  LightCull, while its cluster-buffer UAV/SRV transitions occur inside the
  callback (`:119,125`), not through the real producer/consumer graph edge.

Therefore 3.1 stays unchecked, narrowed to dependent-UAV synchronization with an
omitted-barrier negative control, transient first-use/reuse truth and actual
cluster-buffer edges. No current-frame corruption is inferred from these gaps
without a reproducer.

### F-001, F-005 and F-022: real mitigations, not complete contracts

`466df62e` separated `m_taaHistoryWritten` from state knowledge and
`CreateTaaResources` resets it (`scene_temporal_resources.cpp:35-36`).
`frame_postprocess.cpp:269` checks both matrix and content validity.
However, `:348-349` assumes a void Execute wrote content and `:360` selects it
for tonemapping. Upscaler switches (`scene_upscaler.cpp:19-46`) do not reset
history, and the disabled FSR2 branch still has empty Execute
(`fsr2_upscaler.cpp:563`). Lifecycle resets and successful output production
remain separate open items 3.2/3.3.

The completed frame-allocation task's sky/AP content sentinels and accepted
one-code-value poison residual do not validate every temporal reset or shader
layout. Layout tests already exist: `test_constant_buffers.cpp`,
`test_deferred_pipeline.cpp`, and DXC/C++ reflection-offset comparisons with
negative-control shaders in `scripts/rendering/test_cbuffer_layouts.py`.
Item 5.3 now requires coverage of the remaining contracts and an accurate
capability matrix, rather than pretending no layout tests exist.

### F-006/F-008: published capability scope closes 3.7

Current PT source supersedes parts of the original F-006 description:

- GBuffer still supplies the primary hit; DXR inline queries supply bounce
  intersections. `path_tracer_ps.hlsl:204-216` first uses
  `GITrace_HitSurface` for triangle-interpolated attributes, falling back to
  shared material-table average albedo/GDF normals where unavailable.
- The primary specular estimator now uses VNDF GGX, not the old
  roughness-perturbed mirror (`path_tracer_ps.hlsl:425` onward). Subsequent
  transport still uses a narrower diffuse model; forced-opaque ray queries
  do not establish general transmission or full material parity.
- Direct equirectangular environment lookup, progressive accumulation and
  environment MIS infrastructure exist. The recorded Lua-HDRI fixture can leave
  the alias table unavailable; convergence there does not certify active MIS.
  RT-off sky fallback is not geometry-occluded transport.
- Shared GBuffer, GI/material and environment helpers prevent the internal PT
  from being an independent oracle. A shared-input error can make both outputs
  agree: that agreement is expressly insufficient acceptance.

`48e63f3b` and [PT results, Tier-4c/Tier-5m](../path-tracing/09-test-results.md)
record a real Falcor 8.0 gate, explicit capture-frame correction and proxy
roughness-row results. The gate uses a **single global energy scalar**;
raw, scalar-fit and per-channel metrics must remain distinct. Near-mirror
roughness 0.05 remains about 30.46 dB (known geometry-parity gap); the textured
helmet has visual fidelity evidence rather than a clean matched numeric pass.
No normalized proxy score closes this audit's full raw >=35-dB matrix.
Earlier PT spec/reconciliation paragraphs saying Falcor is absent or VNDF is
unimplemented are historical/stale; the later Tier-4b/4c/5m measurements and
current shader are the evidence used here.

`2cc2e85c` already published the
[post-process capability inventory](../post-process/postprocess-chain-status.md).
Current `frame_postprocess.cpp:258-265,492-526,628-640` retains the simplified
temporal/SSR/Bloom/Tonemap path and explains the absent full chain.
Motion blur, DOF, CAS, FXAA-related and chromatic-aberration controls are explicitly
inert (`:32-47`); lens-flare controls likewise say inert
(`passes/postprocess/lens_flare_pass.cpp:36-40`). CAS/FXAA, motion blur, DOF,
lens flare and color-grading LUT are not newly wired here.
Historical inventory and before/after negative controls live in
`scripts/rendering/test_postprocess_chain.py`; its recorded Sponza nonidentity
is not rewritten as a pass. Item 3.7 is capability/claims reconciliation only.
Full PT parity, unfinished effects and the broader matrix remain with their
existing owners and item 4.1.

## F-012: shared hierarchy rejection

`SceneComponent::AttachTo` walks the proposed parent's ancestor chain before
detaching. Encountering this component rejects the operation without modifying
parents, child lists, transforms or dirty flags. Valid reparenting preserves
local transforms; null parenting detaches. The public void API is unchanged.
The direct Lua caller (`lua_scene_api.cpp:384`) reaches the same boundary.
The scene loader currently has no separate AttachTo call or parent-pointer
write; future callers must use this boundary rather than bypass it.
No ECS slot/version or world-thread ownership change is needed for this guard.

New regressions:

- `SceneComponentTest.SelfParentingRootLeavesStateUnchanged`
- `SceneComponentTest.SelfParentingPreservesExistingParent`
- `SceneComponentTest.AncestorCyclePreservesHierarchyAndTransforms`
- `SceneComponentTest.ReparentAndNullDetachPreserveLocalTransform`
- `LuaSceneExtendedTest.AttachTo_RejectsSelfAndAncestorCycles`

These test rejection against both root and existing-parent hierarchies, clean
cached transforms/dirty flags, a three-edge ancestor cycle, valid reparenting
and detach. The Lua test exercises the actual binding and checks both links
and child counts. Existing native tests cover ordinary attach/detach/destruction.
The unsafe operations are negative controls: removing the guard permits cycles
or changes links/dirty flags and fails these assertions.

## Remaining programme

The checklist records a current source location, classification and scoped
acceptance for each of the 20 previously open items. ECS handle identity,
world ownership, transactional loading, IPC/H.264 pressure, temporal outputs,
object motion, real material context, glass authoring and graph lifetime rules
are multi-session engineering, not blocked attempts disguised as completion.
DLSS discovery is a small implementation candidate but still needs an exercised
non-default-location discovery seam; loose-file VFS is a separate measured
concurrency candidate. Neither is accepted on source inspection alone.
Items 6.1-6.3 remain umbrella requirements; this slice's docs/tests do not
complete the remaining programme.

## Executed verification for this slice

- `cd runtime && zig build -Doptimize=ReleaseFast`: exit 0.
- Serialized test commands used `GTEST_FILTER="-RuntimeViewportTest.*"` for
  `zig build test -Doptimize=ReleaseFast`, followed from the repository root by
  the full-path `E:/UzmiEngine/uzengine/runtime/build/bin/test_runtime.exe
  --gtest_filter="-RuntimeViewportTest.*"`. Each command had a 180-second bound.
  The terminal's retained tail contains the final full-path result below;
  a separate exact Zig-run count/exit is not retained in that tail.
- Full-path result: **7680 tests, 7664 passed, 13 skipped, 3 failed** in
  **31.686 seconds**, exit **1**. All five new hierarchy tests pass, adding
  five passes to the comparable 7659-pass baseline. The only failures remain
  `VfxEditorSlate.WorkbenchHasTwoColumns`,
  `VfxEditorSlate.TitleBarReflectsEmitterName` and
  `VfxEditorSlate.StatusBarBindingsReflectTelemetry`, the pre-existing user
  VFX-editor WIP. No test was weakened or excluded beyond the requested
  headless-hanging `RuntimeViewportTest.*`.
- `git diff --check`: passed. Existing CRLF conversion warnings in unrelated
  cloud documentation were not modified. LSP diagnostics could not run because
  clangd is not installed; the actual compiler/build and native tests ran.
- No new GPU, TypeScript, IPC stress, VFS benchmark or full Falcor matrix run
  is claimed. Rendering capability closure above uses explicitly named existing
  revisions and evidence, not this C++ hierarchy run.

Two of the original 20 open checklist items are now closed: 2.4 (fixed here)
and 3.7 (completed-work capability reconciliation). The checklist is **10/28
done, 18 open**. Item 3.1's migration portion is accepted, but the complete
item remains open for its independently verified residuals.
