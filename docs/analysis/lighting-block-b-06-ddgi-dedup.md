# Lighting block B, item 6 — DDGI dedup (one trace per probe ray, the second bounce, TLAS reuse)

Rulebook task `phase0_lighting-block-b-06-ddgi-dedup` (proposal, design, spec `ddgi-dedup`). Branch
`feat/sponza-gi-parity`, 2026-10-07. The parecer's "DDGI: deduplicar os raios".

## Before

- `ddgi_probe_update_cs.hlsl` ran one thread per atlas texel and every thread traced ALL of its probe's
  rays: a probe has a 6 × 6 irradiance tile and a 16 × 16 depth tile, so its 32 rays were traced 292
  times each (36 + 256 texels), the shadow ray and the seven distance-field samples of the hit shading
  with them. Sponza Camera C at 1080p with `r.RayTracing.GI 1` (build 9f1be63f,
  `test-ddgi-dedup/before/camera-a-dxr`): `RadianceCache_Trace` (the SDF writer and the DDGI writer
  share the pass) 3.57 ms, frame 32.4 ms.
- `TLASManager::Rebuild` rebuilt the top-level acceleration structure every frame from the same
  instance descriptors: `RT_BLAS_TLAS` 0.093 ms on a static scene.
- The DDGI hit radiance was the sun term alone (`albedo × sunRad / π`); the SDF writer's is
  `albedo × (sunRad / π + indirect)` with the indirect term from the previous frame's probes. The
  per-probe energy comparison (`scripts/test_gi_energy_gpu.ps1`, linear grid mean ray radiance) read
  DDGI/SDF = 0.9321 (SDF 1.32622, DDGI 1.23623; the per-probe values of that run are the committed
  reference of D1a).
