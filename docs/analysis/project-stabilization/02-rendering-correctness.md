# 02. Rendering correctness

Return to the [index](README.md). Evidence refers to the working tree described in
[01](01-scope-and-evidence.md). None of these findings was GPU-reproduced in this audit.

## F-001 — Temporal history state and validity are conflated

**Priority:** P1. **Confidence:** high, source-confirmed barrier mismatch and validity coupling.

**Evidence:** [frame_postprocess.cpp](../../../runtime/engine/renderer/frame/frame_postprocess.cpp),
lines 268, 307–319, 365–393; [scene_temporal_resources.cpp](../../../runtime/engine/renderer/scene/scene_temporal_resources.cpp),
lines 35 and 157; [scene_upscaler.cpp](../../../runtime/engine/renderer/scene/scene_upscaler.cpp),
lines 19–46.

The upscaler writes a history texture and transitions it from UAV to SRV for
tonemapping. On the following frame that texture becomes the read target, but its
barrier assumes the previous state was UAV. `firstFrame` comes from
`m_prevViewProjValid`, which becomes true even when the upscaler did not run.
Previous camera-matrix validity therefore cannot prove valid temporal contents
or the actual resource state. Switching AA/upscalers needs explicit treatment.

**Impact:** incorrect resource-state declarations and use of invalid or unrelated
history can affect startup, toggles and temporal stability. This is not proof of
the cause of every reported flicker.

**Correction and acceptance:** track real states through the existing registry;
separate matrix validity, history-content validity and successful output creation.
Test first/second frames, TAA off-to-on, upscaler changes, camera cuts, scene changes
and resize. A state-truth test and GPU diagnostics must agree, with no old-scene
history reused. See the existing state-registry task in the execution plan.

## F-002 — Mesh velocity contains camera motion only

**Priority:** P1. **Confidence:** high for the reviewed mesh path; visual impact unmeasured.

**Evidence:** [frame_geometry.cpp](../../../runtime/engine/renderer/frame/frame_geometry.cpp),
lines 392–419; [velocity_ps.hlsl](../../../runtime/shaders/runtime/postprocess/velocity_ps.hlsl),
lines 110–132. The full-screen pass reconstructs a current world position from
depth and reprojects it with the previous camera matrix. It has no previous
object transform or previous deformed vertex position.

**Impact:** at a stationary camera, a moving mesh cannot obtain its own motion
from camera reprojection. Temporal upscalers lack information needed to follow
rigid, skeletal and world-position-offset motion. Particle paths require their
own assessment; this finding does not say all particles lack velocity.

**Correction and acceptance:** establish previous transforms/poses/deformation
data and the velocity convention shared by consumers. Capture the velocity
buffer for a translating rigid mesh, animated skeleton and supported vertex
deformation, at both fixed and moving cameras. Expected direction and magnitude
must match projected displacement, including jitter and render/output scaling.

## F-003 — Render-graph synchronization and resource declarations are incomplete

**Priority:** P1. **Confidence:** high for the synchronization gaps; production
manifestations and the transient-pool exposure require runtime validation.

**Evidence:** [render_graph_builder.cpp](../../../runtime/engine/renderer/render_graph/render_graph_builder.cpp),
lines 496–529 and 571, emits transitions for differing states and skips equal
states; [dx12_command_list.cpp](../../../runtime/engine/rhi/dx12/dx12_command_list.cpp),
lines 662–668, already supports same-state UAV barriers.
[frame_lighting_pass.cpp](../../../runtime/engine/renderer/frame/frame_lighting_pass.cpp),
lines 113–118, uses a depth dependency to order light culling instead of declaring
the actual cluster-buffer relationship.

**Cause:** state transitions alone do not express all ordered UAV accesses.
Transient resources start as Undefined in graph bookkeeping and pooled reuse
does not preserve final state in the same way as imported-resource tracking.
Callbacks that operate on undeclared internal resources also conceal dependencies.
The existing registry is real progress, but it does not automatically complete
its migration or solve every UAV ordering and transient-lifetime case.

**Impact:** missing synchronization or incorrect before-states; graph scheduling,
culling and future pooling/aliasing changes cannot safely rely on incomplete
declarations. Most reviewed production resources are imported, so the transient
pool issue is not attributed to the current visible bugs without a reproducer.

**Correction and acceptance:** integrate with `phase0_render-graph-state-registry`;
test dependent UAV-to-UAV access, first use and reuse of transient resources,
import deduplication, resize/recreation and actual buffer edges. Declare accesses
where the resources are consumed. Include a negative control that detects a
deliberately omitted required barrier. Do not claim current reordering corruption
solely from a hidden dependency.

**API references:** [D3D12 UAV barriers](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_resource_uav_barrier)
and [resource-state synchronization](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12).

## F-004 — Generated material graphs receive dummy context

**Priority:** P1. **Confidence:** high, source-confirmed on the generated-material path.

