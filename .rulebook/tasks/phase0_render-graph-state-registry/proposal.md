# Cross-frame resource states: a central registry for the render graph

## Why

The audit of 2026-10-08 (a workflow over every renderer subsystem, each claim checked by an adversarial
verifier) confirmed 80 cross-frame resource-state mismatches out of 89 claims. The render graph takes an
import's state on the caller's word, never restores imported resources, and the DX12 RHI copies every
`stateBefore` verbatim into the D3D12 barrier; only HDR, depth, albedo and normal carry their end state to
the next frame (`GetFinalState`, scene_render_frame.cpp:245-248). Every other persistent resource uses a
hard-coded or predicted state: the froxel fog volumes (imported as UnorderedAccess every frame, left in
ShaderResource: wrong from frame 2; block B.4b's own code), the shadow atlases, the DBuffer, the Light_*
debug targets, the path tracer's accumulation pair, the AP volume, the transmittance LUT, the sky
cubemap, the VFX mesh buffers, and the manual barriers of TAA, SSR, Hi-Z, bloom, GTAO, the env-MIS
history, the radiance cache, DDGI, auto exposure and the light-cull lists. Resize and recreation reset
nothing. NVIDIA drivers tolerate most of this; it is undefined behaviour the D3D12 debug layer would
report (unavailable on this workstation through the Agility factory: 0x887A002D), and the clouds'
render-graph migration (0c.1) and the path tracer's Phase A need correct cross-frame states.

## What Changes (staged)

- Stage 0 (no behaviour change): `rg::RgStateRegistry` (render_graph_states.h/.cpp) keyed by the
  generational texture / buffer handle; `ImportTrackedTexture/Buffer`; dedup of a handle imported twice;
  the observe mode (an explicit claim that disagrees with the registry is recorded and logged once); the
  commit of every imported resource's final state after Execute; buffer decay at each queue submit;
  SceneRenderer owns the registry; MockDevice gtests (the design's cases 1-12).
- Stage 1: the graph imports switch to tracked imports, with the required fixes (SSR_CopyPrevColor on the
  non-TAA path, m_hdrHasContent on Resize, the velocity reads); the frame-0 warmup and the four end-state
  members go.
- Stage 2: the missing reads declared (path tracer atlas, local shadow atlas, depth in the upscaler).
- Stage 3: the manual barriers migrate to `Transition`.

## Gates (per stage, registered before its first build)

S0: test_runtime RenderGraphStates* all pass, including the negative control (today's claims reported as
mismatches); RenderGraph* unchanged; the observe log on Sponza, the fog reference and the reflection
reference lists the audit's graph-visible items; frames bit-identical to the build before (reflection
reference; Sponza within its same-binary floor). Later stages: the observe log empty for graph imports, the
same identity.

## Impact
- Affected specs: render-graph-states (new)
- Affected code: runtime/engine/renderer/render_graph/*, scene/scene_render_frame.cpp, frame/frame_setup.cpp,
  the import sites, later the manual-barrier sites; runtime/tests/engine/renderer/test_render_graph_states.cpp
- Breaking change: NO (barrier before-states become correct; no rendering change expected)
- User benefit: correct D3D12 usage, the foundation for the clouds' graph migration and the path tracer fixes.
