## Phase 1: Sky Bleeding Fix
- [ ] 1.1 Read `ssgi_trace_cs.hlsl` — identify where ray hits are evaluated and sky is sampled.
- [ ] 1.2 Add sky rejection: if ray hit depth >= farPlane * 0.99, discard the hit or replace with ambient.
- [ ] 1.3 Build + visual verification: pass-dump ssgi-trace should show no white patches below spheres.

## Phase 2: Surface Noise Reduction
- [ ] 2.1 Read `ssgi_spatial_denoise_cs.hlsl` — check kernel size and edge-stop functions.
- [ ] 2.2 Increase spatial denoise iterations or radius if too small.
- [ ] 2.3 Check temporal accumulation blend factor in `ssgi_temporal_accum_cs.hlsl`.
- [ ] 2.4 Build + visual verification: spheres should appear smooth without grain/shimmer.

## Phase 3: Validation
- [ ] 3.1 Pass dump comparison: before vs after (ssgi-trace, ssgi-result, hdr).
- [ ] 3.2 Runtime visual check: no bright halos, no shimmer on smooth surfaces.