**Evidence:** [gbuffer_ps.hlsl](../../../runtime/shaders/runtime/gbuffer/gbuffer_ps.hlsl),
lines 122–130, passes white vertex color, zero camera/object positions, zero time
and `positionCS.xy / float2(1920, 1080)` to `ComputeMaterialOutputs`.
[hlsl_material_translator.cpp](../../../runtime/engine/material/compiler/hlsl_material_translator.cpp),
lines 442–450, 510 and 520, consumes these inputs for view/screen/time/Fresnel/panner
nodes. [material_pipeline.cpp](../../../runtime/engine/renderer/material/material_pipeline.cpp),
lines 398–401, connects generated code to that shader.

**Impact:** a graph can compile and render while camera-dependent, animated,
vertex-colored or screen-space nodes produce incorrect results. The fixed
screen divisor also makes results resolution-dependent.

**Correction and acceptance:** define the per-frame, per-object and vertex input
contract, then supply real values. Diagnostic graph scenes must cover elapsed
time, camera/object movement, vertex colors and at least two viewport sizes.
Validate the actual compiled graph draw, not only emitted HLSL strings. Review
constant-buffer layout compatibility when extending the contract.

## F-005 — Unavailable FSR2 can still be selected as an output producer

**Priority:** P1. **Confidence:** high for the disabled-SDK branch.

**Evidence:** [fsr2_upscaler.cpp](../../../runtime/engine/renderer/passes/postprocess/fsr2_upscaler.cpp),
lines 16–17 and 540–563: FSR2 is disabled by default and that branch's `Execute`
does nothing. [temporal_upscaler.cpp](../../../runtime/engine/renderer/passes/postprocess/temporal_upscaler.cpp),
lines 30–31, still creates the class; `scene_upscaler.cpp:34–43` enables the selected
type without a successful initialization result. `frame_postprocess.cpp:362–378`
then chooses the presumed history output for tonemapping.

**Impact:** when the temporal path is active, selecting the unavailable backend
can make the display consume stale or unwritten output. Merely logging a warning
does not restore a valid producer.

**Correction and acceptance:** make capabilities and initialization/dispatch
success explicit; reject unsupported selection or select a valid fallback with
an observable result. Build without the SDK, select FSR2 and verify valid current
output. Test initialization failure and runtime switching too. SDK-enabled image
quality remains with `phase2_fsr2-full-integration`; a selectable no-op must not be
presented as a working capability.

## F-006 — The internal path tracer is not an independent rendering oracle

**Priority:** P2. **Confidence:** high for shared code and approximations; recommendation
concerns validation scope, not a measured image error.

**Evidence:** [path_tracer_ps.hlsl](../../../runtime/shaders/runtime/pathtracing/path_tracer_ps.hlsl),
lines 272–275, shares `GITrace_GdfNormal` and `GITrace_HitAlbedo` with GI support
code. Lines 308–321 use a roughness-based interpolation between mirror and random
directions and a roughness-dependent sky LOD.

**Impact:** agreement with the raster/GI path can preserve a shared defect;
approximate transport/material support cannot establish full physical parity.

**Correction and acceptance:** document supported materials, geometry, sampling
and limitations; use it for the checks it can establish. Independent quality
acceptance stays with matched Falcor references at PSNR >= 35 dB. A shared-input
negative control must not become a false proof of correctness. Full tracer
implementation belongs to its existing Rulebook tasks.

## F-007 — The reviewed glass path assumes spherical geometry

**Priority:** P2. **Confidence:** high for the assumption; severity depends on the
intended contract of shading model 9.

**Evidence:** [translucent_ps.hlsl](../../../runtime/shaders/runtime/gbuffer/translucent_ps.hlsl),
lines 160 and 198–216, derives a sphere center from `obj_model[3]`, a radius from
the first matrix row, and an exit point from that sphere.

**Impact:** arbitrary glass meshes and nonuniform scaling do not satisfy that
geometric model. If this is intentionally a sphere-only model, the limitation
must be explicit in authoring and documentation.

**Correction and acceptance:** define sphere-only versus general thin/volume
glass semantics before changing the algorithm. Verify sphere, flat pane and
nonuniformly scaled mesh against the declared contract. General transmission
needs published-reference research, diagnostics and matched Falcor comparison;
do not silently claim arbitrary-volume support.

## F-008 — Several exposed post-process controls have no active implementation

**Priority:** P2. **Confidence:** high, source-confirmed capability gap.

**Evidence:** [frame_postprocess.cpp](../../../runtime/engine/renderer/frame/frame_postprocess.cpp),
lines 31–45 and 254–258, identifies inert controls and the simplified TAA + Bloom
+ Tonemap chain. The [existing post-process task](../../../.rulebook/archive/2026-10-10-phase0_postprocess-chain-restore/proposal.md)
records that multiple shader bodies are scaffolds and that the historical wiring
was already unreachable; the recent cleanup did not create that feature gap.

**Impact:** settings may imply motion blur, DOF, CAS, FXAA or other effects that
do not run. Connecting incomplete kernels can make output worse or invalid.

**Correction and acceptance:** publish capability status consistently and keep
unfinished passes out of the active graph. Implement a selected effect only as a
complete, independently validated unit, with diagnostic inputs and observable
output. The existing task owns capability documentation; this audit does not
authorize building every missing effect or reopen a resolved jitter fix.