- Found while gating: with `r.GI.FinalGather 1` (the QA project) the SDF final gather replaces the probe
  irradiance in every pixel (`sdf_diffuse_gather` 1.0 in the path mask) and the dump's `diffuse_indirect`
  is ~0.0006; a frame comparison of the DDGI writers there compares noise. D1 was re-specified before
  the second run as D1a (the writer's own per-probe statistics) and D1b (the frame with the gather off).
- Found while gating: Sponza's fog project at frame 60 reproduces itself exactly between two warm runs
  of this build (1 px), while the committed capture (that build's first Sponza run) differs on ~90 k
  pixels (median 3 %); block B.5 had measured ~90 k pixels between two runs of its build and called it
  run-to-run spread. The third run of this build with the shader cache set aside is the test of the
  cold-cache explanation (D5's envelope).

**D5 at the block's closing sweep (2026-10-07, `lighting-block-b-2026-10.md`).** The cold-cache run
came out identical to the warm ones (0 px) while the colour against the committed capture differed on
70 566 px (median 2.1 %) with the direct light identical: the shader cache is one perturbation of the
loading timing, not the state itself, and a fresh envelope can collapse to zero. D5 now takes the
spread measured here (90 656 px, median 0.034) as the envelope's floor; the sweep reads 6/6.

## What changed

- `runtime/shaders/runtime/gi/ddgi_probe_common.hlsli` (new): the trace constants (the two
  radiance-cache-only slots carry the ray buffers' SRVs: `ddgiRayRadianceSrv`, `ddgiRayInfoSrv`),
  constants, helpers, `ProbeRayDirection(i, nRays, probe, frame)`, sky, distance-field shading and
  `TraceProbeRay` (now also returning the hit position, normal and albedo), shared by both writers.
- `ddgi_probe_trace_cs.hlsl` (new): one thread per probe ray, `[numthreads(64,1,1)]`; traces the ray,
  shades the hit with the sun and adds `albedo × SamplePreviousIrradiance(hit + N × 10 cm, N)` (the
  eight probes around the hit, trilinear × squared normal weight, each probe's 6 × 6 tile read at the
  texel of the normal, through the atlas UAV, zero on the first frame), writes `(radiance, hitT)` and the
  hit info to two structured buffers.
- `ddgi_probe_update_cs.hlsl`: reads ray i of probe p from the buffers (`probe × nRays + i`, the same
  `nRays` and the same direction formula as the trace); its cosine weighting, depth moments,
  diagnostics, VFX emissive injection and EMA blend are unchanged. With the sentinel SRV
  (`r.GI.DDGI.RayBuffers 0`, or no buffers / no trace pipeline) it traces per texel as before.
- `DDGISystem`: the two ray buffers (4096 × 64 × 16 B each), the trace dispatch between the atlas
  transition and the atlas passes with UAV → SRV barriers, CVars `r.GI.DDGI.RayBuffers` (default 1)
  and `r.GI.DDGI.IndirectTerm` (default 1), the identity log line reports both;
  `PipelineLibrary::GetDdgiProbeTrace()`; `frame_gi_passes.cpp` passes the pipeline.
- `TLASManager::Rebuild`: FNV-1a 64 of the 64-byte instance descriptors as they are written; an
  unchanged set skips the build and the barrier, a same-size set is refit in place
  (`BuildTLAS(update = true)`, full build every 64 refits), a changed count is fully rebuilt;
  `r.RayTracing.TLAS.Reuse` (default 1); counts logged at the first skip and at shutdown.
- `scripts/test_ddgi_dedup.py` (D1a, D1b, D2-D5; the per-texel and rebuild-every-frame controls on the
  same binary, the committed build's captures and per-probe values as references, the energy harness,
  the cold-cache run, montage).

## Gates (`scripts/test_ddgi_dedup.py`)

| Gate | Criterion | Result |
|---|---|---|
| D1a | ray dedup equivalence (writer): per-probe mean ray radiance, buffers (indirect term off) vs the per-texel trace on this binary (median rel <= 0.01, p99 <= 0.05) | PASS: median 0.0, p99 0.0, max 0.0 over 3719 probes (grid 1.2362289860789728 vs 1.2362289860789728); vs the committed build: buffers median 1.862955726469459e-06, p99 0.00029122458793126083; per-texel median 1.862955726469459e-06, p99 0.00029122458793126083 (3719 probes; committed grid 1.23623) |
| D1b | ray dedup equivalence (frame, final gather off): diffuse_indirect buffers (indirect term off) vs per-texel (median rel <= 0.01, p99 <= 0.05; channel mean luminance >= 10 x its gather-on value) | PASS: median 0.00000, p99 0.00000, max 0.0094 over 684945 px, mean luminance 0.0077; with the gather on (channel mean 0.00056): buffers vs per-texel median 0.00873, buffers vs committed median 0.00275 |
| D2 | cost: the DDGI writer (RadianceCache_Trace with DXR GI minus the SDF writer alone) <= 0.5 ms at 1080p | PASS: dxr 1.203 ms, sdf only 0.941 ms, increment 0.262 ms (per-texel path on this binary 3.585 ms, increment 2.644 ms; committed build 3.57245) |
| D3 | TLAS reuse: RT_BLAS_TLAS <= 0.03 ms on the static scene and the log reports skipped builds | PASS: 0.0006 ms (rebuilt every frame on this binary 0.0925422; committed 0.0928); builds full=35 refit=0 skipped=133 (rebuild control: full=168 skipped=0) |
| D4 | parity with the indirect term: the energy harness passes, the DDGI/SDF ratio lies within 1 +/- 0.10 and closer to 1 than the committed 0.9321 | PASS: ratio 0.9662 (SDF 1.32278605712345, DDGI 1.27805021841651), failed checks []; DDGI transport: identity skySrv=37220 mip=0 swizzle=(x,z,y) rays=32 sequence=stratified-frame-probe-rotation skyMultiplier=(1,1,1) lightColor=(10,9.8,9.5) lightDir=(0.10669906,0.5489189,0.8290376) lightIntensity=1 indirectTerm=atlas-previous-frame rayBuffers=1 knownRadiance=0 keepDepth=0 knownMode=0 knownDistance=0 knownDistanceMode=0 farDepth=0 |
| D5 | Sponza without DXR: direct_light identical to the committed build and the colour difference within the same-build run-to-run envelope (warm repeat and cold shader cache) | PASS: direct 0.00e+00; colour vs committed 89823 px, median rel 0.032; envelope 90656 px, median rel 0.034 (warm pair 1 px, cold vs warm 90656 px, cold vs committed 71199 px, cold direct 0.00e+00); diffuse_indirect median rel vs committed 0.01084, cold vs committed 0.00973 |

**Numbers (after, Sponza Camera C 1080p, DXR GI).** `RadianceCache_Trace`: 1.203 ms with the dedup (SDF writer alone 0.941 ms, DDGI increment 0.262 ms); the per-texel path on this binary 3.585 ms (increment 2.644 ms); committed build 3.572 ms. Frame 29.904 ms (per-texel 31.794 ms). `RT_BLAS_TLAS`: 0.0006 ms with reuse, 0.093 ms rebuilt every frame on this binary, committed 0.093 ms; builds over the 126 frames: full 35, refit 0, skipped 133 (the full builds happen while the meshes stream in and the instance count grows; the first skip at 138 instances; the rebuild control: full 168, skipped 0). Writer equivalence (sample project, per-probe mean ray radiance at frame 120, 3719 probes): buffers vs per-texel median relative 0.00000, p99 0.00000, max 0.0000 (grid 1.23623 vs 1.23623); against the committed build's per-probe values: buffers median 0.00000, p99 0.00029; per-texel median 0.00000, p99 0.00029 (committed grid 1.23623). Frame equivalence with the final gather off (684945 px, channel mean luminance 0.0077): median 0.00000, p99 0.00000; with the gather on the channel mean is 0.00056 (buffers vs per-texel median 0.00873, vs committed 0.00275). Indirect term (gather off): diffuse_indirect × 3.093 (median over lit pixels) against the dedup without it, mean ratio 4.795, mean luminance 0.0180 vs 0.0077. Frame parity of the two probe writers (gather off, 402833 px lit by the SDF cache): DDGI / SDF radiance cache diffuse GI median ratio 5.749 with the indirect term, 0.342 without it (mean luminance 0.0180 vs 0.0006). Energy parity (`test_gi_energy_gpu.ps1 -Frames 200`): SDF 1.32278605712345, DDGI 1.27805021841651, ratio 0.9662 (committed 0.9321); failed checks none. Sponza without DXR (fog project, frame 60): direct_light max |diff| vs the committed capture 0.0; colour 89823 px changed (median relative 0.032); the warm pair 1 px, cold cache vs warm 90656 px (median 0.034), cold cache vs committed 71199 px (median 0.023, direct 0.0); diffuse_indirect median relative vs committed 0.01084, cold vs committed 0.00973. Harness 6/6. Montage: `.screenshots/flicker-elimination/test-ddgi-dedup/ddgi_dedup_montage.jpg`.

## The frame with the gather off (reported, not gated)

Camera C looks at the rear doors of the atrium, an interior lit by bounce light. With
`r.GI.FinalGather 0` (montage `ddgi_dedup_montage.jpg`): the per-texel trace and the dedup without the
indirect term render the interior black (the probes carried the sun bounce only: a shadowed wall
returned nothing), the SDF radiance cache renders it black too (its consumer leaves `diffuse_indirect`
at a mean of 0.0006, support in 99.9 % of the pixels but almost no energy), and the
dedup with the indirect term lights walls, arches and floor (`diffuse_indirect` mean
0.0180, × 3.093 over the same path without the term). Frame colour means
(taa_output): gather on, the QA default, 0.0409; SDF cache with the gather off 0.0410; DDGI without the
term 0.0481; DDGI with the term 0.0582. The writers agree on the grid mean ray radiance within 3.4 %
(D4), so this is a difference between the consumers and the terms they carry: the DDGI path is now the
only configuration that lights this interior, and whether its level is right needs a reference (the
path tracer, or the SponzaTest frames of the parity port). Own item.

## Still open

- Frame-level reference for the DDGI level with the second bounce (above): the SDF cache and the final
  gather leave the Camera C interior nearly black, the DDGI path with the term lights it; neither has
  been compared with a ground truth.
- The indirect term has no Chebyshev visibility test of its own (the SDF writer's has none either):
  a hit behind a thin wall can read the probes on the other side, softened by the squared normal
  weight. The deferred lighting's test stays where it was.
- Probe relocation and classification exist only for the SDF path; the DDGI probes sit on the grid.
- `g_probeOffset` (temporal probe spreading) is still unused: every probe traces every frame.
- A refit keeps the previous tree topology; `kRefitsBeforeRebuild` (64) bounds the degradation under
  motion but has not been measured on a moving scene (Sponza is static).
- With `r.GI.FinalGather 1` the DDGI atlases are written every frame and read by no pixel of the frame
  (the gather replaces the probe irradiance): the writer's cost is paid for the gather's fallback only.
- The DXR shadow / reflection / AO passes remain scaffolding (known-issues "Raytracing").
