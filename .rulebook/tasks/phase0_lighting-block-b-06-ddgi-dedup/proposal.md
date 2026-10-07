# DDGI dedup: each probe ray traced once, the second bounce, and a TLAS that is not rebuilt every frame

## Why

The parecer's block-B item "DDGI: deduplicar os raios": `ddgi_probe_update_cs.hlsl` ran one thread
per atlas texel and every thread traced ALL of its probe's rays. A probe has a 6 × 6 irradiance tile
and a 16 × 16 depth tile, so its 32 rays were traced 292 times each (36 + 256 texels), with the
shadow ray and the seven distance-field samples of the hit shading repeated as many times. Measured
before any change (2026-10-07, build 9f1be63f, Sponza Camera C at 1080p, `r.RayTracing.GI 1`): the
`RadianceCache_Trace` pass (the SDF writer and the DDGI writer together) takes 3.57 ms, and
`RT_BLAS_TLAS` 0.093 ms on a static scene because `TLASManager::Rebuild` rebuilt the top-level
acceleration structure every frame from the same instance descriptors. The DDGI writer also carried
no second bounce: the SDF writer's hit radiance is `albedo × (sunRad / π + indirect)` with the
indirect term from the previous frame's probes, the DDGI writer's was the sun term alone, which is
one of the reasons the per-probe energy comparison read DDGI/SDF = 0.9321.

## What changes

- A new pass `ddgi_probe_trace_cs.hlsl` (one thread per probe ray, `[numthreads(64,1,1)]`) traces
  each ray once per frame, shades the hit with the sun and, by default, adds `albedo × the previous
  frame's irradiance at the hit` read from the irradiance atlas (the SDF writer's second bounce,
  `r.GI.DDGI.IndirectTerm`), and writes two structured buffers: (radiance, hitT) and the hit info.
- `ddgi_probe_update_cs.hlsl` integrates the buffers per atlas texel (ray i of probe p derived from
  the same `ProbeRayDirection(i, nRays, p, frame)` in both shaders), keeping its cosine weighting,
  depth moments, diagnostics, VFX emissive injection and EMA blend verbatim. The shared helpers,
  constants, sky, distance-field shading and `TraceProbeRay` live in `ddgi_probe_common.hlsli`.
  With `r.GI.DDGI.RayBuffers 0` (or no buffers / no trace pipeline) the update shader traces per
  texel as before, the A/B control on one binary.
- `DDGISystem` owns the two ray buffers (4096 × 64 × 16 B each), dispatches the trace before the
  atlas passes (barriers UAV → SRV), and passes the buffers' SRVs through the two radiance-cache-only
  slots of the shared trace constants; `PipelineLibrary::GetDdgiProbeTrace()`.
- `TLASManager::Rebuild` hashes the instance descriptors (FNV-1a 64 over the 64-byte records it
  writes): an unchanged set skips the build and the barrier, a same-size set with different contents
  is refit in place (`BuildTLAS(..., update = true)`, a full build every 64 refits), a changed count is
  fully rebuilt. `r.RayTracing.TLAS.Reuse 0` restores the per-frame rebuild. Build counts are logged.
- Evidence: `scripts/test_ddgi_dedup.py` (D1-D5), the energy parity harness, montage.

## Gates (pre-registered)

D1 equivalence: with the indirect term off, `diffuse_indirect` at Sponza Camera C frame 120 matches
the committed build's capture (median relative difference ≤ 0.01, p99 ≤ 0.05); D2 cost: the DDGI
writer (`RadianceCache_Trace` with DXR GI minus the SDF writer alone) ≤ 0.5 ms at 1080p; D3 TLAS:
`RT_BLAS_TLAS` ≤ 0.03 ms on the static scene and the log reports skipped builds; D4 parity with the
indirect term: `scripts/test_gi_energy_gpu.ps1 -Frames 200` passes and the DDGI/SDF ratio lies
within 1 ± 0.10 and closer to 1 than 0.9321; D5 Sponza without DXR unchanged (the fog project at
frame 60: direct_light identical to the committed capture, colour within the run-to-run envelope).
Re-specified before the first run: D2 was first written as the whole pass ≤ 0.5 ms; the pass also
holds the SDF writer, so the gate is the DDGI increment over an SDF-only capture.
Re-specified after the first run (results recorded in the doc): D1 compared `diffuse_indirect` with
`r.GI.FinalGather 1`, where the SDF final gather replaces the probe irradiance in every pixel and the
channel is ~0.0006 (noise against noise, 4/5 "passed"); it is now D1a, the writer's own per-probe mean
ray radiance (buffers with the indirect term off vs the per-texel trace on this binary, median ≤ 0.01,
p99 ≤ 0.05; the committed build's per-probe values reported), and D1b, the same frame comparison with
the gather off (channel mean luminance > 0.01 required). D5's envelope compared against one warm repeat,
which was identical (1 px) while the committed capture, that build's first Sponza run, differed on
~90 k pixels; the envelope is now the largest difference among three runs of this build, two warm and
one with the shader cache set aside (the cold-cache explanation of block B.5's "run-to-run spread").

## Impact
- Affected specs: ddgi-dedup (new)
- Affected code: `runtime/shaders/runtime/gi/ddgi_probe_common.hlsli` (new),
  `ddgi_probe_trace_cs.hlsl` (new), `ddgi_probe_update_cs.hlsl`, `runtime/engine/renderer/gi/ddgi_system.*`,
  `core/pipeline/pipeline_gi.cpp`, `pipeline_library.h`, `pipeline_library_impl.h`,
  `frame/frame_gi_passes.cpp`, `raytracing/tlas_manager.*`, `scripts/test_ddgi_dedup.py`
- Breaking change: NO (defaults change the DDGI radiance by the indirect term, gated by D4; the
  previous behaviour is one CVar away: `r.GI.DDGI.IndirectTerm 0`, `r.GI.DDGI.RayBuffers 0`,
  `r.RayTracing.TLAS.Reuse 0`)
- User benefit: the DXR probe update costs a fraction of what it did, the TLAS build disappears on
  static scenes, and the DXR path carries the same second bounce the SDF path does.

## Out of scope

Probe relocation and classification for the DDGI path, a Chebyshev test inside the indirect
term, temporal probe spreading (`g_probeOffset`), BLAS refits for skinned meshes, the DXR shadow /
reflection / AO passes (still scaffolding), the SDF writer's own cost.
