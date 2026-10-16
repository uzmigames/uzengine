<!-- doc-meta -->
title: Known Issues & Partial Implementations
date: 2026-04-07
status: living-document
<!-- end-doc-meta -->

# Known Issues & Partial Implementations

Living registry of systems/features that are **partially implemented**,
**disabled by default**, or **known to have bugs**. Everything listed here
has either been worked on and paused, or was added speculatively and not
integrated. The goal is to prevent rediscovery cost — before touching any
of these, read this doc and the linked code locations.

**Ordering**: user-visible impact first, then size of remaining work.
**Status legend**:
- `OFF` — implemented code path exists, disabled via CVar (documented below)
- `PARTIAL` — builds and runs but known incorrect / incomplete
- `STUB` — scaffolding only, never integrated / tested
- `BUG` — enabled but produces wrong output
- `LANDED` — implemented, enabled (or opt-in) and verified

---

## Capability matrix

Single source of truth for what the renderer actually supports, versus what is
partial, stub or a known bug. Established 2026-10-15 (audit item 5.3, F-022 part b) and
reconciled 2026-10-16 against the landed stabilization commits (F-001 42b64396, F-005 e782f6b7,
F-004 6c1fcbe3 + 46858197, F-002 5834d10f, F-007 ed5521c0, F-011 23c62f5c, F-009 db4e7c82)
and the cloud rewrite M0-M6. Design: [13-remaining-stabilization-design.md, F-022](project-stabilization/13-remaining-stabilization-design.md).

Rules: `Validation evidence` names a revision plus the test or script that proves the row;
`untested` is a legal value, blank is not. `Integration point` is the file:line where the
behaviour is decided, checked against the tree on 2026-10-16. The per-feature sections below
are the notes behind each row. Every rendering task updates its row in the same change
(rows flip from "designed" to "LANDED @ sha").

| Feature | Status | Backend | Integration point (file:line) | Validation evidence (revision+test) | Known limits | Owner task |
|---|---|---|---|---|---|---|
| TAA | OFF by default (`r.TAA`); implemented | DX12 SM6.6 | frame_setup.cpp:48 | 42b64396: `test_temporal_history_state.cpp` 17/17, frame-95 reflection reference byte-identical; flicker-programme region dumps | history validity now keyed on frame continuity, size, camera cut, upscaler and scene change (F-001); WPO motion still missing (F-002 stage 2) | audit 3.2 (F-001 landed) |
| TSR | STUB, declared unavailable | DX12 SM6.6 | tsr_upscaler.cpp:28 (`kTsrImplemented = false`) | e782f6b7: `test_upscaler_selection.cpp` 20/20 | `tsr_resolve_cs.hlsl` empty since bd4db199; selecting it falls back to TAA, then None | audit 3.3 (F-005 landed) |
| FSR2 | STUB without SDK; SDK path untested | DX12 | fsr2_upscaler.cpp:17 (`UZ_FSR2_ENABLED` default 0); :568 stub `IsAvailable` false, :313 SDK `IsAvailable` true | e782f6b7: `test_upscaler_selection.cpp` FSR2 stub guard 20/20; SDK path untested | stub reports unavailable and never becomes the display source | phase2_fsr2-full-integration |
| DLSS | PARTIAL (SDK builds) | DX12, NVIDIA | dlss_upscaler.cpp:462 (`IsAvailable`, SDK build), :700 (stub, false) | 0f2c2de4: path-agnostic discovery (F-021); e782f6b7: `test_upscaler_selection.cpp` DLSS stub guard 20/20; SDK upscale untested in this programme | RR and FG not integrated; vendor motion-vector scale sign deferred (5834d10f) | audit 3.4 |
| XeSS | not implemented | none | temporal_upscaler.cpp:38-39 (`UpscalerType::XeSS` returns nullptr) | e782f6b7: `test_upscaler_selection.cpp` 20/20 | none to ship | audit 3.3 |
| Upscaler selection and display source | LANDED | DX12 | scene_upscaler.cpp:19 (`SetUpscalerType`); frame_setup.cpp:56 (`r.Upscaler.FailoverFrames`, default 3) | e782f6b7: `test_upscaler_selection.cpp` (UpscalerSelection, DisplaySource) 20/20; forced-failure run failed over to None | CVar keeps the requested value, `GetUpscalerType` returns the effective one | audit 3.3 (F-005) |
| Object velocity | PARTIAL: rigid and skinned LANDED (stage 1); WPO missing | DX12 SM6.6 | velocity_pass.cpp:28 (`AddVelocityObjectPass`), called at frame_geometry.cpp:438 | 5834d10f: `test_velocity_pass.cpp` 15/15 (8 DX12 RG16F readbacks), translating-box and skinned smokes | WPO is stage 2; object pass runs only when `ctx.temporal.matrixValid`; particle vectors now in render pixels | audit 3.4 (F-002 stage 1 landed) |
| Material-graph context | LANDED (real inputs) | DX12 SM6.6; SM5 vertex colour white | mesh_vs.hlsl:20 (`ConstantBuffer<PushConstants> push : register(b0)`, {vb, packed, color, prevVb}); pushed at gbuffer_pass.cpp:145 (instanced) and :263 (per-draw) | 46858197 (completes 6c1fcbe3): `test_gbuffer.cpp` MaterialContextDraw, `test_gpu_mesh.cpp` GPUMeshColorUpload (GPU-gated, passed in the 7742 suite) | 6c1fcbe3 alone is incoherent (shader reads 4 constants, C++ pushed 2); use 46858197 or later | audit 3.5 (F-004 landed) |
| Glass (shading model 9) | PARTIAL: thin-slab transmission LANDED | DX12 SM6.6 | translucent_ps.hlsl:188 (`refract(-V, N, 1/ior)`) | ed5521c0: `test_glass_contract.cpp` 8/8 (7 on DX12), checker scene shows the wall through the pane | thin-slab only, front faces; no volume, no other-translucent refraction, no roughness blur, no thickness maps; reuses `heightmapSrvIndex`/`displacementScale` for model 9; audit 3.6 stays open on Sponza panes | audit 3.6 (F-007) |
| POM | OFF on the binder path | DX12 SM6.6 | constant_buffers.h:413 (POM defaults); gbuffer_per_object_params.h:82 (`GBufferPerObjectParams : PerObjectCB`) | working tree, uncommitted F-022(a) de-dup: ReleaseFast build exit 0, reflection-reference frame 95 byte-identical to the F-005 baseline (capture `E:/UzmiEngine/f022-after-1792160613122` vs `E:/UzmiEngine/f005-after/frames/f_95.png`, max code delta 0, 0 changed channels, 1920x1080) | the binder uploads the PerObjectCB defaults 8 / 32 / 1000 / 2000 instead of zeros at 224-251, but `displacementScale` stays 0, so POM stays off | audit 5.3 (F-022 a) |
| CB / structured-buffer layout contracts | LANDED / GREEN (5.3a, D3 option A) | DX12 SM6.6 | scripts/rendering/test_cbuffer_layouts.py:215; constant_buffers.h:53; mesh_vs.hlsl:16; package.json:21; .githooks/pre-push:4 | 2026-10-16, uncommitted: `npm run test:layouts` invokes Python and exits 0: Tier A 85/85, B 27/27, C 4/4; all five negative controls reject; inventory 163 declarations = 102 mapped + 61 reasoned exclusions, zero unmapped; ReleaseFast build and real render exit 0, no shader compile failures; `E:/UzmiEngine/f022-d3a-after/frames/f_95.png` is byte-identical to F-005 (max_code_delta 0, 1920x1080 RGB) | Fixed missing skyLightMinAmbient before exposure in billboard VS and mesh VS/PS (252 B); packed root padding now uint2 + uint4[3] (64 B); VertexPacked now two uint words with signed-byte decode (32 B), not int4. Six non-reflectable contracts remain explicitly UNTRACKED-with-reason in `UNTRACKED_CONTRACTS` (empty/unimplemented, legacy CPU-only, or raw-byte contracts), not fabricated passing reflections. Versioned pre-push runs `npm run prepush`, with layout gate first and failure propagated; install with `npm run hooks:install` (installed here, other hooks preserved) | audit 5.3a GREEN; F-022 c remains separately deferred |
| Mesh decals | LANDED | DX12 | gbuffer_pass.cpp:218 (`materialFlags & 0x10`, bit 4, drawn in the decal phase) | 513e2983: `test_mesh_decals.py` 5/5 | albedo blend by opacity only; skipped in depth-only mode (gbuffer_pass.cpp:222) | lighting side item |
| GPU instancing | OFF; instanced VS unimplemented | DX12 | frame_sync.cpp:45 (`ctx.useInstancing = false`) | no `UZ_INSTANCED` VS exists in runtime/shaders | wrong push constants and mis-keyed batcher until F-018 stage 2 | audit 5.1 (F-018) |
| GPU occlusion culling (`r.GpuCull`) | STUB: no implemented kernel, no consumer | DX12 SM6.6 | scene_renderer_init.cpp:1106; frame_geometry.cpp:114; gpu_cull_cs.hlsl (empty kernel, no `CullConstants` cbuffer) | untested | the CPU dispatch path exists but the kernel does nothing and nothing reads its buffers | audit 5.1 (F-018 stage 1) |
| Depth prepass (`r.DepthPrepass`) | OFF; per-draw only | DX12 | frame_setup.cpp:63 | 5470d37f: `test_depth_prepass.py` 5/5 | depth-only GBuffer mode + Equal; mutually exclusive with instancing | lighting B.4c |
| Post-process chain | PARTIAL | DX12 | frame_postprocess.cpp:260 (TAA + Bloom + Tonemap pipeline), :492 (Bloom), :49 (`r.Tonemap.ChromaAbScale` marked inert) | e782f6b7 display source, `test_upscaler_selection.cpp` DisplaySource; chain audit in [postprocess-chain-status](post-process/postprocess-chain-status.md) | active: Bloom, Tonemap (+ TAA, SSR composite and copy); inert: CAS, FXAA, motion blur, DOF, lens flare, LUT, chromatic aberration | audit 3.8 (F-008) |
| Path tracer | PARTIAL (diagnostic) | DX12 DXR | frame_lighting.cpp:71 (`r.PathTracer.Enable`, default off), :87 (replaces the lighting) | e933550a (3.7 closed); doc 10, F-006; no automated test | not a parity oracle | none |
| Shadow cache static/movable | LANDED (per-draw loop) | DX12 | frame_shadows.cpp:55 (pass 2: movable geometry into the dynamic atlas), :85 (`ShadowCacheMode::Uncached` fallback) | untested in this programme | the instanced loop (F-018 stage 2) must filter per batch | audit 5.1 |
| SM5 / non-bindless | PARTIAL | DX12 SM5 | frame_setup.cpp:74 (`ctx.bindless`) | untested | TAA off, instancing n/a, vertex colour white (F-004) | none |
| Vulkan, Metal | untested in this programme | Vulkan 1.3, Metal | vulkan_command_list.cpp:751 (`DrawIndexed`); metal_command_list.cpp:618 (`DrawIndexed`) | untested | sources exist, nothing verified here | none |
| ECS handle validity | LANDED | n/a | world.h:272 (`SlotOf`), world.cpp:18 | db4e7c82: `test_world.cpp` 6 new tests incl. SlotChurn and VersionWrap control, `test_lua_scene_api.cpp` | generation table keyed by slot, 4 B/slot; ABA needs 2^32-1 reuses of one slot | audit 2.1 (F-009) |
| Owner-thread world mutation | BUG (unsynchronized) | n/a | render_thread.cpp, napi_*.cpp | untested; F-010 design (doc 11) | Node thread mutates RuntimeState while the render thread reads; shutdown use-after-free | audit 2.2 |
| Transactional scene load | LANDED (validate, then commit) | n/a | scene_loader.cpp:1119 (`LoadSceneEx`), called at runtime.cpp:863 | 23c62f5c: `test_scene_loader.cpp` malformed-JSON and missing-entities negative controls, `test_lua_scene_io_api.cpp` | legacy `LoadScene` wrapper still returns 0 for failure and for an empty scene | audit 2.3 (F-011) |
| IPC / H.264 streaming | BUG + PARTIAL | n/a | render_thread.cpp, runtime-client.ts, webview | untested; F-020 design (doc 11) | unbounded queues, no deadlines; the webview never converts base64 to bytes, so H.264 never decodes | audit 2.5 |
| Lighting B.1 per-pass GPU timings | LANDED | DX12 | render_graph_builder.h:184 (per-pass GPU timestamp profiling in `Execute`) | 088b8f40: `test_diag_timing.py` 7/7 | `--diag` only | lighting block B |
| Lighting B.2 incremental global distance field | LANDED | DX12 SM6.6 | frame_gi_passes.cpp:71 (`r.GI.GDF.RegionalRetrace`) | ce2caa5e, 0a9cc732, ec24a4ce: `test_gdf_incremental.py` (see the index for per-letter results) | regional recompose; history kept outside change boxes | lighting block B |
| Lighting B.3 async disk-cached mesh SDF bake | LANDED | DX12 | mesh_sdf_bake_queue.h / mesh_sdf_bake_queue.cpp (bake queue, from :1) | 8c4d14b6: `test_sdf_bake_cache.py` 7/11 (C1 138/138 identical) | C2/C4/C5 documented as measurement letters | lighting block B |
| Lighting B.4 froxel fog | LANDED | DX12 SM6.6 | frame_atmosphere_passes.cpp:31 (`r.VolumetricFog`, default on), :178 | 8851f37e: `test_froxel_fog.py` 12/12, `test_cbuffer_layouts.py` | sun once, shadowed | lighting block B |
| Lighting B.4b contact shadows | LANDED | DX12 SM6.6 | frame_constants.cpp:60 (`r.ContactShadow.Length`); deferred_lighting_ps.hlsl:619 (`ComputeContactShadow`) | 8aeb8917: `test_contact_shadows.py` 9/9 | reversed-Z linearisation | lighting block B |
| Lighting B.5 layered SSR | LANDED, `r.SSR` default OFF | DX12 SM6.6 | frame_lighting.cpp:50 (`r.SSR`, default false); frame_gi_passes.cpp:31 (SSR per-pass CVars) | 9f1be63f: `test_layered_reflections.py` 7/7 | off by default; frame_lighting.cpp:49 notes the resolved-history pass produces zero | lighting block B |
| Lighting B.6 DDGI dedup | LANDED | DX12 DXR | frame_gi_passes.cpp:276 (radiance cache trace and DDGI probe update) | edfd7942: `test_ddgi_dedup.py` 6/6 (+ `test_gi_energy_gpu.ps1`) | needs `r.RayTracing.GI` and a TLAS | lighting block B |
| Lighting B.7 clustered lights | LANDED | DX12 SM6.6 | frame_constants.cpp:37 (`r.Lights.Clustered`, default on); frame_lighting_pass.cpp:78 | 58340259, 794b632f: `test_clustered_lights.py` 8/9 | 512 / 128 light limits; spot cone added in a side item | lighting block B |
| Lighting side items: env-MIS specular, sky capture per-channel sun, Sponza recalibration | LANDED | DX12 SM6.6 | deferred_lighting_ps.hlsl:1383 (env-MIS SRV); sky capture sun colour atmosphere_renderer.cpp:151, :164 (`skyComp.sunIlluminance`, per channel) | B.5b ea13ab21: `test_env_mis.py` 8/9; B.5c b8ff5077: `test_sky_capture.py` 5/5; 4501fa74: `test_sponza_recalibration.py` | env-MIS E1 read the old sky value, fixed in B.5c; details in the [block-B index](lighting/uzengine/sponza-lighting-2026-10/lighting-block-b-2026-10.md) | lighting side items |
| Brixelizer GI (`r.BrixelizerGI`) | LANDED (opt-in, default OFF) | DX12 | frame_gi_passes.cpp:394 | enabled 2026-10-14 (A/B 6.90 dB, +1.26 ms) | needs `uz_brixelizer.dll` and the FFX SDK libs at build | none |
| Volumetric clouds | LANDED (M0-M6) | DX12 SM6.6 | cloud_raymarch_cs.hlsl:506 via cloud_layers.hlsli:76 (`MarchCloudLayers`) | M1 b5232d3d, M2 8c50c1c0, M3 a1f16cd8, M4 25ce13b0, M1 fix 8efdbf4d, M5 597477f4, M4b c6027060, M6 849e5fef (`test_cbuffer_layouts.py` CloudCB 1024 B, default-unchanged pairs within one code value) | opt-in caveats recorded in 849e5fef; ground shadows gated, default-safe | phase0 clouds-rewrite |

The March 2026 [uzengine-audit.md](rendering-arch/overview/uzengine-audit.md) "production-ready" claim
is historical and is not evidence for the current tree; this matrix supersedes it.

---

## Anti-Aliasing / Temporal / Upscalers

### `r.TAA` — `OFF` by default (the trembling below matches the jitter defect fixed 2026-09-08)
**File**: [runtime/engine/renderer/frame/frame_setup.cpp:47](../../runtime/engine/renderer/frame/frame_setup.cpp)
**Default**: `false`

TAA causes visible mesh trembling / vertex wobble. Multiple tuning sessions
on jitter sequences, clamping, and history rejection did NOT resolve it.
Suspected root cause: motion vector generation or history reprojection bug
introduced by an unrelated change. Needs a ground-up audit.

**Workaround**: leave disabled. FXAA is on by default for edge smoothing.

**Memory reference**: MEMORY.md documents the bug as
`project_taa_jitter_trembling_bug.md` (2026-04-01), attributed to commit
`24c4d801` (decals).

