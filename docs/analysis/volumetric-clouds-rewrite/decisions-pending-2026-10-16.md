# Decisions pending — autonomous lighting run (2026-10-16)

The user is away ~10h and asked me to run the lighting (volumetric-clouds) tasks
in sequence, saving any decision that needs them and skipping non-blocking items.
This file records decisions left for the user. Each entry: what, why it needs you,
the default I took (if non-blocking) or why I skipped (if blocking).

Status: in progress. HEAD at start: 27a21d36.

## Decisions / notes

### D1 (2026-10-16) — clouds M4b / item 4.10: how much cloud-ground-shadow silhouette polish?

**Context.** The near-black ground under cloud ground-shadows (gate r.Clouds.GroundShadows, default OFF) was a SCENE issue, not a shader bug: the old m4-ground scene was fully overcast with no skylight, so a sun-blocked ground went to pure black. The deferred_lighting cloud-shadow term already correctly scales ONLY the direct sun (renderer source unchanged; default-off DXIL byte-matches HEAD). On a scattered-cloud scene WITH a skylight (m4-scattered.uzscene), the ground now renders LIT grey (not black) with GroundShadows=1; GBV exit 0, gtests 7661 pass.

**What still needs a call.** From the current horizon framing the cloud-SHAPED shadow silhouettes are NOT readable on the ground (it's uniform lit grey; see .screenshots/cloud-m4/scattered-gbv/frames/f_15.png). Making them readable needs a top-down camera looking at the ground plane + denser scattered clouds + a lower sun angle — iterative scene/tuning work on an OPT-IN feature (default OFF, so the shipped product is unaffected either way).

**Default I took (non-blocking):** committed the verified improvement (ground is now LIT not black; feature is correct by construction + default-safe), marked 4.10 as "ground-black fixed; readable silhouettes deferred", and moved on to M6.

**Decision for you:** (a) accept as-is (ground-lit + correct, silhouettes an opt-in known-limit), or (b) you want full silhouette polish (I author a top-down scene + tune density/sun until shadows read clearly). Tell me (b) and I'll finish it.

**D1 resolved (2026-10-16):** user chose "investe no polish". Scene-only
`scripts/rendering/cloud-shadow-silhouettes.uzproject` now shows separated,
dark lobed footprints with legible soft edges on lit ground; opened capture:
`.screenshots/cloud-d1-polish/coherent/frames/f_28.png`. Global gate remains
OFF. Fresh reflection-reference frame 95 matches `f005-after` exactly
(max_code_delta 0). Build/layouts exit 0; gtests 7791 pass / same three
pre-existing VfxEditorSlate failures, delta 0. Item 4.10 is resolved opt-in.
Full settings, control capture, checks and ownership:
`08-d1-silhouette-polish-2026-10-16.md`.


### D2 (2026-10-16) — clouds-unrelated: F-007 glass changes the Sponza window-glass appearance; confirm the keep-neutral parity baseline
F-007 (thin-transmission glass, audit 3.6) is implemented + verified + committed: transmission=0 materials are BYTE-IDENTICAL, and glass now correctly shows the SCENE BEHIND it (refracted) instead of the old sphere-only + IBL-only 'refraction' (a real bug: the per-frame scene-color copy was dead/unread). BUT the Sponza window panes are promoted to ThinTranslucent (transmission>0), so the SHIPPED Sponza render's glass will now LOOK DIFFERENT (scene-behind vs sky). The subagent kept 3.6 [ ] pending the design's Sponza/Three.js parity review + the keep-neutral baseline sign-off - that is YOUR call. Decision for you: review the Sponza glass appearance and confirm the baseline move (accept the corrected glass), or tell me to gate/tune it. The implementation is correct + default-safe; this is only about accepting the Sponza visual change.

### N1 (2026-10-16) — NOT a decision; status note: F-004 WIP, deep-low lane exhausted
Autonomous lighting run progress: clouds M0-M6 done (597477f4/c6027060/849e5fef) + F-001 (42b64396) + F-005 (e782f6b7) all committed, verified, default byte-identical. Then started F-004 (material-graph context); the deep-low delegation lane hit its usage limit mid-work. F-004 is UNCOMMITTED WIP in the tree (translator contextMask + gpu_mesh colour stream + tests landed by helpers; the VS/PS context wiring was in progress). HEAD is clean at e782f6b7. A shipped-G-buffer-shader change is NOT committed without a render default-unchanged proof, which needs the engine + (for the baseline) the lane. Armed a ~96-min monitor to resume when deep-low resets: finish+verify F-004 (build + render no-compile-fail + default-unchanged + gtests) -> commit, then F-002 velocity, then F-007 glass. No user decision needed; this is a resource wait.


### N2 (2026-10-16) — status: F-004 DONE; F-002/F-007 await deep-low reset
F-004 (material-graph context, audit 3.5) committed 6c1fcbe3 - I verified it myself in the main session (deep-low still exhausted): build + reflection-reference frame-95 render BYTE-IDENTICAL to the pre-change baseline (f005-after) + gtests 7707. Audit 15/28; 7 verified commits this autonomous run. F-002 (velocity) + F-007 (glass) remain - both large net-new shipped-path implementations with no in-tree WIP, better delegated to deep-low (rigorous, did F-001/F-005). The lane's reset is longer than 96 min this cycle; re-armed a ~2.5h monitor to resume them. No user decision needed; resource wait.


### N3 (2026-10-16) — status note (NOT a decision): lighting COMPLETE; runtime-lifecycle begun; F-009 awaits deep-low reset
The whole lighting/rendering sequence is DONE + committed (clouds M0-M6 + F-001/F-002/F-004/F-005/F-007, 10 commits incl. the F-004 coherence fix). Moved on to the runtime-lifecycle cluster (doc 11, turnkey): F-011 transactional scene-load committed 23c62f5c (audit 17/28) - I verified it in the main session after its subagent died on the usage limit (pure engine: build + scene_loader/lua gtests pass, malformed-load negative controls prove the live scene survives). F-009 (ECS slot/version handles) is NEXT but it is net-new with NO in-tree WIP and the deep-low lane is exhausted again (errored in 2.3s), so it cannot be verified in main like F-011. Re-armed a ~2.5h reset monitor to resume F-009 via the rigorous lane. No user decision needed - resource wait. Pending USER decisions remain D1 (cloud silhouettes) + D2 (F-007 Sponza glass parity sign-off).


### N4 (2026-10-16) — status note (NOT a decision): F-009 done; main-session implementation STOPPED; remaining audit on the lane
The autonomous run delivered 12 verified commits: the WHOLE lighting/rendering sequence (clouds M0-M6 + F-001/F-005/F-004/F-002/F-007) PLUS two runtime-lifecycle findings (F-011 23c62f5c, F-009 db4e7c82). Audit 12->18/28. All pre-commit green; all shipped defaults proven byte-identical.
I STOPPED hand-implementing findings in the main session after F-009. Reason: the remaining audit findings are NOT the clean/low-risk/headless kind I can responsibly do in main while you are away - F-022 (layout contracts) is large and de-dups SHIPPED structs + moves code; F-018 (instancing) is net-new render; F-010/F-020 (owner-thread, IPC/H.264) are editor/N-API and NOT headless-verifiable; 3.1 needs GPU validation; 4.1 is heavy Falcor compute. They belong on the rigorous deep-low lane (which did F-001/F-005/F-002/F-007 well) or need you. The deep-low lane is deeply drained (6+ consecutive usage-limit errors this session; the ~96min and ~2.5h reset monitors both fired into a still-exhausted lane). Armed a ~3h monitor to resume the remaining findings VIA THE LANE when it recovers. No new user decision needed; D1 + D2 remain the only pending decisions.


### D3 (2026-10-16) — F-022 layout gate found 12 pre-existing shader-side drifts (3 real BUGs); decide disposition
The new CB/structured-buffer layout gate (test_cbuffer_layouts.py, committed 01c6f4d3) works (82/85 Tier A, 25/27 B, 3/4 C, inventory 163 decls 0 unmapped, both negative controls reject) and SURFACED 12 pre-existing HLSL-vs-C++ layout discrepancies it will not pass over. They are OUT OF F-022's scope ('install the contract'), so 5.3(a) stays [ ] PARTIAL and the gate is NOT wired into the blocking pre-commit yet. 3 are real BUGs worth a follow-up fix:
1. **Exposure offset (3 VFX PerFrame mirrors)**: billboard VS, mesh VS, mesh PS declare exposure at offset 248 instead of 252 -> those VFX shaders read exposure from the wrong slot. (In your VFX WIP area.)
2. **Particle-mesh root blocks (2)**: a reserved uint[14] array reflects as 228 B / 57 DWORDs under cbuffer 16-B packing vs the 64-B / 16-DWORD CPU struct -> a size/packing mismatch.
3. **VertexPacked**: the shader declares int4 packedNormal/int4 packedTangent (reflected 56 B) vs the CPU int8_t[4] (32 B) -> either a manual-unpack convention (benign) or a real misread; needs a look.
Plus 6 contracts lack a reflectable declaration (empty gpu_cull_cs kernel; InstanceData/LightGPU/ReflectionProbeGPU/SdfObjectGpuData are C++/raw-buffer only; SkinnedVertex is a ByteAddressBuffer contract). DECISION for you: (A) open a follow-up task to FIX the 3 BUGs + reconcile the 6 contracts, then wire the gate into CI; or (B) quarantine the 12 in an explicit KNOWN_DRIFT allowlist (each with a reason) so the gate goes green + CI-ready now while the bugs stay tracked; or (C) accept the gate as a documented diagnostic (current state). I did NOT force the gate green and did NOT touch your VFX shaders.


### D4 (2026-10-17) — clouds aerial perspective: 2 visual-quality gates miss targets (5.4, 8.1)
Aerial perspective shipped (28473ec4, r.Clouds.AerialPerspective default ON; distant clouds fade into the atmosphere via the Bruneton transmittance LUT). Default render byte-identical (no clouds in reflection-reference); 10/12 clouds items closed + big perf path. TWO cloud-scene visual-quality gates miss their numeric targets and were NOT forced:
- **5.4 aerial appearance:** horizon falloff 0.238 vs target <=0.10 (legacy fake haze was 0.154). The warmer horizon look needs either more tuning or a sign-off.
- **8.1 silver linings:** sun/away silver ratio difference -0.048 vs target +0.15. Silver linings (bright sun-side rims) are under target - this ties to the silver-lining / multi-scatter work deferred earlier to M2b, so it likely needs that feature, not just a tune.
DECISION for you: (A) accept the current cloud look + treat 5.4/8.1 as aspirational (close them as accepted); (B) have me tune toward the targets (5.4 is tunable; 8.1 likely needs the deferred M2b silver-lining work); (C) make aerial perspective opt-in (default OFF) until signed off. Clouds is 76/78; 5.4/8.1 stay [ ] pending this.


### D5 (2026-10-17) — 2 phase0 tasks externally blocked; toolchain decision for dx12 5.1/5.4
Two phase0 tasks cannot be finished autonomously without an external unblock (documented in each task's tasks.md; I did NOT fake completion):
- **fix-dx12-first-frame-resource-state-crash (29/31):** the first-frame crash itself is FIXED + GBV-verified via the main engine. The 2 open items 5.1/5.4 are a vfx_viewport_app DEBUG-LAYER REGRESSION CHECK blocked by a Zig toolchain mismatch (finding F-017): the machine PATH has Zig 0.16 but the project pins the vendored 0.15.2; demos/common/build_common.zig is 0.15.2-only, and runtime.lib built with 0.16 libcxx will not link into a 0.15.2 demo (lld-link: undefined symbol std::__1::__hash_memory). DECISION for you: (A) standardize the whole stack on the vendored Zig 0.15.2 (rebuild runtime + demos with it) so the demo links + 5.1/5.4 can run — a build-infra change affecting every build; or (B) accept the crash fix as verified-via-main-GBV and leave 5.1/5.4 as a documented toolchain-gated regression check (the crash fix's value is already delivered). I did NOT change the build toolchain unilaterally.
- **vfx-mesh-bindless-attr-read-bug (10/17):** the core bug (sortedIndices bindless read returns 0 under sort_mode=ViewDepth) needs a RenderDoc/PIX GPU capture, UNAVAILABLE in this headless environment. No decision of yours resolves this here; it needs a machine with RenderDoc/PIX. The guard tests (4.2/4.3) landed; the rest stay blocked + documented.


### D6 (2026-10-17) — pre-existing shared-TLAS translation bug found; blocks path-tracer Cornell gate (4.5) [D6 RESOLVED 56f65e6c]
**RESOLVED (56f65e6c):** confirmed a REAL bug (Mat4 stores translation in _41/_42/_43 per DirectX row-vector convention; DXR INSTANCE_DESC.Transform wants it in the last column) and fixed tlas_manager.cpp to extract _41/_42/_43 + transpose the 3x3 basis. Default raster byte-identical (max 0); off-origin RT consumers IMPROVE (inline PT black cross gone, DDGI correct); GBV clean (527/538/607/942=0); Cornell 4.5 now passes (colour bleed G>R 7.0 / R>G 13.1, one-bounce control [0,0,0]); 2 new CPU gtests nail the DXR column semantics. falcor-path-tracer is COMPLETE 21/21. No open action remains for D6.

While implementing falcor-path-tracer stage-2 (multi-bounce + Cornell captures), the lane found a PRE-EXISTING bug in the shared raytracing TLAS infrastructure: runtime/engine/renderer/raytracing/tlas_manager.cpp:101-107 populates the DXR instance translation from matrix elements _14/_24/_34, while the engine stores translation in _41/_42/_43 (row vs column convention mismatch). Off-origin geometry gets the wrong world translation in the TLAS, so the Cornell box walls OVERLAP and the red/green bleed accuracy gate (item 4.5) cannot be proven. Near-origin scenes (reflection-reference) look fine because the translation error is small there, which is why stage-1/stage-2 verified correctly on that scene.
The lane correctly LEFT THIS UNTOUCHED (it is shared infra used by the inline PT / SDF-GI / anything building a TLAS, not path-tracer-local), so path-tracer stages 1-2 shipped clean + default-byte-identical. BUT this bug likely affects RT correctness for any off-origin instanced geometry engine-wide.
DECISION / PLAN: I intend to investigate + fix this carefully in a FOLLOW-UP (verify the actual matrix convention first - confirm it is a real bug not a misdiagnosis; if real, swap to _41/_42/_43 with cross-scene byte-identical + GBV verification since it is shared), then close Cornell 4.5 + the path-tracer docs tail. If you would rather I leave the shared TLAS alone and accept 4.5 as blocked, say so. Path-tracer is otherwise stage-1+stage-2 complete (3.1-3.4/4.1-4.4 [x]); only 4.5 + docs remain [ ].


### D7 (2026-10-18) — path-tracer BASE BRDF gains energy (white-furnace albedo 1.28 at grazing); pre-existing, affects all PT features [D7 RESOLVED 7f0a7e3f]
**RESOLVED (7f0a7e3f):** NOT a GGX normalization bug (the GGX specular alone is 0.884<1, Smith+VNDF correct) - it was the diffuse/specular COUPLING: the half-vector (1-F) on Lambert diffuse left ~0.94 diffuse + 0.344 specular at grazing. Fixed by weighting diffuse by the complement of the specular directional albedo E_spec(NoV,roughness) (diffuse->0 at the horizon). Base white furnace now <=1 over a 378-case sweep (max 0.99999993; was 1.284; D7 case 0.3/NoV-0.1 1.284->0.9946; not crushed, normal-incidence ~0.995). PT-only: default raster byte-identical (max 0). Layered furnace tests re-baselined to the energy-correct base. gtests 7890. No open action for D7.

While implementing falcor-layered-materials stage-1, the lane ran a CPU white-furnace quadrature on the path tracer's base GGX BRDF (runtime/shaders/runtime/pathtracing/pathtracer_shared.hlsli:128-138) and found it is NOT energy-conserving: directional albedo = **1.284** for a white dielectric at roughness 0.3, N.V=0.1 (grazing), at 0.5M and 2M integration samples (stable). A physically-correct microfacet BRDF must have furnace albedo <= 1 (slightly below for single-scatter GGX); 1.28 means the base GAINS energy at grazing angles - a normalization defect (likely the Smith masking/shadowing G term, the VNDF-sampling Jacobian, or the denominator). This is PRE-EXISTING (predates the layered-materials task) and affects the RENDERED output of EVERY opt-in --path-trace feature already committed (path-tracer, ReSTIR-DI, light-BVH, NRD, volumetric-media) - they all sit on this base, so they are slightly over-bright at grazing. It was not caught earlier because the relative proofs (feature-on vs feature-off) shift both sides equally, and nothing ran a furnace/energy invariant until now.
DECISION / PLAN: I SCOPED the layered-materials stage-1 to RELATIVE energy conservation (the coat adds no energy beyond the base + its Fresnel) and did NOT alter the base inside that task (changing the base would silently change 5 committed features' output). The base-BRDF energy fix deserves its OWN scoped task: fix the GGX normalization so furnace albedo <= 1, then RE-VERIFY all PT features against fresh references (their relative proofs still hold, but the absolute look changes slightly). For you: (A) I open+do a dedicated base-BRDF-energy-fix task next (root-cause, improves all PT correctness; default raster is unaffected since the base BRDF is in pathtracer_shared.hlsli = PT-only); or (B) leave it as a documented known-limitation (the grazing over-brightness is subtle) and keep shipping stage-1 features. Only --path-trace output is affected; the default raster render is byte-identical regardless.
