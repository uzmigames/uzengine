# DDGI Dedup

## Purpose
Each DDGI probe ray is traced once per frame and carries the second bounce; the TLAS is built only
when its instances change.

## ADDED Requirements

### Requirement: One trace per probe ray
The DDGI writer SHALL trace each of a probe's rays once per frame (`ddgi_probe_trace_cs.hlsl`) into
ray buffers that the atlas writer integrates per texel, and the integrated atlases SHALL equal those of
the per-texel trace.

#### Scenario: Equivalence of the writer
Given the energy harness's run with the indirect term off and the same run with the per-texel trace
When the DDGI writer's per-probe mean ray radiance at frame 120 is compared between them
Then the median relative difference is at most 0.01 and the 99th percentile at most 0.05.

#### Scenario: Equivalence of the frame
Given Sponza Camera C with DXR GI on, the final gather off and the indirect term off
When frame 120's diffuse_indirect is compared with the per-texel trace's capture
Then the median relative difference is at most 0.01, the 99th percentile at most 0.05, and the
channel's mean luminance exceeds 0.01 (with the gather on the channel is ~0.0006).

#### Scenario: Cost
Given the same capture and one with the DDGI writer off (`r.RayTracing.GI 0`)
When the RadianceCache_Trace timings are subtracted
Then the DDGI writer costs at most 0.5 ms at 1920 × 1080.

### Requirement: Second bounce
The DDGI probe radiance SHALL include `albedo × the previous frame's probe irradiance` at the hit,
as the SDF writer's does, under `r.GI.DDGI.IndirectTerm` (default on).

#### Scenario: Energy parity
Given `scripts/rendering/test_gi_energy_gpu.ps1 -Frames 200`
When the grid mean ray radiance of both writers is compared
Then the harness passes, the DDGI/SDF ratio lies within 1 ± 0.10 and is closer to 1 than 0.9321.

### Requirement: TLAS reuse
`TLASManager::Rebuild` SHALL skip the build when the instance descriptors are unchanged, refit in
place when their count is unchanged, and rebuild otherwise (`r.RayTracing.TLAS.Reuse`, default on).

#### Scenario: Static scene
Given Sponza Camera C with DXR on
When RT_BLAS_TLAS is averaged over frames 60-125
Then it is at most 0.03 ms and the log reports skipped builds.

### Requirement: The SDF path is untouched
Without DXR the frame SHALL be unchanged.

#### Scenario: Sponza without DXR
Given the fog project at frame 60 (SSR off)
When compared with the committed build's capture
Then direct_light is identical and the colour difference lies within the same-build run-to-run
envelope (the largest difference among three runs of the build: two with the warm shader cache and
one with the cache set aside).