**Correction (2026-10-08, closure of `phase0_sponza-followup-06`).** The trembling described above
matches the defect fixed on 2026-09-08 by `phase0_sponza-followup-07`: the jitter was applied to the
view-projection as if it were the projection ("TAA jitter applied to the view-projection as if it
were the projection — `FIXED`", under Global Illumination). The comment above the CVar
(`frame_setup.cpp:46`, "TAA disabled (jitter trembling bug). No anti-aliasing until fixed.")
predates that fix. The FXAA named in the workaround is inert: its pass was removed with the dead post
chain (`phase0_sponza-followup-08` 4.4), and `r.FXAA.EdgeThreshold` is registered as "pass not wired"
(`frame_postprocess.cpp:45`). The default is still `false`, and no quality preset applies at startup
unless `quality` is set (`scene_renderer_init.cpp:1027-1036`). With TAA off, the stochastic GI passes
(`r.GI.Reflections`, `r.GI.FinalGather`, GTAO) show per-pixel noise, including white specks beside
openings ("Indirect-path noise", under DDGI), so a project that enables them sets `r.TAA 1`, as
`sample/projects/sponza.uzproject` does. The default itself is unchanged, and no task tracks
changing it.

---

### `r.TSR` — `STUB` (corrected 2026-10-08; was listed as `PARTIAL`)
**File**: [runtime/engine/renderer/passes/postprocess/tsr_upscaler.cpp](../../runtime/engine/renderer/passes/postprocess/tsr_upscaler.cpp)

The three passes (dilate-velocity, reject-shading, update-history) are all
compiled from `tsr_resolve_cs.hlsl` (`pipeline_temporal.cpp:79`, `:115`, `:151`),
and that file has been a one-line empty compute body since `bd4db199`
(2026-03-31). Selecting TSR (`UpscalerType::TSR`: Lua
`uz.renderer.set_upscaler("tsr")`, `lua_renderer_api.cpp:368`, and the fallback
for `XeSS`, `temporal_upscaler.cpp:28` and `:42`) therefore writes no output.
The earlier text ("implemented with 3 passes ... known to produce artifacts")
was written on 2026-04-07 (`192ef554`), after that commit, and did not reflect
it. Open; no task re-implements TSR: owner `<stub-shader task>` (proposed by the
`phase0_sponza-parity-06-qa-harness-validation` closure review). See "One-line
stub shaders".

---

### `r.DLSS.Quality` — `PARTIAL`
**Files**: [dlss_upscaler.cpp](../../runtime/engine/renderer/passes/postprocess/dlss_upscaler.cpp),
`extension/native` (NGX wrapper)
**Status**: upscale path works; Ray Reconstruction + DLSS-G (frame generation)
are NOT integrated.

Base DLSS 2/3 upscale via NGX ("NVSDK_NGX_D3D12_CreateFeature") is functional
after the 2026-03-25 fix (raw vtable access + exposure default 10.0). Remaining gaps:
- **Ray Reconstruction (DLSS 3.5)** not integrated
- **DLSS-G / DLSS Multi Frame Gen** not integrated
- **Motion vector coverage**: some passes don't feed motion vectors, so
  DLSS occasionally ghost-smears — same root as the TAA bug
- **No runtime fallback**: if NGX fails, the pass silently returns low-res

---

### `r.FSR2` — `OFF`
**File**: [fsr2_upscaler.cpp:76](../../runtime/engine/renderer/passes/postprocess/fsr2_upscaler.cpp)

FSR 2.x temporal upscaler is scaffolded but never enabled by default. Build
path compiles, integration untested at runtime.

### `r.FSR` — `STUB` (corrected 2026-10-08; was listed as `OFF`, "works")
**File**: [fsr_pass.cpp:24](../../runtime/engine/renderer/passes/postprocess/fsr_pass.cpp)

FSR 1.0 spatial upscaler (EASU + RCAS). `fsr_easu_cs.hlsl` and
`fsr_rcas_cs.hlsl` are one-line empty compute bodies since `bd4db199`
(2026-03-31), and `FSRPass` is used only by its own files and
`test_fsr_pass.cpp`: it is neither wired nor able to produce an image. Owner:
`<stub-shader task>` (no open task re-implements FSR 1.0). See "One-line stub
shaders".

---

### `r.SMAA` — `STUB` (corrected 2026-10-08; was listed as `OFF`)
**File**: [smaa_pass.cpp:33](../../runtime/engine/renderer/passes/postprocess/smaa_pass.cpp)

`smaa_edge_ps.hlsl`, `smaa_blend_ps.hlsl` and `smaa_resolve_ps.hlsl` compile,
but each is a one-line pixel shader returning opaque black since `bd4db199`
(2026-03-31), and `SMAAPass` is used only by its own files and
`test_smaa_pass.cpp`. Owner: `<stub-shader task>` (no open task re-implements
SMAA). See "One-line stub shaders".

---

### Static TAA residual — `MODELLED (2026-09-10), STILL OPEN`
The static flicker's *amplitude* is described by the TAA accumulation's steady-state ripple: the
accumulation is a first-order IIR filter (`r.TAA.CurrentFrameWeight` = 0.04) whose input is
periodic in the jitter cycle, and such a filter settles into a ripple rather than converging.
Predicted from the measured input ripple and the blend weight, with no fitted parameters, it lands
within 1.5x at jitter periods 4, 8 and 16 and reproduces the ordering; with the sub-pixel offset
removed the residual falls four orders of magnitude. Ledger H-27, evidence in
`.screenshots/flicker-elimination/15-isolate-clamp-bounds/`.

This is an **approximate amplitude model, not a verdict on the image.** The shader's accumulator is
not a constant-coefficient IIR (its effective weight ranges 0.039–0.300), the prediction applies the
fundamental's gain to the whole variance, and neither phase nor per-pixel time series were
reproduced. It bounds the remedies without declaring the visible behaviour acceptable — that
judgement has not been made.

**Open, owner: `phase0_sponza-followup-15-taa-window-residual`** (proposed at the followup-10 and
followup-14 close-outs, 2026-10-08). No open task item owned the window's residual before it.
followup-14's closure notes list H-25b's untested bounds variants and H-26 as "work that belongs to
follow-ups rather than here". Untested on the window: clamp bounds averaged over a jitter cycle or
filtered slowly (H-25b scope), an exact history read substituted during accumulation (H-26), and the
motion behaviour of two followup-10 variants that never went through the cut sequence:
`r.TAA.ReconstructionMode=2` (centre tap only), which cut the static residual 53 % but raised it
75 % on the boundary pixels, and `ReconstructionMode=1` with `BlendMode=1` (-50 %), each measured in
one 24-frame run. The same task owns the judgement, not yet made, of whether the modelled residual
is acceptable.

Consequences for anyone tuning TAA here:

* the residual is `input_ripple x |H|`, so reducing it means making the reconstruction less
  jitter-dependent (measured, caps at −35 %, and it bleeds light into black pixels), lowering the
  blend weight (lag, which the motion gate rejects), or shortening the jitter cycle (less
  anti-aliasing);
* the clamp *bounds* are rejected in steady state on a static camera (H-25b): freezing them moves
  the oscillation by −3.4 %, below this ROI's decision threshold, while the error against the
  correctly resampled image grows 14.3 %. Averaging the bounds over a jitter cycle, or filtering
  them slowly, are different interventions and were not tested;
* the history fetch is **not** eliminated (H-26): its immediate perturbation is small, but no run
  substituted an exact history read while accumulating, so an effect that builds up over frames
  remains possible;
* no pre-registered gate judges a window candidate against a reference: followup-14's G1 measured
  stability (`std_mean`, which a correct reconstruction can raise), and followup-12's acceptance
  criteria of 2026-09-17 (its notes.md, "Acceptance criteria for any candidate correction") cover
  the pose A rear-door glass (with two walls as controls), the walls of pose W in motion and the
  teleport, not the W `window`. Open, owner: `phase0_sponza-followup-15-taa-window-residual`;
* `r.TAA.JitterScale=0` is a *control*, not a fix: it removes the residual by removing the
  anti-aliasing.

The shadowed walls (open, owner: `phase0_sponza-flicker-correction`, proposed 2026-10-08; their
state is in the Sponza flicker entry under Global Illumination) and the behaviour in motion are
outside all of this and remain open.

### TAA reconstruction HDR weighting - `MEASURED, NOT CHANGED (2026-09-09)`
The plus filter in `taa_resolve_cs.hlsl` weights each tap by `1 / (Y * exposure + 4)`
(Karis 2014). It is a demonstrated source of part of the static flicker on bright features
(ledger H-11, H-12): it makes the filtered colour depend on where the jitter put the samples,
and it loses energy on thin bright features.

`r.TAA.ReconstructionHdrWeightExponent` (default 1.0 = production, 0 = no weighting) was added
to study it and is **opt-in**: it failed four of the seven preregistered acceptance gates, so
the default was not changed. Measured at exponent 0, pose W window, 3 repeats:

* static residual -35.4 % (the gate asked for -50 %; the whole exponent range was swept
  and this is the ceiling);
* 3x closer to the correctly resampled reference, with the bright side's energy deficit nearly
  eliminated - the gain is real, and it is not blur;
* but recovery after a camera cut goes 16 -> 60 frames, and a dark
  ROI containing a small bright feature (pose A `rear_door`) gets
  +120 % residual and
  +79 % energy against the baseline. Against the cycle-averaged reference most of that energy is
  restored, not added (bright side 42 % -> 74 % of correct, per-pixel error halved; ledger "Two
  findings about the measurement itself"), so on that ROI the cost is stability, not accuracy;
* pixels whose correct value is ~0 receive about 6x more light.

The weighting is a variance/bleed trade, not a defect to delete. The reversible-tonemap
reconstruction once named here as the next experiment was rejected on paper before any capture
(ledger H-24: it is the same `1/(k + Y)` weighting with k = 1, more aggressive than production), and
H-24b bounds every member of that family by the exponent-0 endpoint, so no reconstruction-weighting
experiment is pending; what was run after it, and what is still untested, is under "Static TAA
residual" above. `r.TAA.ClampMode=3` (AABB only) is rejected as an alternative as well.
Independently of motion, it lowers the static window residual only 9.5 % against the 50 % gate G1
asks for (followup-14 `matrix.json`, matching followup-10's independent run). On its own it also
moves recovery after a cut from 16 to 48 frames against G6's limit of 21, a motion figure measured
once and never repeated (`motion.json`, 2026-09-09; ledger H-23b, H-23d). The energy bias itself is
unchanged in production (91.9 % of the correctly resampled input over the window rectangle; a
1.5-px bright bar loses 48.4 %): **open, owner: `phase0_sponza-followup-15-taa-window-residual`**
(proposed at the followup-10 and followup-14 close-outs, 2026-10-08).

### Stale unit tests - `PRE-EXISTING, DIAGNOSED (2026-09-09)`; both `FIXED (2026-10-08, 8246ab5c)`; `ShaderCrossBackend` failures `OPEN`
Two tests in `runtime/tests/` fail against committed production values and are unrelated to the
flicker work. Diagnosed rather than filtered out:

* `BuildPerFrameCB_StructSize.PerFrameCB_Is768Bytes` expects `sizeof(PerFrameCB) == 768`. Commit
  `29c83ba5` grew the struct to 1024 bytes (4 x 256) and
  `runtime/engine/renderer/core/params/constant_buffers.h` carries its own
  `static_assert(sizeof(PerFrameCB) == 1024)`, with fields in use out to offset 1008. The
  invariant that matters - D3D12's 256-byte constant-buffer alignment - is already enforced
  there; the test hardcodes the old size. Suggested fix: assert `sizeof(PerFrameCB) % 256 == 0`.
* `PerFrameCB_IBL.PrefilteredMipLevelsDefaultIsFive` expects 5. Commit `6985ffac` ("IBL rough
  metal excessive reflections") deliberately raised the prefiltered chain to 8 in
  `IBLBaker::kPrefilteredMipLevels` and in all three CB structs, which agree with each other.
  Suggested fix: expect `IBLBaker::kPrefilteredMipLevels` instead of a literal.

Neither test was changed here: they belong to the IBL and constant-buffer areas, not to this
programme.

**Fixed 2026-10-08 (`8246ab5c`).** followup-09, -10, -12 and -13 had routed the fix to followup-14
item 1.6, which only diagnosed both tests; both still failed in the full run of 2026-10-08 (12:45
local). `BuildPerFrameCB_StructSize.PerFrameCB_Is768Bytes` was removed as a duplicate rather than
changed to the `% 256` check suggested above: the 1024-byte size is already pinned by the
`static_assert` at `constant_buffers.h:321` and by `test_constant_buffers.cpp:19`,
`test_ibl_atmosphere.cpp:668` and `test_material.cpp:293`.
`PerFrameCB_IBL.PrefilteredMipLevelsDefaultIsFive` became `PrefilteredMipLevelsDefaultMatchesTheBaker`:
it compares the low byte of `prefilteredMipLevels` with `IBLBaker::kPrefilteredMipLevels` (8) and
checks that the PMREM flag (bit 8) is clear by default. The commit records
`test_runtime --gtest_filter=PerFrameCB_IBL.*:BuildPerFrameCB*:ConstantBuffers*:SHProjection.*`
at 47/47. The tasks that had handed these two tests to one another (followup-09, -10, -12, -13
and -14) owe nothing further on them.

**Same category, still open, and owned by no task yet** (`phase0_test-suite-preexisting-failures`
is proposed for it with the followup-09 closing): 13 of the 21
`ShaderCrossBackend/ShaderCrossBackendTest.Compile/*` cases fail (full runs of 2026-09-16 and
2026-10-08). Twelve report "Shader source not found on disk": the fixture's subdirectory list
(`runtime/tests/engine/shader/test_shader_cross_backend.cpp:56-57`) has no `gbuffer/`, where `mesh_vs`,
`mesh_ps`, `gbuffer_ps` and `gbuffer_debug_ps` have lived since bd4db199. The thirteenth,
`SPIRV_SM60_deferred_lighting_ps`, is a real compile error of the non-bindless (SPIR-V SM 6.0) path:
`shadow.hlsli` uses `shadowAtlasTexture` (lines 156 and 251), which no non-bindless block of
`deferred_lighting_ps.hlsl` declares, and `deferred_lighting_ps.hlsl:1381` reads `gBufIndices` outside
`#ifdef UZ_BINDLESS` (since 1d2e97da). Suggested fixes: add `"gbuffer/"` to the fixture's list; declare
`Texture2D<float> shadowAtlasTexture : register(t5);` in the non-bindless block (lines 50-58) and keep
the line-1381 test inside `#ifdef UZ_BINDLESS`. Neither change alters the DX12 (bindless) code path.

## Global Illumination

### `r.SSGI` — `OFF` (BUG)
**File**: [ssgi_renderer.cpp:22](../../runtime/engine/renderer/gi/ssgi_renderer.cpp)
**Shaders**: [shaders/runtime/ssgi/*.hlsl](../../runtime/shaders/runtime/ssgi/)

SSGI was scaffolded (trace, spatial denoise, temporal accum, upsample) but
never functionally validated. The `ssgi_trace_cs.hlsl` has a hardcoded
`expectedLinearDepth = lerp(linearRayDepth, linearRayDepth + 5000.0 * t, t)`
with no physical basis — 5km depth assumption in a cm-unit engine. Result:
screen-space rays never register hits on architectural-scale scenes,
`ssgi_ao` writes constant 1.0, `ssgi_result` stays black. Thickness CVar
bumped to 50 cm in the recent session but that's a band-aid.

**Recommendation**: rewrite the march with proper view-space hit-distance
accumulation, or retire it. The world-space SDF final gather
(`r.GI.FinalGather`) is the diffuse GI that runs on Sponza, and the legacy
screen-space SSGI is only added while that gather is off
(`deferred_lighting_ps.hlsl:1277`). Brixelizer GI is not an option: it has
not run since 2026-04-07 (entry below).

---

### `r.BrixelizerGI` — `OFF` by default, functional opt-in on RTX 5080 (verified 2026-10-14)
**File**: [brixelizer_gi_pass.cpp:50](../../runtime/engine/renderer/passes/gi/brixelizer_gi_pass.cpp)
**Task**: `.rulebook/tasks/phase0_brixelizer-gi-integration/tasks.md` (reconciliation note: which ticks hold, defects found)

AMD FidelityFX Brixelizer GI now builds, loads its wrapper, creates both real contexts, and
contributes diffuse GI in Sponza when opted in. Final 1080p A/B PSNR is 6.904351404 dB;
measured RTX 5080 frame overhead is 1.259197 ms. See
[enablement evidence](brixelizer-gi-integration/01-enablement-2026-10-14.md) for commands,
SRV/target proof, all 12 passing Brixelizer tests, and the remaining numerical-coverage gap.

**Wrong when written (2026-04-07).** This entry read "`OFF` → `ON (via scene CVars)`" and
"Implementation is **functional** — produces diffuse GI visible in Sponza", and listed three gaps:
specular output that saturated the pass dump (HDR > 10, "Reinhard clamps to white"), cascades
(2 cm × 4) not profiled beyond Sponza, and a per-frame "delete-all + re-create-all"
re-registration. `29c83ba5` had disabled the dispatch at 00:53 that day, before this registry was
created (`192ef554`, 23:12). On that day's machine the pass was still compiled and initialised: the
session dumped its `brixelizer.*` targets (`eb839ed9`), which register only for an initialised pass
(`frame_lighting.cpp:348-359`). Nothing dispatched it, though, so nothing Brixelizer wrote was on
screen or in a dump after that commit, which predates the saturated dump; no committed revision ever
dispatched it. The gaps described code that was not running, and the delete-all + re-create-all
path had already been removed by `29c83ba5`.

**Historical state verified 2026-10-08 (superseded by the enablement above):**
- **Not compiled.** `runtime/build.zig:903-912` defines `UZ_BRIXELIZER_ENABLED` (`:929-931`) only
  when the MSVC-built `runtime/thirdparty/amd-fidelityfx-1.1/wrapper/build/bin/uz_brixelizer.dll`
  exists. Nothing in the repo produces it (`scripts/setup.ps1:543-549` only clones SDK v1.1.4), so
  the build compiles the no-op stub and registers none of the 15 `r.BrixelizerGI*` CVars.
- **Never constructed.** Construction is commented out at `scene_renderer_init.cpp:666-674` since
  `c84a8bce` (2026-04-10); its comment records `ffxBrixelizerContextCreate` failing with
  `FFX_ERROR_BACKEND_API_ERROR` (0x8000000D) on non-AMD GPUs. The cause was never diagnosed.
- **Never dispatched.** `RegisterBrixelizerGIPasses` returns at its first statement
  (`frame_gi_passes.cpp:387`) since `29c83ba5` (2026-04-07 00:53).
- **Composite never taken.** `frame_constants.cpp:868-872` publishes the sentinel SRV unless a pass
  is initialised, so the branch at `deferred_lighting_ps.hlsl:1306-1324` never runs. It would
  replace `diffuseAmbient` (`:1321`) rather than add to it, and it reads no specular.
- The engine has no `ffxBrixelizerDeleteInstances` call site: `29c83ba5` removed the only one, and
  the wrapper only re-exports the function (`wrapper/uz_brixelizer_dll.cpp:112-116`, resolved by
  `brixelizer_loader.cpp:118`).

The default GI remains the SDF radiance cache with final gather and SDF reflections (next entry),
with DDGI as the DXR option. Brixelizer is opt-in; its default was not changed.

---

### `r.RadianceCache` — `OFF` by default, `ON` for Sponza (project CVar) — `FUNCTIONAL` since 2026-09-08
**File**: [radiance_cache.cpp](../../runtime/engine/renderer/gi/radiance_cache.cpp)
**Shader**: [radiance_cache_trace_cs.hlsl](../../runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl)
**Docs**: [33-sdf-gi-final-gather.md](../engine/12-rendering/33-sdf-gi-final-gather.md), [sponza-gi-correction-2026-09-06.md](rendering/sponza-gi-correction-2026-09-06.md)

16³ L2 SH probe grid traced through the global distance field, with SDF
reflections (`r.GI.Reflections`, SSR lerped in before the BRDF) and the
world-space final gather (`r.GI.FinalGather`) layered on top. Until the
SponzaTest port (`phase0_sponza-parity-03/04`) the mesh SDF bake and global SDF
composite shaders (`mesh_sdf_bake_cs.hlsl`, `global_sdf_composite_cs.hlsl`) were
one-line empty stubs and `r.RadianceCache` could not actually be disabled —
the earlier "implemented but disabled because Brixelizer supersedes it"
description was wrong on both counts. (Corrected 2026-10-08: this paragraph used
to say every trace/bake/composite shader under `gi/` was a stub, "32 stub files,
26 referenced"; that count is for all of `runtime/shaders` in parity-01's
2026-09-07 sweep, and the trace shader was not a stub. See "One-line stub
shaders".) Sponza enables it from
`sample/projects/sponza.uzproject`; `--cvar` beats the project file since
task 05. Brixelizer GI was planned as the alternative path; it is dormant (never constructed since
2026-04-10, entry above) and its task was closed as superseded on 2026-10-08.

**SDF fixture refresh (2026-10-13):** the two standalone GPU fixtures now bind the
current 9-DWORD mesh bake and 13-DWORD GDF composite layouts, whole-window regions,
production toroidal window origins, zero slice offset/band, and WRAP s3.
`test_sdf_window.ps1 -BvhCompare` passes: all 2,097,152 fp16 voxels are bit-identical
(mesh128/GDF256, mesh voxel 37.500 cm, combined bias 28.516 cm).
`test_sdf_gpu.ps1` and `test_sdf_refined_controls.ps1` pass; enclosed/transformed/
mixed-validity reflection controls have max absolute error 0.000195.
`test_sdf_reflection_energy.ps1` passes its uniform agreement check with RGB
Karis/plain ratios (0.9965, 0.9949, 0.9949); layered history retains
(0.8845, 0.8846, 0.8846), a measured approximately 11.55% energy loss.
The gate is **unblocked (2026-10-13)**: `test_sdf_surface_origin.ps1` and
`test_sdf_diffuse_gpu.ps1` now compile. Their wrappers (regenerated on each run from
the `.ps1` heredocs) supply the 8th `GatherSdfDiffuse` argument `rotationMode` as `0u`,
the canonical hashed rotation that matches the fixtures' own embedded `GeometricCoverage`
reference. On the current build (RTX 5080): `test_sdf_diffuse_gpu.ps1` passes every
sub-test (world reflections max abs error <= 0.000195); `test_sdf_surface_origin.ps1`
reproduces the carried-over diffuse-path leak below (max abs error 0.009328) and its
`-Reflections` variant passes (max abs error 0.001984 < 0.002). See [fixture refresh evidence](sdf-gpu-fixture-refresh/01-fixture-refresh.md).

Unresolved (carried over from SponzaTest's handoff and reproduced here on 2026-09-08):
- 1 of 32 world-reflection rays leaks through the enclosed-box fixture
  (`scripts/rendering/test_sdf_surface_origin.ps1`, max abs error 0.009328;
  reproduced on the current build 2026-10-13 after the compile was unblocked — the
  diffuse-gather default path only; the `-Reflections` mirror path is clean at 0.001984).
- An extreme grazing reflection exhausts the 256-step budget
  (`scripts/rendering/test_sdf_dynamic_trace.ps1`, 14/15).
- ~~Sponza LOD2/LOD3 decal triangles still carry the flipped normals~~ —
  **fixed 2026-09-08** (`phase0_sponza-followup-04`). Cause: `GenerateAutoLODs`
  (`runtime/engine/renderer/mesh/auto_lod.h`) ran `meshopt_simplify` with
  **positions only**, so a collapse could leave a triangle whose winding no
  longer matched its vertex normals. It now uses
  `meshopt_simplifyWithAttributes` with the normals (weight 0.5), which fixes
  every future import. Baked LODs in the shipped assets were repaired in place
  by `scripts/rendering/test_decal_lod_normals.ps1 -Repair` (winding swap only, vertex
  data untouched). The decal went from 23 inconsistent triangles at LOD2 and 26
  at LOD3 to zero. Across the 145 Sponza meshes the audit classes are 56 clean,
  43 simplifier artifacts (repaired) and 46 whose **LOD0** is itself
  inconsistent — authored or smoothed normals that legitimately disagree with
  some faces, left alone. One of those, `arch_stones_01_stone_trims_01_0`, is
  100 % inconsistent at LOD0 (a whole-mesh winding inversion) ~~and still needs
  the treatment the decal got~~ — **repaired 2026-10-08** with six more Sponza
  whole-mesh inversions by `scripts/rendering/repair_mesh_winding.py` (see "Meshes exported
  counter-clockwise" under Rendering).
- No Surface Cache and no complete emissive transport (only the VFX → DDGI
  emissive feed); no Lumen parity is claimed.
- Falcor PSNR ≥ 35 dB parity unverified (no Falcor install on either machine).
- `--diag` reads `gi.gdf_cascade0` / `gi.gdf_raw_cascade0` as zero in **both**
  repositories, even after the 3D readback `D3D12_BOX` fix. *Correction (2026-10-08): fixed in
  this repository on 2026-09-08 by `phase0_sponza-followup-03-diag-3d-target-readback`: the
  inspector skipped every 3D target and now reads the centre Z slice, and
  `scripts/rendering/verify_sponza_loading.ps1` fails on an all-zero `gi.gdf_*` since then. Its 2026-10-08
  run reads `gi.gdf_cascade0` at 0.014 / 625 / 156.4 and `gi.gdf_raw_cascade0` at 625 / 625 / 625
  (`scripts/sponza-loading-fixed-converged.log:2297`, git-ignored). SponzaTest was not re-checked.*
- ~~Stone materials read as metallic~~ — **fixed 2026-09-08**
  (`phase0_sponza-followup-05`). The graphs wire `Metallic <- <tex>.B`
  correctly (glTF metallicRoughness), but Sponza's maps carry 3-25 % in that
  channel on dielectrics, and four materials with no Metallic input at all
  (`glass`, `lamp_glass_01`, `light_bulb`, `dirt_decal`) hit
  `gbuffer_ps.hlsl:188`'s `metallic = metallicMultiplier` and rendered fully
  metallic at the neutral 1.0. 23 materials now carry
  `metallicMultiplier: 0.0`; the metallic share above 0.25 fell from 33.2 % to
  0.83 % at Camera A and 41.9 % to 3.9 % at Camera C (the remainder being
  `metal_door`, which is genuinely metallic at 0.816). `E:\SponzaTest` carries
  the same unfixed values. Guard: `scripts/rendering/audit_sponza_metallic.ps1`.
- The metallicRoughness textures themselves still carry the dirty blue
  channel; the multiplier neutralises them at material level. A clean re-export
  is the asset-side fix.

---

### DDGI (`DDGISystem`) — `PARTIAL` (dispatched; DXR-gated)
**File**: [ddgi_system.cpp](../../runtime/engine/renderer/gi/ddgi_system.cpp)
**Shader**: [ddgi_probe_update_cs.hlsl](../../runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl)

Octahedral irradiance + depth atlases, probe grid layout, bindless slots.
`ddgi_probe_update_cs.hlsl` **is** dispatched from `frame_gi_passes.cpp`
(`RadianceCache_Trace` pass) once `r.RayTracing.GI=1` and a TLAS is ready;
deferred lighting samples the atlases after `HasCompletedFirstUpdate()`.
It shares `RadianceCacheTraceConstants` with the SDF trace and declares its first 13 rows, 208
bytes (176 + VFX emissive tail + grid-recentre row; in `ddgi_probe_common.hlsli` since block
B.6). The struct is 464 bytes since block B.2 added 16 change-box rows that only the SDF trace
reads. SponzaTest could not validate the DXR path
(`DDGI_ProbeUpdate` pipeline failed with 0x80070057 on a Tier 1.0 GPU); the
2026-09-08 run on an RTX 5080 (Tier 1.2) is recorded in
`.rulebook/archive/2026-10-10-phase0_sponza-parity-06-qa-harness-validation/notes.md`.

That first validated run exposed four probe-energy defects, all fixed on
2026-09-08 (`phase0_sponza-followup-01-dxr-ddgi-energy-parity`):

1. The probe hit radiance was missing the Lambert `1/pi`, so every surface
   bounce was pi times too bright.
2. There was no shadow ray, so each hit was lit as if the sun were unoccluded.
3. Chebyshev visibility was inert: the RG16F depth atlas stored a squared
   distance in centimetres, which saturates FP16 (65504) at Sponza's scale, so
   the variance collapsed and every probe reported full visibility. Both
   moments are now stored normalised by `maxTraceDistance`.
4. `DDGISystem::DispatchUpdate` built its constants with a placeholder
   environment (sun pointing straight up at intensity 1, sky 0.6/0.7/0.9,
   no distance field), unrelated to the loaded scene. It now reads the same
   `PerFrameCB` fields and `GlobalDistanceField` clipmaps the radiance cache
   reads, and takes the hit normal and albedo from the field
   (`runtime/shaders/runtime/gi/gdf_surface.hlsli`) because inline `RayQuery`
   reports no hit attributes.

Result: Camera A 52.0/255 for both paths (was 77.1 for DXR), Camera C 91.8 vs
the SDF path's 90.1: display-referred PNG means with auto exposure on, and Camera A is identical by
construction (the final gather replaces the probe irradiance for both sources), so neither is a
measurement of the probe energy. Found on the way: with `r.RayTracing` (master) off and `r.RayTracing.GI` on, the writer ran against an empty TLAS and filled every probe with sky — the dispatch is now gated on `IsGIEnabled()` and a populated TLAS. Since then (followup-01, all in 7eea6b95) the DDGI dispatch reads `r.GI.RadianceCache.RaysPerProbe` instead of a hardcoded 32 (2026-09-15); escaping rays sample the sky cubemap as the SDF writer does (`r.GI.DDGI.DirectionalSky`, default 1; the two writers' sky fetch is equal to 6e-9, flicker ledger H-51); the atlases' 1-texel tile borders are filled after every update (`ddgi_border_fill_cs.hlsl`, H-55/H-56); the probe rays are the cache's stratified set rotated per frame and per probe (H-57/H-58); and the consumer's Chebyshev test and depth atlas match an offline model of their code on known inputs (H-59). The two candidates once listed here are settled: at Camera C no SDF probe ray exhausted the 96-step budget (lost fraction 0 on every probe, 45-probe-sky-parity, H-51), and with the final gather off what remains between the paths at the consumer is dominated by their two visibility models (the cache query returns zero when no probe survives its validity and facing tests; the DDGI query has only the Chebyshev test; H-62 as corrected, H-63), not by the writers. The writers' energy: block B.6 below.

Regression guard: `scripts/rendering/test_ddgi_energy.ps1` (20 source-level checks since 2026-10-08; from 2026-09-17 it threw before any check, because `TraceProbeRay` changed signature and then moved to `ddgi_probe_common.hlsli`).

**Block B.6 (2026-10-07, `phase0_lighting-block-b-06-ddgi-dedup`,
[lighting-block-b-06-ddgi-dedup.md](lighting/uzengine/sponza-lighting-2026-10/lighting-block-b-06-ddgi-dedup.md)).** The update shader ran
one thread per atlas texel and every thread traced ALL of its probe's rays: 36 irradiance + 256
depth texels per probe, 292 traces of each ray (`RadianceCache_Trace` 3.57 ms on Sponza Camera C at
1080p, the SDF writer included). `ddgi_probe_trace_cs.hlsl` (new) now traces each ray once into two
ray buffers the atlas writer integrates (`ddgi_probe_common.hlsli` holds the shared trace and
helpers), and adds the second bounce the SDF writer always carried (`albedo × the previous frame's
probe irradiance` at the hit, `r.GI.DDGI.IndirectTerm`, default on; `r.GI.DDGI.RayBuffers 0`
restores the per-texel trace). Measured: `RadianceCache_Trace` 1.203 ms with the dedup
against 3.585 ms per-texel on the same binary (SDF writer alone
0.941 ms: the DDGI writer went from 2.644 to
0.262 ms); the writer's per-probe mean ray radiance with the buffers (indirect term off)
matches the per-texel trace (median relative 0.00000, D1a PASS) and the
frame with the final gather off matches too (median 0.00000, D1b PASS);
with the indirect term the energy ratio DDGI/SDF reads 0.9662 (was 0.9321; D4 PASS; 0.9637 at the
closing sweep on 794b632f) and the diffuse GI of the frame (gather off) rises × 3.093. That ratio is of
luminance (no per-channel statistic is logged) and divides the two writers' grid means over different
probes (the cache's 3 885 with field distance >= 5 cm, 185 of them unreachable under mode 3 and logged
as 0; the DDGI's 4 096): on the cache's valid probes the same logs read 0.973 / 0.970, on those inside the
building 0.943 / 0.940 with a per-probe median of 0.935 / 0.943 (`scripts/rendering/gi_energy_populations.py`;
followup-01 notes, 'Closure review'). Note for every DDGI measurement:
with `r.GI.FinalGather 1` (the QA project) the SDF final gather replaces the probe irradiance in every
pixel, so the frame does not show the atlases at all (the dump's `diffuse_indirect` is ~0.0006 there).
The DDGI probes still have no relocation / classification, no Chebyshev test inside the indirect
term, and `g_probeOffset` (temporal spreading) is unused (open; listed in
`lighting-block-b-06-ddgi-dedup.md` 'Still open', the block-B backlog, no task yet). Nothing here is
measured since 513e2983 and the sky changes of b8ff5077 / 4501fa74: 513e2983 took the mesh decals out
of the distance-field occluder set and the shadow passes but not out of the TLAS
(`tlas_manager.cpp:76-81` takes every draw command), so the DDGI writer still traces Sponza's
dirt-decal quads as opaque occluders the SDF writer no longer has. H-61's writer term (inside the
building the DDGI's per-ray sky 6.5-9.2 % above the cache's where the field reports a surface, measured
on the QA field, GDF 64 / mesh SDF 32) has an untested cause (candidate: the surface bias of
`r.GI.MeshSdf.TwoSided 1`) and was never measured on the project's field (both open, owner:
phase0_lighting-ddgi-sdf-writer-parity-head, proposed at followup-01's closure).

**Allocation (2026-10-08, open decision N4 of `phase0_indoor-lighting-system`).** The atlases and the
ray buffers are created at start-up whether DXR GI is on or not
(`runtime/engine/renderer/scene/scene_renderer_init.cpp:748-761`). They take about 15.1 MiB: a 2 MiB
irradiance atlas, a 5.06 MiB depth atlas and two 4 MiB ray buffers (`ddgi_system.cpp:47-118`). The
comment at `:751` says "~256 KB". The consumer binds them only after the writer's first update and
while `r.RayTracing` and `r.RayTracing.GI` are on (`frame_constants.cpp:890-904`), so with DXR GI off
(the default) the cost is memory only. Open, owner: `phase0_indoor-lighting-system` (N4).

**Stale binding after a runtime `r.RadianceCache 0` (found 2026-10-08 by the
`phase0_indoor-lighting-system` review).** That binding tests `m_radianceCache.IsInitialized()`
(`frame_constants.cpp:890-892`), which stays true, rather than `m_sdfGiEnabled`, while the DDGI
writer runs inside the radiance cache's pass, which is not registered while `r.RadianceCache` is off
(`frame_gi_passes.cpp:149-161`). `ApplyCVars` re-reads the CVar every frame (`frame_setup.cpp:137`,
`scene_renderer_init.cpp:1090-1092`) and nothing resets `DDGISystem`'s first-update flag. Turning
`r.RadianceCache` off at runtime with DXR GI on therefore keeps the last DDGI atlas bound as source 1
of the priority chain while nothing updates it: the stale-binding class that the comment at
`frame_constants.cpp:885-889` records as fixed for `r.RayTracing.GI`. At start-up the gate holds
(the writer has never run). Open, owner: `phase0_indoor-lighting-system` (not yet in its
`tasks.md`).

**Indirect-path noise (2026-09-08, `phase0_sponza-followup-06`).** With `r.TAA` off
(the engine default), the stochastic passes — SDF reflections (4 GGX rays/pixel),
world-space final gather, GTAO — show per-pixel noise; on a dark wall beside a
window it reads as white specks. Root cause of the specks: reflection rays that
escape through the opening return sky radiance, 0-or-1 per pixel, with no filter
on that path. Three estimator defects were fixed along the way (plain mean →
Karis weight; below-horizon rays renormalised away → weight kept; radiance-cache
probe selection by the normal-mapped normal → geometric normal), and the Sponza
project now sets `r.TAA: 1` (Camera A isolated bright pixels 944 → 244). A second report after enabling TAA — the whole scene flashing near-white
while flying — was the radiance cache readiness gate dropping every indirect
path for one frame each time the camera crossed a probe cell (~267 cm);
`HasProducedResult()` is no longer gated on the recentre flag (frames at a
crossing 18.9 → 236.3 → 15.4 before, 18.9 → 15.6 → 12.8 after). A third
report — a blink at one point while rising (z ≈ 267 cm, the probe spacing) —
was the recentre itself: lighting was handed the *current* grid origin with the
buffer completed *last* frame (one frame indexed one cell off → flash), and the
retrace ran with first-write semantics (no history, no multi-bounce feedback →
a plateau decaying over ~40 frames). Vertical sweep at the user's pose, frames
131→132→133: 83.4 → 95.3 → 88.0 before, 83.4 → 83.9 → 84.1 after
(`scripts/sponza-vertical-camera.*`). Lighting now indexes the published buffer
with `GetCompletedGridOrigin()`, and the recentre scrolls the probe history
through an integer grid shift (`historyShift*` row, the 13th row of the trace constants;
they are 464 bytes since block B.2 added 16 change-box rows). Only the first write and a
scene-revision change reset it, and since block B.2 a GDF change box does too for the probes
inside it (replaced at the default `r.GI.RadianceCache.RegionBlendAlpha` 1; more than eight
pending boxes reset every probe, `radiance_cache.h:238-246`). Items 4.1 and 4.2 are closed. The
four debug views were fixed on 2026-09-16 (`scripts/rendering/run_debug_view_check.py` 7/7, last run on the
2026-10-05 binary). No reflection filter was built: measured in linear radiance on the 2026-09-23
build, the TAA output keeps no isolated pixels on `shadow_wall`, and those at pose A's
`rear_door` and `right_shadow` are not reflection pixels (ledger H-41, H-68/H-68b; H-68's rule is
the re-test). About 90 % of the door's isolated-pixel luminance goes with the translucent pass,
the door mechanism `phase0_sponza-followup-12` recorded (H-38, H-45..H-48), which the proposed
`phase0_sponza-flicker-correction` owns. Accepted, not tracked:
with `r.TAA` off the SDF reflections still sparkle beside openings, so a project that enables
`r.GI.Reflections` must set `r.TAA 1` (`sample/projects/sponza.uzproject` does); and the ~170
low-energy isolated output pixels per frame left at `rear_door` with the translucent pass off
(1.6 % of the region's luminance), which are not reflection pixels and which nothing attributes
(H-68b). Guards: `scripts/rendering/test_sdf_reflection_firefly.ps1` (18 checks),
`scripts/rendering/test_rc_recentre_scroll.ps1` (42 checks since its 2026-10-08 re-anchoring).

---

### TAA jitter applied to the view-projection as if it were the projection — `FIXED` 2026-09-08
**File**: [frame_setup.cpp](../../runtime/engine/renderer/frame/frame_setup.cpp), [taa_jitter.h](../../runtime/engine/renderer/frame/taa_jitter.h)
**Tests**: gtest `TaaJitter.*`, `scripts/rendering/test_taa_bloom_input.ps1`; QA scenes `scripts/sponza-closeup-camera.*`, `scripts/sponza-closeup-approach.*`

The Halton sub-pixel jitter was added to `m[2][0]/[2][1]` of the combined
view-projection. That row multiplies view-space z only in a pure projection
matrix; in the view-projection it multiplies the vertex's world z, so the NDC
shift was `jitter · world_z / w` — ±7 px on a wall 40 cm from the camera at
z ≈ 576, ~0.2 px on the far atrium wall. The TAA resolve un-jitters by the
nominal (jx, jy) pixels and its history clamp then follows the displaced frame:
near geometry shook while the camera stood still ("a mesh balança quando a
câmera chega perto"). Static-camera pairs: 0.25–1.1 % of pixels changing by
> 40/255 with TAA on, 0.00 % off; a cornice-edge pixel 180 → 188 → 31 → 179.
Eliminated first: GTAO, final gather, SDF reflections, anti-flicker, variance
clip, bloom, CAS, motion blur. Fix: `ApplyTaaJitter` folds the NDC translation
through the matrix's w column so every vertex moves by exactly (jx, jy) pixels.
After: 0.19 / 0.00 %, 0.22 / 0.00 %, 0.19 / 0.00 % (before 1.06 / 0.25 %, 2.45 / 1.11 %, 1.54 / 0.70 %; TAA off 0.94–0.98 / 0.00 %) — now below the TAA-off noise floor, the cornice-edge scanline identical across frames, changed-pixel grid (threshold 4) 92 897 → ~800. SponzaTest has the same defect (its `r.TAA` is off by
default). Also fixed on the way: the TAA-path Bloom read the jittered scene
colour instead of the TAA output (not the cause — `r.Bloom=0` control identical).

---

### Static-camera flicker on high-contrast HDR edges under TAA (Sponza window bars) — `OPEN`, attributed 2026-09-09
**File**: [taa_resolve_cs.hlsl](../../runtime/shaders/runtime/postprocess/taa_resolve_cs.hlsl), [taa_upscaler.cpp](../../runtime/engine/renderer/passes/postprocess/taa_upscaler.cpp)
**Tests**: `scripts/rendering/test_region_capture.py` (23 engine checks of the region dump v2; it replaced `scripts/test_region_dump.ps1`, removed with the version-1 format in followup-09 item 2.2) and `scripts/rendering/test_flicker_metrics.py` (26 synthetic checks, no engine; 26/26 again since the reader change of 2026-10-08, see "Measurement status" below); QA scenes `scripts/sponza-flicker-w-seq.*`; packages `.screenshots/flicker-{sequence,jitter,region}/` and `.screenshots/flicker-elimination/`
**Status (2026-10-08):** the window residual is open, owner: `phase0_sponza-followup-15-taa-window-residual` (proposed at the followup-10 and followup-14 close-outs; the other regions' owners are under "Where followup-12 left each region" below). Every candidate tried on this ROI so far was rejected: `r.TAA.PreExposeWeights` (below; ledger H-08b), clamp removal and the AABB-only clamp (H-13; H-23, H-23b, H-23d), frozen clamp bounds (H-25b), `r.TAA.VarianceClipGamma=2.0` (fails G6 on `window`, H-42) and followup-14's `r.TAA.ReconstructionHdrWeightExponent` (see "TAA reconstruction HDR weighting" above). The mechanism's current state is under "Static TAA residual" above; the candidate fixes listed below remain untried.

With the camera still, pixels on the bar/pane edges of the barred window oscillate with a
period equal to `r.TAA.JitterSequenceLength`. Per-pixel records from the lighting pass and
the resolve (`--region-dump`) show the raw sample is deterministic in the jitter index (no
stochastic component; the SDF reflection term is 0.0006 Y there and switching reflections
off leaves the linear oscillation unchanged). The residual is produced by the resolve itself
on an 8 Y : 0 Y edge: the plus filter un-jitters by weights only, so pane light enters the
adjacent bar pixel by a jitter-dependent amount (filtered 0 → 1.5 Y while the raw stays 0),
and the variance clamp then snaps the history (17 % of the flicker pixels' frames, 41 % of
their output change). On screen the visible part is the dark bars next to the panes: a
0.005–0.10 Y change rendered at auto-exposure ≈ 67 (`r.AutoExposure.MinBrightness` 0.001
with bars at 0.000 Y) is 50–100 display levels, on top of a ~94 L bloom floor. Anything that
moves the exposure changes the *count* of threshold crossings without touching the
oscillation (reflections off: 37 → 0.6 px > 40 per frame; bloom off: 39 → 1547).
`UpscalerInput::exposure` was never consumed by the resolve's HDR weights; the opt-in
`r.TAA.PreExposeWeights` halves the bar-side count but destabilises the pane side (output std
p99 0.18 → 2.49 Y), so it stays off. Candidate fixes not yet tried: partial pre-exposure,
resampling (not re-weighting) the un-jittered neighbourhood, or an exposure floor for the
interior — open, owner: phase0_sponza-followup-14-validate-flicker-correction, which selects and
validates any correction (the display and isolation tasks are diagnostic until then). Keep
`r.TAA.JitterSequenceLength=8`; length 1 (a constant Halton23(1) offset) is a
control, not a fix.

**Measurement status (2026-09-09, followup-09).** The capture and the metrics behind those numbers
were re-validated before any further conclusion is drawn. The instrumentation is now
`--region-dump` version 2: several rectangles per run, GPU frame stamps on every record block, a
readback that runs independently of TAA, a shutdown flush and a run manifest
(`--run-manifest`: executable hash, effective CVars, exposure, frame timing, integrity counters).
`scripts/rendering/test_region_capture.py` (23 behavioural checks) and `scripts/rendering/test_flicker_metrics.py`
(22 synthetic checks then, 26 since followup-13) guard it. From at least 2026-09-18 until 2026-10-08,
11 of the 26 errored: their format-2/3 fixtures carry only the LIGHTING, TAA and OUTPUT groups
(field masks 3, 5, 4), on which all three legacy layouts agree, yet `resolve_layout`
(`scripts/rendering/flicker/regiondump.py`) rejected any record count that more than one legacy layout
matched, which also left real format-2/3 captures with those masks unreadable. The reader change
handed over at followup-01's closure (accept matching layouts that agree on every group the file
carries) was applied on 2026-10-08, and `python scripts/rendering/test_flicker_metrics.py` (no engine) reads
26/26 again. What the validation established, and what it corrected:

* the records are linear scene radiance — a 16x exposure change leaves them bit-identical, and the
  renderer is deterministic at a given frame index (two independent runs agree exactly); *corrected
  2026-10-08:* separate processes are **not** bit-reproducible — followup-14 found up to 795 of
  786 432 recorded values differing between processes of one configuration, by up to 1.6e-2
  radiance, while run aggregates reproduce closely but not always exactly (up to 2.3e-06 relative
  in mean energy on A `rear_door`, ledger "Corrected 2026-09-15") — so the bit-identical exposure
  pair and the exact agreement held for those processes only. The records are linear radiance on
  the run aggregate, which is how `scripts/rendering/test_region_capture.py` judges this control (check 3)
  and instrumentation neutrality (check 5) since 2026-10-08; bit-identity is checked against a
  measured same-configuration spread instead
  ([flicker-hypothesis-ledger.md](rendering-arch/flicker/flicker-hypothesis-ledger.md), "The renderer is not
  bit-reproducible across processes"; [build-and-deployment.md](../guides/build-and-deployment.md),
  "Capture reproducibility");
* the resolve's own blend equation, recomputed from the recorded fields, reproduces the recorded
  output to 9.2e-07 relative error, so the field meanings are verified rather than assumed;
* the linear-domain oscillation and the display-domain threshold crossings are **disjoint pixel
  sets** (bright pane side vs dark bar side of the same edge) — the two descriptions in the earlier
  package are consistent, but neither one can stand for the other;
* **auto-exposure adapts with the frame time**, so any change of cost moves the displayed image by
  about one code value; every display-referred comparison must fix the exposure;
* **bloom mixes neighbouring pixels**, so a display value is not a per-pixel function of the
  recorded radiance unless bloom is off;
* `--screenshot-frames` file `f_N` held render N+1 until 2026-09-09; older packages must be aligned
  by one frame.

Open hypotheses, their scope and the next discriminating experiment are tracked in
[flicker-hypothesis-ledger.md](rendering-arch/flicker/flicker-hypothesis-ledger.md); evidence packages live under
`.screenshots/flicker-elimination/`.

**Left open when followup-09 closed (2026-10-08).**

* The baselines and the per-ROI decision thresholds of ledger rule 2 were measured on the 2026-09-09
  binary and have not been re-measured since; the engine has changed in between (among others b8ff5077
  sky capture, 975fc3c9 mesh winding, 513e2983 decals, c14215ed split-sum). Re-measure them into a new
  package (`scripts/rendering/run_flicker_baselines.py --out <new dir>`, then `scripts/rendering/run_flicker_field_check.py
  --out` and `scripts/rendering/report_flicker_baselines.py --dir` on that directory) before any flicker decision
  rests on them, after correcting the fixed paragraph `scripts/rendering/report_flicker_baselines.py:97-102`
  writes into every report ("agree to within 0.005 %", "the renderer is deterministic at a given frame
  index") — open, owner: `phase0_sponza-flicker-correction` (proposed; re-homed from
  `phase0_sponza-followup-12-isolate-shadow-lighting`, which closed the same day).
* No shadowed-wall ROI sits above the measurement floor: `wall_bounce` (radiance 5.55e-05, temporal
  std 1.58e-06) is darker than `shadow_wall` (1.64e-04, 3.82e-06) and has no ledger threshold — open,
  owner: `phase0_sponza-flicker-correction` (proposed; re-homed from followup-12).
* Scope note, fixed with this closing (2026-10-08): report identity and `--reuse`.
  `runner._hash_file` (`scripts/rendering/flicker/runner.py`) hashed only the first 64 MiB, so every
  `harness.sha256` written 2026-09-15..2026-10-05 (exe 80.0-81.1 MB) covers a prefix (the manifest's
  `fnv1a64` is whole-file; the ~11.6 MB exe built since 2026-10-07 was hashed whole); it now hashes
  the whole file. `scripts/rendering/run_gdf_sampling_side_accum.py` and `scripts/rendering/run_debug_view_check.py`
  stamped only the binary present at analysis time; their reports now also carry each capture's
  identity from `runner.capture_identity` (the analysis-time `harness` stamp stays beside it).
  `scripts/rendering/run_candidate_evaluation.py --reuse` wrote `candidate_<phase>.json` over a three-repeat
  report; a reuse run now writes `candidate_<phase>.reuse.json` (`runner.report_path`). Reports
  written before the fix keep what they recorded.
* Scope note, not an open item: reports of the October lighting harnesses (e.g.
  `test-env-mis/env_mis.json`, `test-split-sum/split_sum.json`) carry no executable identity because
  `capture_identity` is opt-in; each capture directory's `manifest.json` holds it.

**Mechanism, isolated 2026-09-09 (followup-10).** The window has two independent defects:

1. **An energy bias.** The converged image holds 91.9 % of the radiance of the correctly resampled
   input over the whole window rectangle; on an analytic 1.5-px bright bar the loss is 48.4 %. The
   cause is the HDR weighting (`HdrWeightY`) applied *inside the reconstruction filter*: dropping it
   (`r.TAA.ReconstructionMode=1`) takes the rectangle to 98.0 % and the bar loss to 20.6 %. The loss
   scales with contrast — the same bar at 1.0 radiance loses 16.6 % instead of 48.4 %.
2. **A temporal residual** driven by the jitter. A constant offset (`r.TAA.JitterScale=0`) removes it
   completely (0.0486 → 0.00001 radiance on the edge pixels) and leaves the energy bias untouched.
   Per stage: the raw sample is perfectly stable on the pane (0.00000) and swings 3.5 radiance at the
   bars; the jitter-weighted plus filter spreads that onto the pane (0.248); the accumulation
   attenuates everything about 25x. On the boundary pixels the variance clamp adds about 35 % of
   what is left and the neighbourhood AABB alone behaves like no clamp; over the whole window
   rectangle the clamp adds 48.5 % and either box alone keeps most of it (the variance box all of
   it, the AABB 80 %; ledger H-04c).

Removing the clamp is not an option: it gives the best static number and never clears invalid
history (peak error after a camera cut 10x the baseline, still 46x after 80 frames). This TAA
rejects history only when the reprojection leaves the screen, so the clamp is its only disocclusion
defence. Diagnostic CVars `r.TAA.JitterScale`, `r.TAA.ReconstructionMode`, `r.TAA.ClampMode` and
`r.TAA.BlendMode` all default to the production behaviour; the offline model that reproduces the
resolve to 2e-7 is `scripts/rendering/flicker/taa_model.py`. Until 2026-10-08, three checks in their guard
`scripts/rendering/test_taa_modes.py` were weaker than stated: a jitter negative control ending in `or True`;
a `r.TAA.BlendMode=1` check that never set `r.TAA.CurrentFrameWeight`, so it passed while that CVar
was dead (ledger H-39); and a clip-box check that compared RGB with the lower corner only and never
compared with `taa_clip_max`. All three are fixed and verified statically, and the fixed script passed
12/12 on the GPU on 2026-10-09 (Debug build of 2cc2e85c): **validated 2026-10-09** (followup-10 closed).

**Display side, measured 2026-09-09 (followup-11).** The oscillation reaching the eye is a few per
cent of radiance amplified by the exposure an interior needs. On the identical HDR sequence, replayed
through a tonemap model validated against the engine to 1 code value, a 4x exposure change multiplies
the px > 40 count by 33.8x while the linear records stayed bit-identical in those single-process runs
— a threshold count is never an energy statement. Auto-exposure settles at 66.31 on pose W and is
stable when still; with the camera moving it adapts without oscillating (followup-14, `coverage.json`:
66.1-73.2, one direction only through the strafe, the second hold and after the cut). Bloom lifts the
dark floor 2.4x and halves local contrast, and it lowers the threshold counts at both exposures
measured — without → with bloom, px > 8 677 → 408 per frame pair at exposure 1.0 and px > 40 1547 → 39
per frame at the ~67 operating point (one run, 2026-09-09) — so it masks the residual rather than
revealing it. Two controls used in earlier packages turn out to substitute rather than subtract:
`r.GI.Reflections=0` falls back to the prefiltered IBL cubemap (specular radiance is initialised from
it and only overwritten by the SDF trace), making the interior 11x brighter and the exposure 16x
lower without changing the oscillation; and `r.TAA.PreExposeWeights` is a no-op at exposure 1.0 and,
at the real operating point, makes the bright side oscillate 11x more while crushing the dark side to
2 % of its radiance. *(Corrected 2026-10-08: this paragraph said 31x, from rounded values, and that
bloom's effect on a count changes sign with the exposure, which inverted the 2026-09-09 package —
ledger H-15b, H-16b, H-05b. Its harnesses, `scripts/rendering/test_display_replay.py` and
`scripts/rendering/run_display_study.py`, asserted bit-identity between processes and were rewritten to judge
repeated, interleaved processes against their measured spread. They ran on 2026-10-09 (Debug build of
2cc2e85c): the replay 8/8; the study 7/8, its auto-exposure equivalence failing once on a 2.5 ppm shift and
passing the pre-registered re-run, read as chance (`scripts/rendering/run_display_exposure_rerun.py`); at the
operating exposure bloom lowers px > 40 from about 1652 to 40 per frame in all four processes, so H-16b
stands with repeats. followup-11 closed 2026-10-09.)*

**Which pass owns each region, measured 2026-09-09 (followup-12).** The regions reported as
flickering are not all lit the same way, and two of them are not lit by the deferred pass at all.
With the forward translucent pass disabled, the barred window falls from 2.666 to 0.00006 radiance
and the right-hand shadowed arcade from 0.0539 to 0.00047: their colour is **forward translucent
geometry**, which the deferred lighting records do not describe. No GI control moves the window
(0.99-1.00x under all six); the arcade stays within the 5 % decision threshold under five of them
(1.00-1.05x) and moves 1.33x only under `r.GI.Reflections=0`, which substitutes the IBL cubemap and
raises its mean 1.52x (ledger H-06c). The rear door is mixed (61.5 % deferred). The deeply shadowed
walls and the sunlit floor are fully deferred, and their residuals have different sources again:
the shadowed walls are 97.5 % diffuse indirect with the final gather's per-frame sampling dominating
and GTAO contributing about 40 %, while the sunlit floor is 99 % direct light. The converged
radiance cache contributes nothing to any of them (freezing it after convergence leaves the diffuse
indirect at 1.00x and the output at 0.96-1.00x; ledger H-06c).

**Moving camera: GDF recentres are not a measured cause, corrected 2026-10-08 (followup-13).**
Until 2026-10-08 this paragraph called the recentre "a demonstrated cause": a motion-compensated
residual 1.31-1.35x higher at recentre frames, ROI-mean steps of 3-6x the ordinary change, and an
elevation that followed a half-voxel shift of the snapping phase. That was an analysis artefact. The
study's moving-frame mask ran 120 frames behind the trajectory (`scripts/rendering/sponza-gdf-boundary.lua`
counts from its on_start frame, logged `base_frame=37` in those runs), so 160 of the 360 frames
treated as moving were still, while every recentre happens on a moving frame. The retained package
was re-analysed without the engine (`python scripts/rendering/run_gdf_motion_study.py --reanalyse
.screenshots/flicker-elimination/13-isolate-gdf-motion`). The residual near recentres is 1.09x
(shadowed wall) and 0.89x (lit wall) the rest, and with `r.GI.GDF.FreezeCenter=1`, which has no
recentre at all, it is the same at the same frames: per frame the baseline is 0.99943-1.00022 of the
frozen run at all 16 event frames, the clipmap-1 events included. Valid coverage is 76-86 % on the
moving frames; the 88 % quoted before was over all frames. The field side agrees: a single-voxel
recentre leaves the overlapping field bit-identical on clipmaps 0 and 1 (ledger H-49, H-54), and
since ce2caa5e a recentre recomposes only the entering slab ("Global distance field recentre"
below). Ledger H-07c supersedes H-07b, and the phase sweep H-40 is void (H-40b). The diagnostic
CVars `r.GI.GDF.FreezeCenter` and `r.GI.GDF.CenterPhaseOffset` default to production behaviour;
since ce2caa5e the phase offset only moves the crossing frames, not the grid. The motion residual
away from recentres is not attributed, and freezing the centres leaves it unchanged (H-20b). Its
next control is a hold of the cascaded-shadow-map setup, which needs a new diagnostic CVar: the
cascades are refitted every frame today, because `kCamera` is set unconditionally
(`frame_constants.cpp:151`). That control is open, owner: the proposed task
`phase0_sponza-flicker-correction` (ledger H-18b, H-20b; re-homed from followup-13 with
followup-12's open points on 2026-10-08).

**Where followup-12 left each region (task closed 2026-10-08).** Every region is attributed at pass
level; none has a correction.

* **Rear door.** The forward translucent pass carries 32.5 % of the rectangle's oscillation (H-38).
  There only the glass coverage varies; material, lighting and the produced colour are constant
  (H-45). The coverage changes through the depth test: an opaque silhouette against the sky, about
  26-30 cm in front of the glass, crosses 125 glass pixels as the jitter moves (97.5 % of the missing
  pixel-frames are hidden by the depth test, 0.2 % not rasterised; H-47, `41-door-occluder-depth`).
  The TAA output keeps 14.1 % of the cycle-averaged input energy at the glass and 14.65 % within
  16 px, so the energy is lost, not spread (H-46); the loss and the oscillation first appear in the
  plus-filter reconstruction, where tap values and weights partly cancel (H-48). Acceptance criteria
  for a correction were pre-registered on 2026-09-17 in followup-12's notes.md and must be completed
  before use (no M1 tolerance, a per-mask E1, ROIs missing or undefined at pose A; its closure record
  lists them). 67.5 % of the oscillation survives with the translucent pass off (H-38), and the
  region's deferred part is 90 % direct light. Open, owner: `phase0_sponza-flicker-correction`
  (proposed 2026-10-08).
* **Right arcade (`right_shadow`).** Switching the translucent pass off removes 97.1 % of its
  oscillation with 98.8 % of its mean, so whether the pass introduces the oscillation or is all there
  is was not separated (H-38); what the pass consumes there was never recorded. Open, owner:
  `phase0_sponza-flicker-correction` (proposed; ledger H-38b).
* **Sunlit floor (`lit_floor`).** 99 % direct light; which part of the direct path varies is
  inconclusive (H-18). The discriminating control, a shadow-map freeze, does not exist: no
  `r.Shadow.*` CVar provides it. Open, owner: `phase0_sponza-flicker-correction` (proposed; ledger
  H-18b).
* **Shadowed walls.** The gather's output variation dominates (H-28c); decorrelating its rotation
  makes the visible flicker 41 % worse (H-36), and the clip gamma that helped on a still camera fails
  G6 on `window` (H-42). No candidate remains. Open, owner: `phase0_sponza-flicker-correction`
  (proposed).
* **Baselines.** Every followup-12 number above was measured before b8ff5077, c14215ed, 8aeb8917,
  31b5c14a and 513e2983 (so were followup-09's per-ROI decision thresholds, listed under "Left open
  when followup-09 closed" above), and the pose-A regions were never measured in motion. Open, owner:
  `phase0_sponza-flicker-correction` (proposed).

### Final gather: 8-sample energy deficit and gather-side coupling — `OPEN`, measured 2026-09-10/11 (followup-12)
**Files**: [sdf_diffuse_gather.hlsli](../../runtime/shaders/runtime/gi/sdf_diffuse_gather.hlsli), [ssgi_world_gather.hlsli](../../runtime/shaders/runtime/ssgi/ssgi_world_gather.hlsli), [frame_gi_passes.cpp](../../runtime/engine/renderer/frame/frame_gi_passes.cpp)

Three threads the flicker work found off its own path (ledger H-34, H-35, H-28d):

* **Energy.** Against an independent hashed-rotation reference (32 samples x 128 frames), the
  shipping 8-sample gather reads 8.20 % low on `shadow_wall` and 17.23 % low on `right_shadow`. The
  gap depends on the sample count without converging monotonically (on the wall: 4 samples +6.3 %,
  8 samples -8.2 %, 32 samples +0.31 % against the reference), which reads as the rotated lattice
  aliasing against the integrand rather than a statistical bias; the reference's own convergence and
  uncertainty were never shown, so the deficit is measured, not proven. Sponza ships the gather on
  (`sample/projects/sponza.uzproject`, `r.GI.FinalGather=1`). Decorrelating the rotation removes its
  parity lock and makes the visible flicker 33-41 % worse on the shadowed wall (H-36), so a fix must
  not trade one for the other.
* **Coupling into the reflections.** Changing only the gather's rotation moved the SDF reflection
  term by 8.1e-07 (pose W, inside the 1e-06 tolerance) and by a deterministic 1.4e-06 (pose A, above
  it: H-35's failed confinement control); the route, most plausibly the radiance-cache probes the
  reflection hits sample, is untested.
* **AO and specular under `r.GI.FinalGather.FreezeAfterFrame`.** Specular changes on the frame the
  frozen texture is first reused, and AO in a nine-frame transient (frames 349-357, 5.07e-05); the
  SSGI pass's second output is not the cause, and a shared resource or a missing barrier are not
  excluded. It did not carry H-28c's result, but it is unexplained.

Measured on the September renderer; the sky capture and SH have changed since (b8ff5077, c14215ed).
Open, owner: `phase0_gi-final-gather-bias-coupling` (proposed 2026-10-08).

### `GlobalSDF` — `FUNCTIONAL` (4 clipmaps, raw + expanded)
**File**: [global_distance_field.cpp](../../runtime/engine/renderer/gi/global_distance_field.cpp)
**Shader**: [global_sdf_composite_cs.hlsl](../../runtime/shaders/runtime/gi/global_sdf_composite_cs.hlsl), [mesh_sdf_bake_cs.hlsl](../../runtime/shaders/runtime/gi/mesh_sdf_bake_cs.hlsl)

Four clipmaps (`r.GI.GDF.Resolution`, default 256 on Sponza) composited on
the GPU from per-mesh SDFs baked at load (`mesh_sdf_baker.cpp`, BVH,
two-sided via `r.GI.MeshSdf.TwoSided`), kept as a raw R16F volume plus an
expanded RGBA16F copy. Feeds the radiance cache trace, SDF reflections, the
final gather and DFS. Scene readiness waits for pending bakes (138 on Sponza)
before the first presented frame. (The dormant Brixelizer integration would keep its own 512³ SDF
atlas; it is never constructed, see the `r.BrixelizerGI` entry.)

---

### Global distance field recentre — `INCREMENTAL` 2026-10-05 (block B.2, phase 2)

A recentre of a clipmap used to rewrite all of its voxels over every instance. On Sponza's long
sweep (256^3, 138 instances) `GDF_Composite` averaged 1.27-1.33 ms over 297 frames that hold 46
clipmap-0 recentres (and 11 of clipmap 1, 3 of clipmap 2), so a recentre cost at most ~8.6 ms of
GPU. Corrected 2026-10-08: the "~125 ms" first written here divided by the sweep's 3
radiance-cache recentres, and the "lighting step at grid crossings" it was linked to was an artefact
(ledger H-07c). The clipmaps are stored toroidally now (world voxel k in texel
k mod resolution; readers address by `frac(p / (2 extent))` with the WRAP sampler and skip the
outer voxel of the window) and a recentre recomposes only the entering slabs
(`r.GI.GDF.Incremental`, default on); since phases 3-4 (same day) a changed, added or removed
instance recomposes only its neighbourhood (old ∪ new bounds plus the truncation band
`r.GI.GDF.BandVoxels`, default 32 voxels), the scene revision bumps only for whole-window
recomposes, and the change boxes reach the consumers: radiance-cache probes inside them replace
their history (`r.GI.RadianceCache.RegionBlendAlpha` 1.0), the others blend as on a
recentre, SSGI pixels inside reject theirs (reject code 6). Phase 3-4 gates: G2a PASS (moving box: 60 regional recomposes, 0 whole-window, 0 cache invalidations after loading); G2b PASS (GDF_Composite 0.015 ms mean over the window against 0.575 ms on the whole-window path); G3a-d PASS against the static reference rendered over the same frames (END strip +1.16 %, START -0.33 %, control area within 0.96 % on every frame where the whole-window path drops 8.63 %); G3e FAIL (8.83 % transient peak beside the arrival, gate 5 %, documented); G4 PASS (Sponza Camera C regional vs whole-window 0.13-0.15 %); G6 PASS (band within 2.2e-6); G5 PASS (ratio 0.015).
Gates of phase 2: G1 slab recompose == full recompose PASS (20 recentre events of the boundary trajectory, +-x and +-y single-voxel moves: 100.0000 % identical texels over the whole 256^3 volume, max |delta| 0 cm; 2 events during loading skipped by design); G4 Sponza
Camera C ROIs with Incremental 1 vs 0 PASS (2e-6 relative); G5 recentre cost PASS (1.327 -> 0.0224 ms, ratio 0.017). Still full: the first
fill, the band off, or more than `r.GI.GDF.MaxRegions` (8) instance changes in one frame (those
still reset the GI history everywhere). Known transient: beside an arriving object the indirect
light overshoots for ~60 frames (the replaced probes hold a single-frame estimate whose noise the
clamped SH evaluation reads as a positive bias): 8.8 % peak on the moving-box reference against the
5 % gate G3e of `scripts/rendering/test_gdf_regional.py` (FAIL, documented). A lower blend weight
(`r.GI.RadianceCache.RegionBlendAlpha` 0.25) passes the box but fails G4 on Sponza (+6.5-8 %: a
loading event's box lasts one frame, the stale history is blended once and the feedback keeps it);
persisting the boxes for six frames and averaging the traces of the probes inside (tried 2026-10-06) gives 7.34 % against 6.51 % on the same-day build, so the overshoot is not single-frame noise; the next step is the per-probe transport readback on the arrival frames. A regional
change also still costs a full retrace of the probe grid that frame (~4x the budgeted trace). The diagnostic
`r.GI.GDF.CenterPhaseOffset` now shifts the crossing frames only, not the voxel grid.

**Mesh SDF bakes are asynchronous and cached (block B.3, 2026-10-05).** The preload used to bake
every field on the main thread with a `WaitIdle` per batch (Sponza: 22.65 s of CPU half and
5.62 s of GPU drain of a 37.4-38.0 s preload), marked the field valid before the GPU had run and
cached nothing. Now `MeshSdfBakeQueue` prepares the bakes on worker threads, records the GPU
half in the frame's command list (Z slabs in the render loop, unthrottled during the preload) and
marks the field valid when the frame fence passes; `cache/meshsdf` keeps the GPU readback
(LZ4, XXH64, keyed on the mesh bytes, the options and the bake shader) and a hit uploads the
same bits. The global distance field's instance batches are now sized by their entry count (a full
64 KiB batch per region and clipmap exhausted the 2 MiB frame ring when seven fields completed in one
frame) and the queue completes at most four fields per frame. Gates: C1 PASS (138 cache hits bit-identical to a fresh bake, volume and records); C2 as registered FAIL (warm preload 10.9 s against cold 19.3 s = 56 %: a 10.0 s texture floor the cache cannot touch) and C2' PASS (SDF-attributable preload 9.3 s -> 0.8 s); C3a PASS (no WaitIdle in the path); C3b PASS (CPU frame max 38.8 ms against a 26.9 ms median while the fields arrive after enabling SDF GI at runtime; GPU bake scope 0.04 ms per frame); C4 as registered FAIL (not bit-identical) and C4' 0.5-1.1 % (one ROI 0.08 % over its 1 % gate: cold and warm receive their fields on different frames and the radiance cache keeps its history outside the change boxes, block B.2); C5 as registered FAIL (the gain criterion was meaningless: with SDF GI off the channel holds the sky ambient) and C5' PASS (1.25 % from a reference with SDF GI on from the start); N PASS (a truncated entry is baked and rewritten). Sponza preload 37.4-38.0 s -> 19.3 s cold, 10.9 s warm. Open: a bake start costs
the main thread up to ~10 ms for a 128^3 field (resource creations and 12 MB into upload memory; one start
per frame in the render loop; big buffers are freed on a worker because a Debug-build free of 8 MB cost ~38 ms;
the `SDF bake service took` log line names any frame above 10 ms), a 128^3 field takes 16 frames at the default
`r.GI.MeshSdf.BakeSlicesPerFrame` (8), the cache has no eviction, procedural meshes still get no
field (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-block-b-03-async-sdf-bake.md`).

### Radiance cache probe classification — `ENABLED` 2026-10-05 (followup-01 6.4, H-72)

The current rule (field < 5 cm at the probe = inside) discarded free probes near faces and kept
probes inside solids and behind single-sided shells (H-65). The project configuration now uses
reachability from the camera (`r.GI.RadianceCache.ProbeClassification 3`), relocation of invalid
probes into reached air (`ReachRelocate`) and flood seeds in front of visible points
(`ReachVisibleSeeds`): the reference passes R3r / P1 / P3 / C, Sponza loses its leak through the
arcade wall (S2 unsupported 0 -> 0.11 %, means identical) and adds no motion step (S3). Known and
accepted: P2 of the reference reads +5 % on two lit faces against rule 0 (the control itself
under-lights them); S1 on the coarse QA field was retired. Defaults of the engine are unchanged
(0): the switch lives in `sample/projects/sponza.uzproject`.
The intermittent hang of H-71 has not recurred (38+ runs), and its cause is unknown: of H-71's two
suspects only the readback wait was changed (the radiance cache's two diagnostic readbacks follow the
frame fence; `DDGISystem::ReadDiagnosticResults` still calls `WaitIdle`, `ddgi_system.cpp:521`), the
G-buffer position read the cache pass declares for the visible seeds (`frame_gi_passes.cpp:285`) was not
examined, and the 10 s watchdog log covers only `DX12Device::WaitIdle`'s direct-queue wait and
`DX12Fence::WaitCPU` (`dx12_device.cpp:1285-1294`, `dx12_fence.cpp:83-95` at 8246ab5c): `WaitIdle`'s copy-
and compute-queue drains still wait `INFINITE` with no log (`dx12_device.cpp:1305-1308, 1319-1322`; flicker
ledger H-72b). Open, owner: phase0_lighting-rc-probe-support-followups (proposed at followup-01's
closure): a pass/fail regression harness for this configuration (`scripts/rendering/test_probe_classification.py`
covers rules 0 and 1 in the QA configuration only); a reachable probe on the far side of a wall can still
light it, since the cache query has no visibility test; P2 against an irradiance solution of the reference
instead of rule 0 (flicker ledger H-72); the hang above; and the dark-wall flicker candidate of ledger
H-62 / H-62b (the query's renormalised mean over a changing set of surviving probes), never measured.

### `IrradianceProbeGrid` / `LightProbeVolume` — `INERT` (reviewed 2026-10-08; was listed as `PARTIAL`)
**Files**: [irradiance_probe_grid.cpp](../../runtime/engine/renderer/gi/irradiance_probe_grid.cpp),
[light_probe_volume_component.h](../../runtime/engine/ecs/environment/light_probe_volume_component.h),
[frame_constants.cpp](../../runtime/engine/renderer/frame/frame_constants.cpp) (`:906-977`),
[deferred_lighting_ps.hlsl](../../runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl) (`:973-1028`)

The component and grid were added in `1892a567` (Phase 0 tasks 2.1/2.2) and wired in `29c83ba5`.
The renderer allocates one grid, for the first enabled volume only (`frame_constants.cpp:975`).
When the volume is loaded or created, every probe receives the same per-frame sky SH, after
`shScale` and the lower-hemisphere terms, converted to the world frame
(`FillFromSkySH(SHCubemapToWorld(...))`, `:936-947`).

The volume contributes nothing:
- `lpvIntensity` has been hard-coded to 0 since `29c83ba5` (`:967-972`), and the shader samples the
  volume only when it is above 0 (`deferred_lighting_ps.hlsl:978`).
- With a non-zero intensity it would still be source 3 of the one-source priority chain
  (`:739-754`), behind the radiance cache, which answers every pixel once it runs (`:968-969`).

The previous text of this entry was wrong on two points: the probes are not ECS entities (they live
in one structured buffer), and their irradiance is not zero (it is the broadcast sky SH).

Other gaps:
- `IrradianceProbeGrid::CaptureFromSkyCubemap` (`irradiance_probe_grid.cpp:193-216`) is never
  called.
- `autoFitScene` and `autoFitPadding` are read only by the serializer
  (`light_probe_volume_component.cpp:30-31`, `:47-48`).
- The header's default extents are Y-up (`light_probe_volume_component.h:39-49`).
- The grid has no explicit release at shutdown; only its destructor frees it
  (`scene_renderer_impl.h:403`, `irradiance_probe_grid.cpp:25-26`).
- No scene uses `light_probe_volume`. Sponza's `InteriorProbeVolume` was removed in `eb839ed9`.

Spatially varying indirect diffuse comes from the radiance cache, the SDF final gather and DDGI.
Whether to keep the volume (documented as inert) or remove it, which settles the gaps above, is open
decision N3, owner: `phase0_indoor-lighting-system` (see `docs/guides/indoor-lighting-guide.md`,
section 4).

**Tail disposition (2026-10-14):** retain the volume as documented inert infrastructure (N3).
Adding a `LightProbeVolumeComponent` is not a way to brighten an interior; use the active
radiance cache/DDGI paths or artist-authored local fill lights instead. The tail's render gates
must measure those active paths, not treat buffer allocation as proof of probe-volume lighting.

### SkyLight `intensity` / `indirect_lighting_intensity` / lower hemisphere — `OPEN DECISION` (2026-10-08, `phase0_indoor-lighting-system` N1/N2)
**Files**: [frame_constants.cpp](../../runtime/engine/renderer/frame/frame_constants.cpp) (`:418-454`),
[deferred_lighting_ps.hlsl](../../runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl) (`:714-715`),
[sky_light_component.h](../../runtime/engine/ecs/environment/sky_light_component.h)

The code splits the three settings between lighting paths, and no path reads all of them. The
component's comments describe neither split: `intensity` "scales both diffuse and specular IBL
contributions" (`sky_light_component.h:62-65`), and `indirectLightingIntensity` "scales only the
diffuse IBL contribution" (`:72-75`). The lower-hemisphere comment (`:98-106`) gives the UE meaning
(`true` replaces the lower hemisphere with the colour), while the code does the opposite: `true`,
the default, leaves the captured sky SH unchanged, and `false` adds the colour
(`frame_constants.cpp:446-454`).

- **N1.** `intensity` (times `r.SkyLight.Intensity`) scales:
  - the prefiltered specular and env-MIS;
  - the sky of the radiance-cache and DDGI writers;
  - the sky of the final gather and of the SDF reflections;
  - the height fog and cloud ambient;
  - the translucent IBL diffuse.

  It does not scale the sky-SH fallback diffuse. There `skyTintDiffuse = skyColor`
  (`deferred_lighting_ps.hlsl:714`), since `6543d8cf` (2026-04-08), and `shScale` carries only the
  indirect intensity (`frame_constants.cpp:428`). The local `skyIntensity` at `:321`/`:330` is never
  used, and the comment at `:423-427` wrongly says the shader applies the intensity. As a result,
  the diffuse ambient of every GI-off scene ignores `intensity`. The lights reference authors 0.05
  "so the frame is lit by the local lights alone" (`scripts/rendering/lights_reference.py:5-6`, `:107`), yet
  block B.7 measured a floor ambient of 0.632 there (`lighting-block-b-07-clustered-lights.md:69`).
- **N2.** `indirect_lighting_intensity` (times `r.SkyLight.IndirectIntensity`) and the
  lower-hemisphere colour act only on the packed sky SH (`frame_constants.cpp:428-454`). Only the
  sky-SH fallback reads that SH, plus the seed of the inert light probe volume. The radiance cache,
  DDGI, the final gather and the SDF reflections never see these settings, so they do nothing in
  GI-on scenes such as the Sponza project.
- **Related.**
  - On sky-SH-fallback pixels, the split-sum's multi-scatter share reads both settings
    (`deferred_lighting_ps.hlsl:1218-1219`, `:1339`).
  - With the radiance cache on and `r.GI.Reflections 0`, the same share multiplies the GI
    irradiance, whose sky already carries the intensity, by `skyLightColor * skyLightIntensity`
    again.
  - Fog and clouds read the raw SH L0 times the intensity, without the colour or the indirect scale
    (`frame_atmosphere_passes.cpp:123-125`, `:239-242`).
  - `indirect_lighting_intensity` 0 (or `r.SkyLight.IndirectIntensity 0`) does not darken the GI-off
    ambient: it zeroes the packed SH, `IsSHZero` (`runtime/shaders/runtime/lighting/ibl.hlsli:58-61`,
    which tests only the red channel's L1 terms) then reads the SH as empty, and the fallback
    samples the irradiance cubemap, scaled by neither intensity (`deferred_lighting_ps.hlsl:1040-1044`).

The three Sponza scenes have authored √3 in both settings since `8e3470f8`. Their GI-on image
therefore does not depend on N1, but it would change under N2 if the GI writers took the indirect
intensity. No harness measures any of this yet (task item 8.2). Evidence and options: the task's
`tasks.md` header and `docs/guides/indoor-lighting-guide.md`, section 2. Open, owner:
`phase0_indoor-lighting-system` (N1, N2 and the related items above).

**Tail disposition (2026-10-14):** document the N1/N2 split rather than change scene lighting.
Use strictly positive indirect-intensity values when measuring the fallback's scaling; zero
selects the cubemap fallback described above and is not an ambient-off control. A GI-on interior
must be checked through its consumed indirect-light/probe-support records, not by sweeping a
SkyLight setting that never reaches that path. Changing the split remains separate implementation
work; the documentation tail does not make these settings uniform across lighting paths.

---

## Local lights

### Clustered light lists and the light parameters — `DONE` 2026-10-07 (block B.7)
**Files**: [cluster_assign_cs.hlsl](../../runtime/shaders/runtime/lighting/cluster_assign_cs.hlsl),
[light_types.hlsli](../../runtime/shaders/runtime/lighting/light_types.hlsli),
[deferred_lighting_ps.hlsl](../../runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl),
[frame_lighting_pass.cpp](../../runtime/engine/renderer/frame/frame_lighting_pass.cpp)

Until 2026-10-07 the deferred lighting looped over every point (≤ 64) and spot (≤ 16) light of the
frame in every pixel, `cluster_assign_cs.hlsl` was a one-line stub, and the components' `sourceRadius`,
`lightFalloffExponent`, `specularScale`, `diffuseScale` and `useInverseSquaredFalloff` were packed into
the constant buffer and never read (nor read from scene files). Now `cluster_assign_cs` assigns the
lights to a 32-pixel-tile × 24-depth-slice grid (one thread per cluster, conservative world AABB,
range-sphere test, the loop's order so the sums are identical, an overflow bit beyond
`r.Lights.ClusterSlots`), the pixel evaluates its cluster's lights (`r.Lights.Clustered`, default 1;
`r.Lights.ClusterDebug` = heat map), the limits are 512 / 128 (LightingCB 43264 bytes), and the
parameters are live (custom falloff, scales, Karis sphere light for the source radius) and loaded
from scenes. Measured (`scripts/rendering/test_clustered_lights.py`, 8/9): identity with the
loop (max |diff| 0 with 80 lights, 0 with 500; C1 PASS, C1d PASS);
500 lights at 1080p 0.909 ms clustered against 1.800 ms with the loop, cull
0.070 ms (C2 FAIL); 80 lights 0.382 ms against the committed build's
0.465 ms; diffuse scale ratio 0.526 (C3a PASS), falloff exponent against the
formula median |error| 0.000 (C3b PASS), source radius peaks 2.944 / 0.965 /
0.379 (C3c PASS); Sponza unchanged (C4 PASS). Still open: spots are culled with their range
sphere (no cone test), the forward translucent pass and the froxel fog do not read the lists, local
lights do not reach the GI, shadows stay at 16 atlas tiles. See
[lighting-block-b-07-clustered-lights.md](lighting/uzengine/sponza-lighting-2026-10/lighting-block-b-07-clustered-lights.md).

## Shadows

### `r.DFS` (Distance Field Shadows) — `OFF` (STUB)

**2026-10-05 (block A.7):** the three `dfs_*_cs.hlsl` are one-line scaffolds and the lighting shader
never reads `dfsOutputSrvIndex`; the passes were dispatched for nothing. Default changed to `false`,
TechPanel row removed. The description below is the intended design, not the state of the code.

**File**: [frame_lighting.cpp:52](../../runtime/engine/renderer/frame/frame_lighting.cpp)
**Shaders**: [shaders/runtime/shadows/dfs_*.hlsl](../../runtime/shaders/runtime/shadows/)

Cone-traced distance field shadows. Works on large-scale scenes but can
produce splotchy noise on cm-scale interiors (low sample count, no temporal
accumulation). Currently ON by default; if you see splotchy AO-like noise
inside buildings, this is the first thing to disable.

### Local light (spot / point) shadows — `FIXED` 2026-10-05 (block A.1)

Until 2026-10-05 no spot or point light cast a shadow: the local atlas was cleared to 0.0 and drawn
with the CSM pipeline (`Less`), so no caster passed; spots then read the atlas with NDC used as UV and
the LESS comparison, which lit both sides of every occluder; point lights had six cube-face tiles
rendered per light and `pl.shadowIndex` was never read. Fixed with linear-distance tiles
(`shadow_depth_linear_ps.hlsl`, `SampleLocalShadow` in the lighting shader, cube-face selection).
Guard: `scripts/rendering/test_local_light_shadows.py` (6/6; before 4/6 with lit/shadow ratios 1.07 and 4.2).
Remaining: alpha-masked casters still cast solid shadows (the depth pipelines are vertex-only);
see `docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-block-a-2026-10-05.md`.

### Phantom default sun — `FIXED` 2026-10-05

A scene without a `DirectionalLightComponent` was lit by `LightFrameData`'s defaults (direction
0.5/0.3/0.8, colour 3/2.85/2.7) and shadowed by a CSM rendered for that light. The sun colour now
defaults to zero, `hasDirectionalLight` is false, the cascade pass is skipped (`cascadeCount` 0 =
full visibility) and the sky takes its direction from `SkyAtmosphereComponent::sunDirection`
(scene key `sun_direction`). Guard: `scripts/rendering/test_no_sun_lighting.py` (before 2/6, after 6/6 (dark region direct light 0.0, sun shadow visibility 1.0 on the whole ground, sky light 0.080 -> 0.004 with the sky sun below the horizon, local lights lit 0.358 / 0.385 with shadow 0.0)).
The same change moved the remaining block-A literals to CVars (`r.Shadow.Local.NormalOffset`,
`r.Shadow.Local.NearPlane`, `r.SkyLight.Rebake*`, `r.AutoExposure.AdaptationLinearRange`) and wired
`r.Shadow.AtlasResolution`, `r.Shadow.LocalAtlasSize/LocalTileSize`, `r.Material.MinGGXAlpha` and
`r.Material.DielectricF0`, which were registered and never read; see
`docs/analysis/hardcoded/01-rendering-defaults.md` (re-audit 2026-10-05).

### Shadow atlas depth dump — `STUB`
The pass-dump code skips depth textures other than the GBuffer depth. The
shadow atlas is D32Float and would need a depth → R8 copy path to be
dumpable. Not critical; shadow atlas can be inspected via RenderDoc.

---

## Reflections

### `r.SSR` — `OFF` by default (alive since 2026-10-07, block B.5)
**File**: [frame_lighting.cpp:44](../../runtime/engine/renderer/frame/frame_lighting.cpp)
**Shaders**: [shaders/runtime/postprocess/ssr_*.hlsl](../../runtime/shaders/runtime/postprocess/)

Full pipeline: tile classify → trace (Hi-Z, VNDF) → resolve → temporal → spatial denoise. Until
2026-10-07 the trace never found a hit anywhere: the Hi-Z pyramid it marches was built right
after the depth prepass, where the depth read as zero (the GBuffer pass clears the depth-stencil
before drawing), so every mip was zero and the "not sky" test failed for every sample. The
pyramid is now built after the GBuffer pass. Second cause: the temporal pass declared only its
velocity read, so the render graph (which orders passes only through declared resources) ran it
before the trace; the denoise then fed zeros back through the history. The temporal and the
prepare-indirect passes now declare their depth read. Third cause: the previous-colour copy the trace
samples (`SSR_CopyPrevColor`) declared nothing either and ran first in the frame, so every hit colour
was black; it and the legacy composite now declare their HDR read and run after TAA. The earlier "visible noise" note described an
output of nothing passed through the denoisers. The SSR is now blended before the BRDF over the
active reflection layer (SDF gather or prefiltered IBL), so Fresnel / DFG apply to it, and over the
environment-MIS specular on smooth metals (which replaces the split-sum term there); the
additive post-TAA composite is legacy (`r.SSR.Composite`, default 0, double-counts). Gates: R0-R5 7/7 (scripts/rendering/test_layered_reflections.py, 2026-10-07): mapping 91 % within 0.5 deg (median 0.08 deg), SSR layer median error 0.150 against F x reference (SSR off: 1.0), legacy composite 2.7 x the default on the reflecting floor (double count) while the default sits at 0.92 x, temporal stability unchanged, Sponza with SSR off: direct light identical to 8aeb8917 and colour within the run-to-run envelope, SSR passes 0.12 ms at 1080p. Note: the trace skips surfaces rougher than `r.SSR.MaxRoughness` (0.35); Sponza's
stone is above it, so SSR shows nothing there by design unless the cut-off is raised.
~~`r.SSR.MaxRoughness`, `NumSteps`, `StartMipLevel`, `SlopeToleranceScale`, `Composite` are each
registered twice (frame_gi_passes.cpp and frame_lighting.cpp / frame_postprocess.cpp).~~ Fixed
2026-10-08 ("CVars registered twice" under Rendering).

Open (found 2026-10-08 during the `phase0_sponza-followup-06` closure review): since 9f1be63f the
lighting pass hands the debug view `ssrSrvIndex = UINT32_MAX` whenever SSR is inactive, which is the
default (`frame_lighting_pass.cpp:163`). `--view-mode reflections` loads the SSR texture through that
index without testing it (`debug_view_ps.hlsl:60, 178-185`), unlike cases 8, 9 and 16, which break to
the "no data" magenta. The effect of the out-of-range descriptor is not established. Proposed fix:
the same sentinel test at the top of case 18. No task owns it.

### Environment-MIS specular — `FIXED` 2026-10-08 (block B.5b; was "black on metals", side finding of block B.5)
The environment-MIS estimate (`EnvMIS_Temporal`) replaces the split-sum specular on smooth metals and
came out black in every scene without an HDRI. Six defects: the compute dispatch never bound the
per-frame constants at b1 (camera position and sky tint were the previous compute pass's slot-1 data);
the estimator drew environment samples only but weighted them with the balance heuristic as if a BRDF
strategy existed (it converged to the environment half of the sum, ~0 on a mirror); the pdf lacked the
solid angle's sin(theta); the pass wrote alpha 1 on every lit pixel while the lighting replaced the
split-sum specular wherever alpha > 0 (every dielectric and rough metal lost its environment specular
with `r.GI.Reflections 0`); the sampler's directions were in the cube's Y-up frame used as world
directions (y and z swapped); and the alias table's accept test took the fractional part of
`u.x × texelCount` with a 16-bit `u.x` (always 0: the alias was never taken, the environment was
sampled uniformly over the even texels while the estimator divided by the luminance pdf, +25 % at
roughness 0.5). Now: environment + visible-normal BRDF strategies with the balance heuristic, the pdf
with sin(theta), the frame converted at the sampler's interface, the accept test on the second uniform
with 32-bit randoms, the PerFrameCB bound, the coverage in alpha and the lighting blending by it;
`r.EnvMIS` (default 1) switches it off. Measured (`scripts/rendering/test_env_mis.py`, 8/9): chrome
floor against F × the dumped environment along the reflected ray, median error 0.008 (E2m PASS);
roughness 0.3 / 0.5 metal floors against the CPU integral of the environment, median ratio 0.991 /
0.992 (E2 PASS / PASS; the split-sum path reads 3.255 / 4.850, its own item);
dielectric boxes keep the split-sum specular (0.0325, interior max |diff| to `r.EnvMIS 0`
0.0e+00; E3 PASS); the chrome floor under the ballroom PMREM, median error 0.029 (E4 PASS);
pass 0.219 ms at 1080p. E1 (the floor against the mirrored view's sky) reads ratio 1.714
(p10 1.705, p90 1.726): the sky capture's √3 below. Open: the lighting reads the previous
frame's estimate (one frame of latency; silhouettes under the TAA jitter), no reprojection of the
history (the EMA smears under motion), `OnResize` never called, a captured sun disc counted twice
(shared with the split-sum path), a fixed coverage band.

### Meshes exported counter-clockwise — `FIXED` 2026-10-08 (`sphere.uzmesh` and ten more; found in block B.5b)
In `chrome_test` the dump's visible positions on the sphere were the far hemisphere's (x median
-51.6 cm with the camera at x = 400) and the outward normals faced away from the camera (N·V
-0.70): the sphere is wound counter-clockwise while the engine renders clockwise-front, so back-face
culling kept the far side. `scripts/rendering/repair_mesh_winding.py` audits every sample mesh against the rule
of `test_decal_lod_normals.ps1` and flips whole-mesh inversions; it found and repaired 11 (the sphere,
seven Sponza meshes including `arch_stones_01_stone_trims_01_0` below, bunny, teapot, `test_shard`)
plus suzanne. Measured (`scripts/rendering/test_mesh_winding.py`, 4/4): the sphere's positions x median
72.186 cm, N·V 0.697; its specular against F × the PMREM along the reflected ray, median error
0.035 (block B.5b's E4 as first written); Sponza Camera C unchanged (the repaired meshes are out of
its view). The importer that produced them is not traced (`lighting-mesh-winding.md`).

### Depth prepass — `FIXED` 2026-10-07 (block B.4c; was "discarded by the GBuffer clear", block B.5)
The prepass never wrote a depth: it rendered with the shadow vertex shader whose view-projection
(`ShadowCB`, b4) the pass never bound, tested `LessEqual` against the reversed-Z clear of 0.0, and the
GBuffer cleared the depth anyway (0.167 ms per frame on Sponza for a bit-identical G-buffer). It is now
the GBuffer pass in a depth-only mode (`GBufferPassDesc::depthOnly`: the GBuffer's vertex shader and
constants, no pixel shader, `Greater` with writes, standard opaque draws only); the colour pass keeps
that depth and tests `Equal` without writes for standard draws, masked and material-graph draws keep
`Greater` with writes. The old pass and pipelines are gone. Measured (`scripts/rendering/test_depth_prepass.py`,
5/5): the G-buffer is bit-identical with and without the prepass on Sponza, the overdraw
stack and the masked wall (Z1 PASS, Z1b PASS); on the 48-wall overdraw stack the GBuffer costs
0.054 ms with the prepass (0.019 ms) against 0.180 ms
without (Z2 PASS); on Sponza prepass + GBuffer 0.524 ms against GBuffer alone 0.514 ms, so
`r.DepthPrepass` defaults to 0 (Z3 PASS). Open: no instanced depth-only path
(instancing is never active), two-phase occlusion culling still expects a Hi-Z before the GBuffer,
masked depth with an alpha texture untested (no such texture in the sample content).

---

### Reflection Probes — `PARTIAL`
**File**: [reflection_probe_renderer.cpp](../../runtime/engine/renderer/probes/reflection_probe_renderer.cpp)

`ReflectionProbeRenderer` gathers `ReflectionProbeComponent` entities from
ECS, manages dirty capture via `IBLBaker`, uploads a structured buffer of
`ReflectionProbeGPU`. The **renderer side is functional** — probes are
captured and bound — but:
- No parallax correction in the deferred lighting shader
- Probe selection is single-nearest, not blended
- No priority/weight field on `ReflectionProbeComponent`

---

## Raytracing (DXR)

### `r.RayTracing` — `OFF` (SCAFFOLDING)
**Files**:
- [rt_manager.cpp](../../runtime/engine/renderer/raytracing/rt_manager.cpp)
- [blas_manager.cpp](../../runtime/engine/renderer/raytracing/blas_manager.cpp)
- [tlas_manager.cpp](../../runtime/engine/renderer/raytracing/tlas_manager.cpp)
- [shader_table_builder.cpp](../../runtime/engine/renderer/raytracing/shader_table_builder.cpp)
- [passes/raytracing/rt_uv_test_pass.cpp](../../runtime/engine/renderer/passes/raytracing/rt_uv_test_pass.cpp)

Infrastructure exists:
- `RTManager` singleton; the TLAS is built only when the instance descriptors change (block
  B.6, 2026-10-07: `TLASManager::Rebuild` hashes the descriptors, skips an unchanged set, refits a
  same-size set in place, rebuilds otherwise; `r.RayTracing.TLAS.Reuse 0` = the previous per-frame
  rebuild). Sponza Camera C: `RT_BLAS_TLAS` 0.0006 ms with reuse against
  0.093 ms rebuilt every frame; over 126 frames 35 full
  builds (while the meshes stream in), 0 refits, 133 skipped.
  The refit bound (`kRefitsBeforeRebuild` 64) has not been measured on a moving scene.
- `BLASManager` builds per-mesh on first draw, cached
- `ShaderTableBuilder` for hit groups / miss shaders
- Only one RT shader: `rt_uv_test_lib.hlsl` — a barycentric UV visualizer

**Sub-CVars declared but not wired:**
- `r.RayTracing.Shadows`
- `r.RayTracing.Reflections`
- `r.RayTracing.AO`
- `r.RayTracing.GI`

Each would need: a ray gen shader, hit/miss shaders, dispatch integration
into the render graph, denoiser, composite into deferred lighting.

**Path forward**: the `diharaw/hybrid-rendering` paper is the reference
architecture — start with `RTShadows` (1 visibility ray/pixel vs sun
direction → SVGF-style tile-based denoise → multiply into direct sun term),
then `RTAO`, then `RTReflections`, then `DDGI` update rays.

---

## Path Tracing

### `r.PathTracer.Enable` — `OFF` (WORKING, PARTIAL FEATURES)
**Files**:
- [passes/pathtracing/path_tracer_pass.cpp](../../runtime/engine/renderer/passes/pathtracing/path_tracer_pass.cpp)
- [shaders/runtime/pathtracing/path_tracer_ps.hlsl](../../runtime/shaders/runtime/pathtracing/path_tracer_ps.hlsl)

Shadertoy-style compute path tracer as a runtime mode toggle. Steps 1-6
working (skeleton → GBuffer first hit → 1-bounce diffuse → direct sun +
multi-sample → GGX specular → progressive accumulation with camera-cut
reset). Full design in
[docs/analysis/path-tracing/](./path-tracing/).

**Known gaps**:
- **Step 7** (ECS analytic primitives for true multi-bounce against arbitrary
  geometry) deferred — currently all bounces escape to sky
- **Step 7 hybrid** (screen-space sun shadow) was implemented then removed
  because it was view-dependent (camera-relative shadow artifacts)
- **Step 8** (Falcor PSNR parity) only has a stub script at
  `scripts/falcor/uzengine_pt_reference.py`; Mogwai execution + scene
  export to `.pyscene` not done
- **No TAA interaction safety** — user must manually `r.TAA=0` when
  enabling PT (both do temporal accumulation)
- **No DLSS/FSR/TSR safety** — must manually disable upscalers
- **CB ring stale-buffer race** (tracked separately in MEMORY.md) can cause
  intermittent artifacts in accumulation

---

## Atmosphere / Sky / Fog

### Sky IBL (cubemap capture, SH, prefiltered maps) — `FIXED` 2026-10-05 (block A.5)

`m_skyIBLDirty` was set once at construction and only ever cleared, so a moving sun kept the sky IBL
of the first frame. The sky pass now re-bakes when the sun direction (> 0.25 deg), the sky sun colour
(> 1 %) or the angular radius change, at most once per 30 passes; each bake still stalls on `WaitIdle`
(asynchronous bake is the follow-up). Guard: `scripts/rendering/test_sky_ibl_rebake.py`.

### Sky cubemap capture — `FIXED` 2026-10-08 (block B.5c; was "√3 brighter than the sky pass", found in block B.5b)
`atmosphere_renderer.cpp` scaled the capture's sun by `skySunColor.Length()` (the length of the
illuminance vector, 17.32 for a white (10, 10, 10)) while the sky pass takes the per-channel
illuminance (`skyCB.sunIlluminance`) and the aerial perspective its x component. Measured with the
sampler's environment dump against the mirrored view's sky along the same directions (reflection
reference, block B.5b): the ratio sky pass / capture was 0.584 at every elevation, the shape identical.
Every image-based term samples the capture (the prefiltered specular, the sky SH, the environment-MIS
estimate, the GI probes' sky), so metals reflected a sky √3 brighter than the one behind them.
**Fixed at the user's decision (block B.5c, `lighting-block-b-05c-sky-capture.md`):** both capture
paths take the sun illuminance per channel (three root constants in place of the scalar) and scatter
it as the sky pass does. Measured (`scripts/rendering/test_sky_capture.py`, 5/5): the capture against the
mirrored view's sky along the same rays, ratio median 1.002 (p10 0.994, p90 1.007; S1 PASS);
the chrome floor against F × the mirrored view, median error 0.011 (S2 PASS; block B.5b's E1, 0.714
before); a warm sun (1.0, 0.7, 0.4) per channel r 0.999, g 0.997, b 0.991 (S3 PASS; 0.78 / 0.54 / 0.31 before);
Sponza's direct light identical, its `diffuse_indirect` at 0.894 × (S4 PASS); the PMREM floor
bit-identical (S5 PASS). The look of every atmosphere scene changes by design (the sky's share of
the image-based terms drops by 1/√3 for a white sun); re-tuning Sponza's calibration is the user's call.


### `r.AerialPerspective` — `OFF` (PARTIAL)

**2026-10-05 (block A.4):** the CVar was registered twice (`true` in `atmosphere_renderer.cpp`, `false`
in `frame_constants.cpp`); the registry keeps the first registration, so the effective default depended
on static-initialisation order. One registration now, default `false`.

**File**: [frame_constants.cpp:76](../../runtime/engine/renderer/frame/frame_constants.cpp)

3D aerial perspective volume (Hillaire 2020) is computed each frame via
`AerialPerspectiveVolume` compute pipeline, but the **deferred lighting
shader integration is gated off** by this CVar. When enabled, the fog
color blending is incorrect — suspect transmittance LUT UV mapping or
view-space Z reconstruction bug. Needs a side-by-side comparison vs. the
Hillaire reference shader.

---

### `r.ScatteringLut` — `OFF` (EXPERIMENTAL)
**File**: [atmosphere_renderer.cpp:37](../../runtime/engine/renderer/atmosphere/atmosphere_renderer.cpp)

Bruneton 2017 3D scattering LUT sky path. Shaders were stubs and got
deleted in the recent session; the C++ pipeline factories were also
removed. The CVar is still declared for compat but the code path is
effectively dead — sky uses the Hillaire 2020 transmittance LUT +
analytic ray march.

---

### Volumetric Clouds — `PARTIAL`
**Files**:
- [cloud_pass.cpp](../../runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp)
- Shaders deleted in recent session: `cloud_noise_shape_cs`, `cloud_noise_detail_cs`, `cloud_weather_cs`

Cloud raymarch compute works (see existing screenshots), but **the noise
volume bakers were stub shaders that got deleted**. The compiled `.bin`
noise assets under `content/atmosphere/` still exist and the renderer
reads them, so the runtime cloud pass works — but regenerating the noise
volumes requires rewriting the 3 baker shaders or baking externally.

Additional gaps:
- No temporal reprojection — clouds flicker
- No cheap LOD (always full quality)
- No cloud shadowing onto the ground
- No sun godray integration from clouds

User comment: "ainda precisa de vários ajustes, não é prioridade agora".

---

### Volumetric Fog — `PARTIAL`
**Files**:
- [volumetric_fog_pass.cpp](../../runtime/engine/renderer/passes/atmosphere/volumetric_fog_pass.cpp)
- Inject / scatter compute shaders exist

Froxel volumetric fog (160×90×64 inscattering volume, front-to-back integration, composite over
the HDR scene before the height fog). It runs only for a `height_fog` entity with
`enable_volumetric_fog`, which no shipped scene sets (`scripts/rendering/sponza_fog.py` and
`scripts/rendering/fog_shaft_reference.py` write test scenes). Until 2026-10-06 (block B.4) it could not have
worked: its constant buffer disagreed between C++ and HLSL (`float3 emissive` padded to 192 against
184), the froxels were reconstructed 5-60 cm from the camera over a grid stretched to 6.4 km (the fog
distance, in centimetres, multiplied by 100), the sun was applied twice and never shadowed. Now the
froxels sit on the camera rays at their slice depth, the grid ends at the component's distance
(64 m by default), the composite reads the volume at the pixel's view depth (the reversed-Z inverse;
`r.VolumetricFog.Debug 1` writes its depth / slice / transmittance for verification), the sun is
applied once and shadowed by the cascaded shadow maps (one tap per froxel,
`r.VolumetricFog.SunShadow`), and `scripts/rendering/test_cbuffer_layouts.py` compares DXC's layout against
`offsetof`. Gates: F1-F6 12/12 (scripts/rendering/test_froxel_fog.py, 2026-10-06): froxel placement, composite depth, sun once, Sponza shadowed floor 0.05, shaft reference lit rays 0.97 and rays crossing the shadow 0.270 against 0.269 predicted, 0.07 ms at 1080p, Sponza unchanged (DXIL identical, ROIs within the run-to-run spread). Still missing: temporal reprojection, point / spot injection, sky
pixels (discarded by the composite), the combination with the height fog over the same range, a unit
decision for the density (read per centimetre), and the scatter integrates along the view depth rather
than the ray length.

---

### `r.Fog` (Height Fog) — `ON`
Exponential height fog, Hillaire 2016 unified volumetric. Until 2026-10-06 its constant buffer
was misaligned: `float _fogPad2[2]` took two 16-byte rows in HLSL, shifting every field from
`skyAtmContribColorScale` on by 32 bytes, so the sun lobe pointed along the volumetric albedo
(1, 1, 1) and the sky / sun tints read other fields. Fixed by declaring the pad as two scalars
(block B.4; `scripts/rendering/test_cbuffer_layouts.py`). Scenes with a height fog look different from
that build on: the sun-facing glow follows the sun. `r.Fog` is registered twice; the pass is gated
by the pass inspector entry "fog", which `scene_renderer_init.cpp` seeds from `r.Fog` at start-up
(so the project CVar works, a runtime change does not).

### `LinearizeDepthReversedZ` (depth_common.hlsli) — fixed 2026-10-06 (block B.4b)
Documented as the reversed-Z inverse, it implemented the standard-Z one: near × far /
(far − depth × (far − near)) returned about the near plane for every reversed-Z depth. Now
near × far / (near + depth × (far − near)), the form GTAO and SSR carry inline; the fog composite and
the contact shadow march use it (`scripts/rendering/test_contact_shadows.py` CS1 round-trips it on the CPU, the
fog harness F2b reads it through the composite's debug output).

### Contact shadows (`contact_shadows.hlsli`, sun only) — alive since 2026-10-06 (block B.4b)
Until then the screen-space march never found an occluder: every linear depth came out as ~5 cm (the
function above), the hit window was 0.025 cm wide, the expected depth went negative at the second
step, and the march assumed the ray approaches the camera at its full length while stepping 16 ray
lengths across the screen. Verified: Sponza with `r.ContactShadow.Length` 0 and 0.01 was bit-identical
on 8851f37e. Rewritten: clip-space interpolation of the ray, per-sample comparison of the scene's view
depth with the ray point's (hit when a surface lies in front by more than 0.05 % and less than
`r.Shadow.ContactShadowThickness` of the depth). Gates: CS1-CS6 9/9 (scripts/rendering/test_contact_shadows.py, 2026-10-06): function round trip exact, before pair identical and bridged, 0.000 % false shadows on open lit floor, no gap at the box base (CSM already exact, agreement kept), Sponza 2.9 % of lit pixels darkened with 93 % at depth steps (1.5x the base rate), DeferredLighting +0.10 ms. In Sponza it darkens 2.9 % of the
sun-lit pixels, almost all the recessed bands of the far wall's 7 cm plaster relief under the raised
bands (a CPU replica of the march on the dump reproduces them). Still: sharp 16-step march (no dither,
no temporal filter), sun only, `r.ContactShadows` unread (`r.ContactShadow.Length 0` is the switch).

---

## Post-Process

### Auto-exposure knobs — `WIRED` 2026-10-05 (block A.8)

`r.AutoExposure.MidGrayTarget`, `SpeedUp`, `SpeedDown`, `LogLuminanceMin/Max` were registered and never
read (the shader hard-coded 0.18, 3, 1 EV/s, 10-90 %). They are root constants now; the defaults
reproduce the previous exposure. `r.ContactShadow.Length` is the contact-shadow ray length as a
fraction of the far plane (default 0.01 = the previously fixed length) instead of only switching it
on; `r.AutoExposure.AdaptationLinearRange` (1.5) replaced the last literal of the adaptation.

### `r.SkyLight.MinAmbient` — `DEFAULT CHANGED` 2026-10-05 (block A.2)

Default 0.25 was applied as `max(component, cvar)` to the AO clamp and the no-GI irradiance floor,
overriding every scene (Sponza authors 0.01; the shader documents 0.005-0.02). Default is 0 now (the
component value). Sponza Camera C: `lit_wall` -1.6 %, other ROIs bit-identical.


### `r.GTAO` — `OFF` (WEAK EFFECT / SCENE-DEPENDENT)
**File**: [gtao_pass.cpp:24](../../runtime/engine/renderer/passes/postprocess/gtao_pass.cpp)

GTAO pipeline (horizon-based AO + bilateral denoise + temporal accumulation
+ half-res upsample) is functional and dispatches correctly, but the
visual contribution is marginal in well-lit scenes because the directional
sun light + sky light dominate. Previously was disabled due to an
over-occlusion bug (MEMORY.md `project_ambient_light_zero_investigation`).

**Current state (post-2026-04-07 overnight session)**: the over-occlusion
bug hasn't been confirmed fixed, but enabling GTAO with intensity 0.5 /
radius 60 in Sponza produces visible crevice contact shadows. Needs
testing in more scenes, and PIXEL_DEBUG pass-dump validation that the
AO channel matches expected values.

**AO floor discontinuity (found 2026-10-08 by the `phase0_indoor-lighting-system` review).**
`deferred_lighting_ps.hlsl:506` uses the GTAO value only when it is above 0.01; at or below 0.01
the pixel keeps the G-buffer AO (usually 1). A fully occluded GTAO texel therefore reads as
unoccluded instead of reaching the `minOcclusion` floor (`:512`). The jump at 0.01 has been there
since `a612d736` (2026-04-03). Open, owner: `phase0_indoor-lighting-system` (recorded under its
item 8.2).

---

### `r.DOF` — `OFF`, `STUB` (UNWIRED) (corrected 2026-10-08; was `PARTIAL`, "works as a cheap gaussian blur")
**CVar**: [frame_postprocess.cpp:32](../../runtime/engine/renderer/frame/frame_postprocess.cpp)
**Shaders**: `dof_setup_cs.hlsl`, `dof_blur_cs.hlsl` — one-line empty compute bodies since `bd4db199` (2026-03-31)

No pass dispatches depth of field: the post chain that did was removed as dead
code by `phase0_sponza-followup-08` item 4.4, `ctx.dofActive`
(`frame_setup.cpp:466`) is computed and read by nothing, and the `r.DOF*` CVars
say "(inert: ...)". Re-implementation: open, owner:
`phase3a-3.50.0-ue5-visual-parity-roadmap` item 2.9 (pending; its 2.9.1 tick
predates `bd4db199`).

---

### `r.LensFlare` — `OFF`, `STUB` (UNWIRED) (corrected 2026-10-08; was "builds, runs")
**File**: [lens_flare_pass.cpp:30](../../runtime/engine/renderer/passes/postprocess/lens_flare_pass.cpp)
**Shader**: `lensflare_cs.hlsl` — a one-line empty compute body since `bd4db199` (2026-03-31)

`LensFlarePass` is created and initialised (`scene_renderer_init.cpp:639-640`)
but never executed: nothing calls `LensFlarePass::Execute` (the post chain that
did was removed by `phase0_sponza-followup-08` item 4.4). Open, owner:
`phase3a-3.24.0-lens-flare` (pending; its items 1.1-1.3 and 1.5, ticked on
2026-03-19, predate `bd4db199`).

---

### `r.MotionBlur` — CVar `ON` by default, `STUB` (UNWIRED) (corrected 2026-10-08; was "works correctly")
**CVar**: [frame_postprocess.cpp:31](../../runtime/engine/renderer/frame/frame_postprocess.cpp)
**Shaders**: `motion_blur_cs.hlsl`, `motionblur_tilemax_cs.hlsl`, `motionblur_neighbormax_cs.hlsl` — one-line empty compute bodies since `bd4db199` (2026-03-31)

Nothing is blurred. No pass dispatches these shaders (the post chain that did
was removed as dead code by `phase0_sponza-followup-08` item 4.4);
`ctx.motionBlurActive` (`frame_setup.cpp:460`) is computed and read by nothing;
`r.MotionBlur.MaxRadius` and `.TargetFPS` say "(inert: pass not wired)".
Re-implementation: open, owner: `phase3a-3.50.0-ue5-visual-parity-roadmap`
item 2.8 (pending; its 2.8.1 tick predates `bd4db199`).

---

### Distortion (Heat haze / refraction) — `STUB` (UNWIRED)
**Files**:
- [distortion_pass.cpp](../../runtime/engine/renderer/passes/vfx/distortion_pass.cpp)
- [distortion_resolve_pass.cpp](../../runtime/engine/renderer/passes/vfx/distortion_resolve_pass.cpp)
- Shaders: `distortion_ps.hlsl`, `distortion_haze_ps.hlsl`, `distortion_resolve_ps.hlsl`

The pass classes are implemented and `distortion_haze_ps.hlsl` has a body, but
`distortion_ps.hlsl` and `distortion_resolve_ps.hlsl` are one-line pixel shaders
returning opaque black (since `bd4db199`). The engine compiles neither; only the
VFX viewport demo builds a fullscreen resolve PSO from `distortion_resolve_ps`
(`runtime/demos/vfx_viewport/vfx_viewport_demo.cpp:551`) (corrected 2026-10-08).
**There is no wiring in `frame_postprocess.cpp` or `frame_particles.cpp` that
calls `AddDistortionAccumulationPass` or `AddDistortionResolvePass`**. Scene VFX
entities that flag themselves as distortion emitters will NOT render any
refraction until the pass is added to the frame graph. Open, owner:
`<stub-shader task>`.

---

## Materials

### Named pins + MaterialParams — `DONE`
Recent session added UE5/Godot-style named pin serialization (`dstPin`,
`srcPin` as strings) and `MaterialParams` (baseColorTint, roughnessMul, etc).
Translator emits explicit defaults for unconnected pins. Documented in
commit `eb839ed9`.

### `specularMultiplier` — `PARTIAL`
`MaterialParams::specularMultiplier` is declared and serialized but NOT
consumed by the HLSL translator yet (stub `(void)mp.specularMultiplier`).
Needs either an extra out parameter on `ComputeMaterialOutputs()` (requires
changing the gbuffer RT layout to carry per-pixel F0 scale) or baking it
into the baseColor at authoring time for dielectrics.

### HDRI `.hdr` / `.exr` loading — `DONE`
PMREM atlas + raw equirect paths both work. No known bugs.

---

## Subsurface Scattering / Hair / ClearCoat — `STUB`
Shading models `Subsurface`, `ClearCoat`, `Hair`, `ThinTranslucent` are
declared in `MaterialShadingModel` enum and space is reserved in the
GBuffer RT2.w `customW` channel, but:
- No specialized BRDF for any of them in `deferred_lighting_ps.hlsl`
- No authoring support in the material graph compiler
- No scene-side validation

---

## Skeletal Animation — `PARTIAL`
**Files**: [engine/animation/](../../runtime/engine/animation/)

`Skeleton` struct, `BoneTransform`, `.uzskel` file I/O all exist. What's
missing:
- Animation clip / `.uzanim` loading
- State machine / blend tree
- GPU skinning compute shader (currently no bone palette upload)
- Animation-driven morph targets
- IK solvers (FABRIK, CCD)

Listed in the Phase 3 task list but never implemented. ECS has no
`SkeletalMeshComponent` yet.

---

## VFX / Particles

### Particle system — `PARTIAL` (MOSTLY FUNCTIONAL)
Spawn/update/render compute pipeline works. Mesh particle rendering
works. Trails work. Known gaps:
- **Collision** via SDF exists (the `SceneSDF` data interface); its CPU pieces are unit-tested:
  the analytic sphere/box SDFs and their central-difference gradient
  (`runtime/tests/engine/vfx/test_vfx_scene_sdf.cpp:61-118`) and the collision push-out with
  hand-fed distance/gradient values (`:124-179`). The earlier "only tested against Brixelizer
  global SDF" was wrong: no VFX code references Brixelizer.
- **Vector fields / curl noise** helpers exist but no integration test
- **Ribbons / trails** interpolation has a minor popping artifact at
  segment transitions
- **GPU sort** works for alpha-blended particles but no frustum cull yet

### Split-sum specular on rough metals — `FIXED` 2026-10-08 (block B.5b's finding)
The prefiltered-cubemap specular read 3.3 x / 4.9 x the CPU integral of the GGX BRDF on the roughness
0.3 / 0.5 metal floors (block B.5b). Two causes: the lighting picked the mip with the Three.js PMREM
curve (`r(2-r) x maxMip x 1.2`) while the engine's bake distributes roughness linearly over the mips
(roughness 0.5 read a mip baked for 0.9), and the sky SH was projected in the cubemap's frame (+Y up)
but evaluated with the world normal, so every surface read the irradiance of the direction with y and
z swapped (an upward normal got the horizon's grey 0.72 instead of the sky's (0.07, 0.13, 0.24));
the multi-scatter term of the split-sum uses that irradiance and every scene without a GI probe source
lights its ambient with it. The CPU packing also copied UE5's signs (whose basis negates y, x, yz
and xz) onto the engine's positive Ramamoorthi basis, so the irradiance field was rotated 180 degrees
about the cube's z axis (with the frame fixed, an upward normal read the ground, 1.33), and the
lower-hemisphere colour injected into the SH was written into the red channel's L2 band with Y00 / 2 of
the colour. Now: the engine's bake is read linearly (a PMREM keeps its curve, bit 8 of the per-frame mip
count), the SH is evaluated with `WorldToCubemap(N)` and packed with the basis's own signs
(`SHCubemapToWorld` for the light probe volume, which the shader evaluates with the world normal; 3
gtests), the injection writes L / 2 and -L / 2 into the constant and the up terms;
`EnvMapSampler::DumpRadiance` dumps the prefiltered mips for a CPU check. Measured
(`scripts/rendering/test_split_sum.py`, 7/10): roughness 0.3 / 0.5 floors split-sum / integral
1.309 / 2.331 (P1 FAIL / FAIL); the engine against a CPU model of its own
approximations 0.967 / 0.994 (P1b PASS / PASS): what remains above the integral is the
split-sum's N = V = R assumption at grazing incidence (NoV [0.00,0.20) 3.17 x, NoV [0.20,0.35) 2.58 x, NoV [0.35,0.50) 2.04 x, NoV [0.50,0.70) 1.75 x), not the implementation; box
tops' irradiance against E / pi per channel [1.032, 1.015, 0.985] (P7 PASS), the bake's mips against a CPU prefilter
(P8 PASS). Open: the split-sum's excess at grazing incidence on rough metals (a horizon term for
metals is the usual mitigation; Three.js skips it for them), the PMREM curve against Three.js's
convention (the rough PMREM floors read 1.149 / 0.396 of the integral), the bake's source-mip
bias against the output size.

### Cluster spot cone — `FIXED` 2026-10-08 (block B.7's open item)
The light cull assigned spots by their range sphere alone. Now a conservative cone test against the
cell's bounding sphere (Wronski 2017) after the range test, `r.Lights.ClusterSpotCone` (default 1);
the culled lights contributed exactly 0, so the lighting is bit-identical (`scripts/rendering/test_cluster_spot_cone.py`,
4/4: identity K1 PASS; mean cluster light count on the dense scene 126.63 → 66.88,
K2 PASS; LightCull 0.070 → 0.070 ms, K3 PASS; DeferredLighting 0.897 → 0.714 ms, K4 PASS).

### CVars registered twice — `FIXED` 2026-10-08 (block A.8's list)
Nine names were registered in two translation units (the `r.SSR.*` five, `r.Fog`, `r.Clouds`, `r.SSGI`,
`r.RayTracing.GI`); the registry keeps the first, so the default and range depended on static-init
order. The never-read copies are gone, `r.RayTracing.GI` is read through the registry in its second
file, and `scripts/rendering/test_cvar_registrations.py` (every name once, comments stripped, a duplicate fixture
as the negative control, no registration in a header) guards it (`lighting-cvars-once.md`).

### Mesh decals (Sponza's dirt quads) — `FIXED` 2026-10-08
Sponza's two dirt decal meshes carried a `Masked` graph (`dirt_decal.uzmat`, `alphaClipThreshold: 0`, which
the loader turns into 0.3333), so every texel above a third of opacity was drawn opaque with the texture's
flat colour, writing depth, normal and position and occluding shadows and the distance field: grey blotches
instead of soft dirt (the user's report). A material graph with `domain: Decal` on a mesh is now a mesh
decal: drawn after the opaque geometry with the albedo target blended SrcAlpha / InvSrcAlpha by the graph's
opacity (the AO kept), the other targets and the depth untouched, out of the shadow and distance-field sets
(`materialFlags` bit 4; `lighting-sponza-mesh-decals.md`). Measured (`scripts/rendering/test_mesh_decals.py`,
5/5): on the reference ramp the lit colour is lerp(wall, decal, alpha) to 0.012 (R² 1.000;
D1 PASS), the positions under the decal sit on the wall (|y| 0.029 cm; D2 PASS), no shadow (D3 PASS);
Sponza Camera A: positions changed only inside the decal bounds (D4 PASS), the footprint's boundary
contrast 0.530 → 0.120 (D5 PASS). Open: only the albedo blends (roughness
and normal stay the stone's); the reflection-probe capture still draws the quads opaque; the TLAS still
holds them as opaque instances, as it holds every translucent draw (`TLASManager::Rebuild` takes every
draw command, `tlas_manager.cpp:76-81`), so the DXR GI writer traces occluders the distance field and the
shadows do not have, the decals since 513e2983 and translucent draws always (open, owner:
phase0_lighting-ddgi-sdf-writer-parity-head, proposed at followup-01's closure).

**Addendum (2026-10-08, the Sponza recalibration item):** the dirt-decal mesh had also been a
distance-field occluder. `decals_1st_floor1_dirt_decal_0` (7036 vertices, bounds spanning the atrium)
as a Masked mesh in the two-sided 128³ mesh SDF darkened Sponza's diffuse indirect by 43 %: the chain of
Camera C captures puts × 1.69 (Camera A × 1.39) at the mesh-decal commit, uniform over the frame, and
with the material back to the Surface domain on the same binary the indirect reads 0.571 × and the
specular 0.34 ×. The look Sponza had been calibrated to included that occluder
(`lighting-sponza-recalibration.md`). Open: thin open "sticker" meshes in the two-sided mesh SDF have no
guard in the bake.

### Decals — `DONE`
Deferred decal system (DBuffer) works. Decal material graph integration
works. No known issues.

---

## Input / editor navigation

### Editor free camera (F5) and the input frame boundary — `FIXED` 2026-10-09 (8fb13fa1; found 2026-10-08 by the parity-06 closure review)
**Files**: [frame_setup.cpp](../../runtime/engine/renderer/frame/frame_setup.cpp), [frame_overlay.cpp](../../runtime/engine/renderer/frame/frame_overlay.cpp), [scene_renderer_init.cpp](../../runtime/engine/renderer/scene/scene_renderer_init.cpp)

From reading the sources (not from a run):
- **The F5 free camera cannot be moved.** F5 pushes an `EditorFreeCam` (`frame_setup.cpp:287-303`),
  but the fly controller runs only while the active camera id is 0 (`:339`) and `fly_camera.lua`
  moves only the ECS camera, so WASD and the right mouse button do nothing to it;
  `docs/analysis/rendering/sponza-test-handoff.md:45` promises F5 + WASD. SponzaTest also runs the
  controller for the free camera and re-initialises its yaw and pitch (`InitFromCamera`) on the push
  and the pop.
- **With `r.ShadowDebug` 1 every key toggle fires again each frame.** The only
  `InputSystem::EndFrame()` runs inside the OverlayPass (`frame_overlay.cpp:396`), which is not
  registered when `r.ShadowDebug` is 1 (`:188`), so press edges and the scroll / raw deltas are never
  cleared and `IsKeyJustPressed` stays true (F5, F, G, the number keys, Lua
  `uz.input.is_key_just_pressed`). SponzaTest ends the input frame unconditionally at the end of
  `RenderFrame`; merge `31b5c14a` dropped that call as a duplicate.
- The C++ fly controller moves at 5 cm/s (`scene_renderer_init.cpp:177`) in the hosts that never call
  `SetMoveSpeed`: the standalone `uzengine.exe` and the runtime C API viewport of the VS Code scene
  editor (`runtime/engine/app/runtime.cpp`); the `Application` host applies
  `AppConfig::cameraMoveSpeed` (500, `application.cpp:174`).

**Fixed in 8fb13fa1 (2026-10-09):** `InputSystem::EndFrame` runs at the end of `RenderFrame`; the
fly controller drives the F5 free camera, with `InitFromCamera` on the push and the pop; the standalone speed is
500 cm/s; `SetModifierState` records edges. `scripts/rendering/test_editor_navigation.ps1` passed 75/75 (9
EditorNavigation + 66 input). parity-06 closed.

---

## Lua Scripting

### Base API — `DONE`
LuaJIT integration, sandboxing, hot-reload, `uz.scene` / `uz.mesh` /
`uz.camera` / `uz.material` / `uz.light` APIs all work.

### `uz.pathtracer` — `STUB`
Listed in the path tracer milestone plan but the Lua binding file
`runtime/engine/scripting/api/lua_pathtracer.cpp` does not exist yet.
The C++ side is accessible only via CVars.

### Coroutines / DAP server / LSP IntelliSense — `NOT STARTED`
Planned in Phase 1 specs, no implementation.

---

## RHI Backends

### DX12 — `PRIMARY`
Fully functional, bindless SM6.6+, DXR support detected. All development
happens here.

### DX11 — `PARTIAL`
Files in `runtime/engine/rhi/dx11/` compile but path hasn't been validated
recently. Bindless not supported (SM5 fallback paths exist in shaders).
Used occasionally for backend parity testing; breaks regularly.

### Vulkan — `PARTIAL`
13 files in `runtime/engine/rhi/vulkan/`. SPIRV-Cross wrapper for HLSL →
SPIRV transpilation is set up. Never booted end-to-end. Listed as a
future target in the architecture docs.

### Metal — `STUB`
`metal_device.mm`, `metal_command_list.cpp`, etc. exist but are
Windows-built stubs (no macOS CI). Assume broken until validated on Mac.

---

## Other scaffolded-but-unused

### `Job System` — `STUB`
`engine/job/` directory exists. No current consumers. Renderer is 2-thread
(game thread + render thread) with no task-based parallelism.

### `Async streaming` — `STUB`
Spec exists in `docs/specs/`, no implementation.

### `Undo/Redo` — `NOT STARTED`
### `Editor gizmos` — `NOT STARTED` (beyond the base grid + outline)
### `Multi-select` — `NOT STARTED`
### `Prefab edit mode` — `NOT STARTED`
### `Navmesh` — `NOT STARTED`
### `Plugin system` — `NOT STARTED`
### `Terrain system` — `NOT STARTED`
### `Audio (XAudio2)` — `NOT STARTED` (spec only)
### `Physics (Jolt)` — `NOT STARTED` (spec only)

All of these have specs in `docs/specs/` and tasks in `.rulebook/tasks/`
but no code yet.

### One-line stub shaders — `STUB`, 29 files (audit 2026-10-08)

Requested by `phase0_sponza-parity-01` (notes.md:130-150) so that "the pass is
wired up" is not read as "the pass works". Each file has been a single line since
`bd4db199` (2026-03-31): the compute shaders are
`[numthreads(8,8,1)] void main(uint3 DTid:SV_DispatchThreadID){}` (they write
nothing), the pixel shaders `return float4(0,0,0,1);` (opaque black). The
2026-09-07 sweep found 32; filled since: `mesh_sdf_bake_cs` and
`global_sdf_composite_cs` (parity-03, committed in `7eea6b95`) and
`cluster_assign_cs` (block B.7, `58340259`).

- Compiled by the engine through `EngineShader::` (20): `atmosphere/sky_cubemap_ps`
  (SM5 fallback of `sky_cubemap_cs`), `compute/gpu_cull_cs` (`r.GpuCull`),
  `debug/pixel_debug_cs`, `debug/vis_fullscreen_ps` (pass-inspector visualisation,
  `frame_overlay.cpp`), `gbuffer/gbuffer_debug_ps`, `postprocess/color_grading_lut_cs`,
  `postprocess/dof_setup_cs`, `postprocess/dof_blur_cs`, `postprocess/fsr_easu_cs`,
  `postprocess/fsr_rcas_cs`, `postprocess/lensflare_cs`, `postprocess/motion_blur_cs`,
  `postprocess/motionblur_tilemax_cs`, `postprocess/motionblur_neighbormax_cs`,
  `postprocess/smaa_edge_ps`, `postprocess/smaa_blend_ps`, `postprocess/smaa_resolve_ps`,
  `postprocess/tsr_resolve_cs`, `shadows/dfs_cone_trace_cs`, `shadows/dfs_cull_objects_cs`.
- Compiled only by the VFX viewport demo (1): `vfx/distortion_resolve_ps`
  (`runtime/demos/vfx_viewport/vfx_viewport_demo.cpp:551`).
- Named only in comments (4): `postprocess/bloom_downsample_ps`, `bloom_blur_h_ps`,
  `bloom_blur_v_ps`, `bloom_composite_ps` (`postprocess_stack.h`; the live bloom is the
  compute chain `bloom_threshold_cs` / `bloom_downsample_cs` / `bloom_upsample_cs`).
- Not referenced (4): `compute/draw_compact_cs`, `compute/mipgen_cs`,
  `shadows/dfs_upsample_cs`, `vfx/distortion_ps`.

The entries above for `r.TSR`, `r.FSR`, `r.SMAA`, `r.DFS`, `r.DOF`, `r.LensFlare`,
`r.MotionBlur` and Distortion describe the features these files leave
unimplemented. Repro:
`find runtime/shaders -name '*.hlsl' | while read f; do [ "$(wc -l < "$f")" -le 2 ] && echo "$f"; done`

---

## Build / Tooling

### `--diag` per-pass timing — `FIXED` 2026-10-05 (lighting block B.1)

`DIAG_JSON` printed `"timing": []` on every run: the GPU profiler was never enabled for `--diag`
and `PassInspector::SetTimingData` had no caller. Three defects in the existing DX12 path would
have made the numbers wrong once read: the readback ring was as deep as the command-list ring
(3), so the slot of frame N-2 was resolved while that frame could still execute; one shared name
table labelled frame N-3's timings with frame N's pass list; the multi-scatter passes registered
`std::string` names that were freed before execution. Now `--diag` (and `--gpu-profile`) enable the
profiler, the report carries mean / min / max ms and samples per pass plus the GPU frame span,
the ring has 4 slots with per-slot names and counts, and the per-frame log is opt-in
(`r.GpuProfiler.Log`). Guard: `scripts/rendering/test_diag_timing.py` (before 2/7, after 7/7). Only DX12
implements the `IRHIDevice::GpuProfiler*` calls; the DX11/Vulkan/Metal profiler classes exist but
are not reachable. Still open: the FPS printed by `LogRenderStats` divides by a frame count that
includes the preload frames.


### Runtime leaks — FIXED (2026-04-07)
`PT_Accum0` / `PT_Accum1` textures leaked at shutdown, triggering D3D12MA
assert. Fixed in commit `cb9f4028`. If similar "Unfreed committed allocations"
errors appear for other textures, grep for their creation site and ensure
`DestroyTexture` is called in the matching shutdown path.

### Shader cache — `STABLE`
DXC-based pipeline cache at `runtime/build/bin/cache/shaders/`. Clearing
it (`rm -rf`) forces full recompile on next boot — useful when shader
includes change without touching the top-level file.

### PSO disk cache — `STABLE`
Pipeline state objects are serialized to disk for fast boot on subsequent
runs.

### Pass-dump — `IMPROVED (2026-04-07)`
Recent session added `DumpChannel` for split views (gbuffer.ao, metallic,
roughness from packed RTs) and enabled gbuffer depth dump via a sqrt curve.
Volfog inscattering/integrated outputs are now registered; the Brixelizer
diffuse/specular targets listed with them register only for an initialised
pass (`frame_lighting.cpp:348-359`), so never since 2026-04-10. Shadow atlas
depth still skipped.

---

## Follow-up priority (personal recommendation)

1. **Fix TAA** — unblocks DLSS ghost-fix, TSR tuning, and path tracer
   accumulation coordination. Highest cross-system impact.
2. **RT Shadows via inline ray query** — `diharaw/hybrid-rendering` pattern.
   Replaces the view-dependent contact shadow march. Most visible quality
   upgrade.
3. **Rewrite SSGI march** — or formally retire it in favor of the world-space
   SDF final gather (`r.GI.FinalGather`), the diffuse GI that runs on Sponza.
   (Brixelizer GI, named here on 2026-04-07, has been dormant since then; its
   task was closed as superseded on 2026-10-08.)
4. **Wire distortion passes** into frame_postprocess — zero new code,
   just graph registration. Unlocks heat haze / refraction VFX.
5. **GTAO validation + AO-map authoring path** — for when assets actually
   ship with baked AO.
6. **Skeletal animation Phase 1** — GPU skinning compute + clip playback.
   Unblocks characters in the engine.
7. **Path tracer Step 7 ECS primitives** — analytic Cornell-style test
   scene, serves as BRDF validation reference against Falcor.
8. **DLSS Ray Reconstruction + DLSS-G** — once TAA / motion vectors are
   fixed.
9. **Backend parity** — validate DX11 / Vulkan builds compile + boot.
10. **Audio + Physics** — large unblocked deliverables for the next phase.

### Capture reproducibility: the pre-load length chooses a persistent TAA state — `FIXED` 2026-10-09 (36951b4e; found and fixed the same day)
**Files**: [uzengine.cpp](../../runtime/tools/uzengine.cpp) (pre-load loop), [mesh_sdf_bake_queue.cpp](../../runtime/engine/renderer/gi/mesh_sdf_bake_queue.cpp)

Separate processes of one build and one configuration take 43 or 44 frames to pre-load Sponza on the Debug
build, depending on worker-thread timing, and a process with the other length lands in a different TAA
state that persists (ledger H-73): up to 0.19 linear on the correction-gates ROI and 80 code values on the
whole frame, while the TAA input agrees to 4e-05. It is the floor of every cross-process comparison in the
flicker programme and the reason followup-09 and followup-14 stay open. Also measured: the frames depend on
the allocation layout. With the RT UV test pass failing to initialise (one 1920x1080 UAV and two descriptors
fewer), the reflection, fog-shaft and path-tracer references moved by one code value at 1 to 997 pixels while
the local-shadow, decal and HDRI references did not (render-graph state harness, 2026-10-09).
**Fixed (pre-load):** in the pre-load the bake queue waits for its worker preparations and starts the bakes in
mesh order (`r.GI.MeshSdf.PreloadWait`, default on; docs/analysis/rendering-arch/flicker/capture-preload-determinism.md): every
process pre-loads Sponza in 38 frames, under CPU load too, followup-09 passes 23/23 and followup-14 11/11.
**Still open:** the allocation-layout dependence and a residual of one code value at a few pixels between
processes, owner `phase0_frame-allocation-layout-dependence`.

### SSR exposure ratio on a linear scene colour — `OPEN` (found 2026-10-09)
**Files**: [frame_gi_passes.cpp](../../runtime/engine/renderer/frame/frame_gi_passes.cpp), [frame_postprocess.cpp](../../runtime/engine/renderer/frame/frame_postprocess.cpp), [ssr_trace_cs.hlsl](../../runtime/shaders/runtime/postprocess/ssr_trace_cs.hlsl)

The SSR trace multiplies the previous frame's colour by `currentExposure / m_ssrPrevExposure` (the AMD
FidelityFX SSSR pattern, which assumes a pre-exposed scene colour). This engine's HDR target is linear and
not pre-exposed: changing `r.Exposure` leaves the TAA output unchanged (followup-11 step 3, 2026-10-09). So
while auto-exposure adapts, every SSR hit is scaled by the per-frame exposure change (a few per cent per
frame at the default 3 EV/s), and at a steady exposure the ratio is 1. From reading the code, not measured;
SSR is off by default (`r.SSR` 0). Open, owner: `phase0_ssr-exposure-correction`.

### Cross-frame resource states (render-graph imports and manual barriers) — `OPEN`, stage 0 done (2026-10-09)
**Files**: [render_graph_states.h](../../runtime/engine/renderer/render_graph/render_graph_states.h), [render_graph_builder.cpp](../../runtime/engine/renderer/render_graph/render_graph_builder.cpp), [frame_setup.cpp](../../runtime/engine/renderer/frame/frame_setup.cpp)

Barriers name a state the resource is not in: the graph takes each import's state on the caller's word and
the DX12 RHI copies it verbatim, while only four resources carry their end state across frames (audit of
2026-10-08: 80 of 89 claims confirmed). The registry's observe mode lists them on every run: the froxel
volumes, the Light_* targets, velocity with albedo's state, the local shadow atlas, the AP volume on HDRI and
path-traced frames, the path tracer's accumulation, the DBuffer, the first LUT and sky fills, and HDR plus the
G-buffer after the render-loop resize (docs/analysis/rendering-arch/render-graph/render-graph-states.md). NVIDIA tolerates them; the
D3D12 debug layer cannot run on this workstation to report them. Stage 0 (the registry, observe only) shipped
in 9735d401; stages 1-3 (tracked imports, the missing reads, the manual barriers) are open, owner:
`phase0_render-graph-state-registry`.

## GDF incremental - transient-arrival ghosting (G3e), 2026-10-14

`phase0_lighting-block-b-02-gdf-incremental` shipped its regional knock-on (item 4.1); the
transient-arrival END-strip peak gate **G3e stays at 8.83 % (target <= 5 %)** as an accepted
limitation. The optimization that targeted it (4.3: persist each change box a few frames and
retrace only its probes at a decaying blend) was tried 2026-10-06 and made G3e WORSE
(7.34 % vs 6.51 % HEAD), so it was reverted. Research follow-up, not yet pursued: per-probe
transport readback on the arrival frames (measure which probes actually received transport the
frame the object arrives, and retrace only those) rather than a geometric change-box blend.

## Brixelizer GI enabled; numerical coverage unverified (2026-10-14)

The formerly dormant opt-in path is now built and running. SDK DX12 Release libraries exist at
`runtime/thirdparty/amd-fidelityfx-1.1/sdk/bin/ffx_sdk/`, and the MSVC wrapper exists at
`wrapper/build/bin/uz_brixelizer.dll` and is deployed to `runtime/build/bin/uz_brixelizer.dll`.
The loader binds all 18 exports. Both real contexts create on NVIDIA GeForce RTX 5080.

The context-creation error was diagnosed: `ffxBrixelizerContextCreate` returned
`-2147483635` (`FFX_ERROR_BACKEND_API_ERROR`), while the actual failed operation was system
`D3D12SerializeRootSignature`, HRESULT `0x887E0003` (`D3D12_ERROR_INVALID_REDIST`).
The executable exports `D3D12SDKPath = ".\\D3D12\\"`, but DLL deployment only populated
the executable directory's root. Deploying the Agility DLLs into `build/bin/D3D12/`
resolved it; this deployment is now automatic when the Brixelizer build gate is enabled.
No AMD-only restriction or scratch/ABI mismatch was established.

Final 1920x1080 Sponza captures each exit 0 after 282 frames, screenshot frame 240,
with no shader compile failure or device-removal error. Actual bake/GI submission
uses diffuse SRV 36955 and specular SRV 36957 for 138 instances. `brixelizer.diffuse`
is measured nonzero (min 0.836426, max 63.968750, average 16.763229).
NumPy/PIL A/B PSNR is 6.904351404 dB, with visibly added interior indirect light.
Mean GPU frame time is 6.859859 ms on versus 5.600662 ms off, overhead +1.259197 ms;
the GI pass averages 1.435603 ms over 82 samples. These are RTX 5080 measurements,
not RTX 4090 claims. An earlier TDR capture was discarded and its condition retried successfully.

Task items 8.1-8.5 are verified. All nine retained API-safety tests and three new real GPU
tests pass in one final suite run: 7,664 passed, 19 skipped, exactly the three pre-existing
`VfxEditorSlate.*` failures. Item 8.6 remains unticked: the requested enumerated-test
coverage argument is recorded, but no native instrumented percentage certifies >=95%;
allocation/upload failure injection and initialized resizing are not covered.
`r.BrixelizerGI` remains OFF by default. No commit or archive was made.
Full commands, artifacts, test names, and limitations:
[enablement report](brixelizer-gi-integration/01-enablement-2026-10-14.md).
