## 1. Diagnose

- [ ] 1.1 Add `UZ_VFX_DIAG_ATTR_READBACK` macro gating a per-frame readback of `gpuBuf.WriteSet().positions[0..3]` immediately after the end-of-tick `UAV -> ShaderResource` transition in `frame_vfx_inline.cpp`. Log the float4 values + the corresponding `lifespans` / `ages` values at `Info` level. Run the demo with `mesh_sort_shards.uzvfx` and capture the log. Three possible outcomes:
   - Readback returns sane values close to `(0, 0, 0..150)`: the spawn / update writes are correct, the bindless read inside the mesh CS is the bug (path 1, 2, or 3 in proposal).
   - Readback returns garbage > 1000: the spawn or update CS writes garbage. Check `emitter->worldPosition`, `attach.resolved` path, and the CB build site (path 4).
   - Readback fails / returns zeros: barrier itself is wrong or the buffer is being reset between sim and readback.

- [ ] 1.2 Force-emit a `UAV -> UAV` flush barrier on each attribute buffer immediately before the `UAV -> ShaderResource` transition in `frame_vfx_inline.cpp:1043`. Build, run, screenshot. If the mesh particles appear visibly correct in the sky+ground scene without the predecessor's "tetras engulf the camera" pathology, root cause was path 2 — the SR transition was happening before the spawn / update UAV writes had reached visibility. Document the result either way.

- [ ] 1.3 Capture a RenderDoc frame of the demo running `mesh_sort_shards.uzvfx --headless --frames 80`. In RenderDoc Pipeline State viewer, inspect the resource state of `VFX_MeshInstanceTransforms`, `VFX_PositionsB`, `VFX_VelocitiesB`, `VFX_AgesB`, `VFX_LifespansB`, `VFX_ColorsB` immediately before the `VFX_MeshInstanceCS` dispatch. The state PIX shows is the ground truth — `UAV` means RG corruption (path 1); `ShaderResource` means the reads are accessing the right state but data is wrong (rules out path 1, points at path 2 or 4).

- [ ] 1.4 Add a one-shot `UZ_LOG(VFX, Info)` in `frame_vfx_inline.cpp` spawn CB build site dumping `emitter->worldPosition`, `emitter->isEmitting`, and the resolved `attach.position` (if any). Confirms whether the asset emitter is being given the correct origin. Compare against the spline_follow asset (which renders correctly) to see if their `worldPosition` differs at runtime.

- [ ] 1.5 Triangulate the four diagnostic outputs (1.1 readback, 1.2 UAV barrier test, 1.3 RenderDoc state, 1.4 worldPosition log) into a written root-cause statement. NOT acceptable: "I think this is the issue" — only "1.X result Y rules in / rules out path Z" is acceptable. Pick the path the evidence converges on. If evidence converges on a path NOT enumerated in the proposal, document the new path.

## 2. Fix

- [ ] 2.1 Implement the surgical fix matching the diagnosed path. Path 1 = import attribute buffers into RG (gated on `firstCycle == false` to avoid the predecessor's TDR regression). Path 2 = explicit `UAV -> UAV` barrier before the state transition. Path 3 = move SR transition to after the per-emitter loop. Path 4 = fix `scene_vfx_sync.cpp` worldPosition / attach resolution.

- [ ] 2.2 Re-build runtime + demo (`zig build -Ddll=true -Doptimize=ReleaseFast` in `runtime/`, then `runtime/demos/vfx_viewport_app/`).

- [ ] 2.3 Re-run `--gpu-validation --headless --frames 30` and confirm zero `D3D12 ERROR` lines (regression guard against the RG-import-causes-TDR scenario the predecessor fix tripped on).

## 3. Verify visually

- [ ] 3.1 Capture `--vfx-asset vfx/tests/mesh_sort_shards.uzvfx --headless --screenshot e:/UzmiEngine/uzengine/.sandbox/visibility-fixed-v2.png --screenshot-frame 90 --frames 120`. The asset must remain the COMMITTED state (`scale: [3.0, 3.0, 3.0]`, `velocity: [0, 0, 200]`, `sort_mode: ViewDepth`) — do NOT tune the asset to mask the bug.

- [ ] 3.2 Read the PNG with the Read tool. Pass criterion: discrete tetrahedron mesh particles must be IDENTIFIABLE in the sky+ground scene. Pass = visible particle silhouettes against an OBVIOUS sky+ground composition (matches `.sandbox/spline-now.png` baseline composition + adds visible mesh particles). Fail = uniform color filling the viewport, OR camera-engulfed-by-geometry, OR fully black.

- [ ] 3.3 Capture three additional camera-frame variants (`--screenshot-frame 30`, `60`, `120`) to confirm particles persist across the spawn arc. Discrete particles must be visible in ALL three frames; if any frame shows engulfed-camera the fix is incomplete.

- [ ] 3.4 Verify the `mesh_sort_shards.uzfx` editor-schema sister-asset still loads + renders (use the demo's preset cycling or the editor harness — TBD whether a separate command line). Mesh particle path must work for both the runtime production loader (`.uzvfx`) and the editor / serializer schema (`.uzfx`).

## 4. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 4.1 Update or create documentation covering the implementation — overhaul `docs/engine/25-vfx/88-mesh-particle-render-path.md` section 7. Replace the "Visibility verified" claim from the predecessor with a corrected entry. Add an explicit "Failure modes investigated" subsection enumerating the four hypothesized root causes and which one matched. Embed the new `visibility-fixed-v2.png` screenshot AND keep the misleading `post-fix-final.png` for reference (annotated as "previous fix's appearance — actually broken").

- [ ] 4.2 Write tests covering the new behavior — extend `runtime/tests/engine/renderer/frame_particles_mesh_guard_test.cpp` (or a new sibling) with a regression test specific to whichever path was fixed. Examples: (path 1) verify the mesh-instance-CS RG pass declares `pb.Read` on attribute buffers when imported; (path 2) verify a `UAV -> UAV` barrier is recorded before the state transition; (path 4) verify the asset emitter's worldPosition is `(0,0,0)` after `ParticleEmitter::Initialize`.

- [ ] 4.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter="*MeshGuard*:*MeshAssetLookup*:*ParticleMeshPSO*:*Visibility*"` 100% pass; the `phase16_vfx-sort-pipeline-hardware-validation` 1.2 visual A/B re-run produces a screenshot with discrete identifiable mesh particles. Capture the post-fix screenshot and link it from the test description / docs.
