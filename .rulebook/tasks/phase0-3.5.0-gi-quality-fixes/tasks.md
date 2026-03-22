## 0. Diagnostic Baseline
- [ ] 0.1 Capture baseline screenshots at 3 camera positions (default, z=700, z=1500) with `--screenshot-stdout`.
- [ ] 0.2 Capture SSGI-only contribution (disable radiance cache via `radianceCacheEnabled=0`, screenshot).
- [ ] 0.3 Capture Radiance Cache-only contribution (disable SSGI via `ssgiEnabled=0`, screenshot).
- [ ] 0.4 Verify GTAO output via buffer viz grid (tile 7 = AO channel) — confirm AO is non-trivial under spheres.

## 1. SSGI Sky Bleeding Fix
- [ ] 1.1 Read `ssgi_trace_cs.hlsl` — identify where ray hits are evaluated and sky is sampled.
- [ ] 1.2 Add sky rejection: if ray hit depth >= farPlane * 0.99, discard the hit or replace with ambient. Prevents bright halos under objects.
- [ ] 1.3 Review ray march max distance cutoff — ensure it's not too large for the scene scale.
- [ ] 1.4 Build + visual verify: SSGI pass dump should show no white patches below spheres.

## 2. SSGI Quality & Denoise
- [ ] 2.1 Increase SSGI default quality from 1 (4 rays) to 2 (8 rays) in `ssgi_renderer.h`.
- [ ] 2.2 Read `ssgi_spatial_denoise_cs.hlsl` — check kernel size and edge-stop functions. Increase radius or iterations if too small.
- [ ] 2.3 Read `ssgi_temporal_accum_cs.hlsl` — verify temporal blend factor and velocity-based rejection handles TAA jitter.
- [ ] 2.4 Build + visual verify: spheres should appear smooth without grain/shimmer on smooth materials.

## 3. Radiance Cache Shadow Bleed Fix
- [ ] 3.1 Fix `deferred_lighting_ps.hlsl` line 1145: replace `max(diffuseIBL, rcDiffuse)` with shadow-aware weighted blend: `diffuseIBL += rcDiffuse * rcWeight * ao`.
- [ ] 3.2 Multiply radiance cache diffuse contribution by direct shadow factor to prevent RC from lighting areas in CSM shadow.
- [ ] 3.3 Build + visual verify: shadows under spheres should be darker with proper contrast.

## 4. Temporal Stability (Anti-Pulsing)
- [ ] 4.1 Increase RadianceCache EMA alpha from 0.05 to 0.12 in `radiance_cache.h` — faster convergence reduces pulsing.
- [ ] 4.2 Add TAA jitter offset compensation to radiance cache probe ray origins — probes should trace from unjittered world positions.
- [ ] 4.3 Verify SSGI temporal reprojection correctly subtracts TAA jitter from velocity vectors.
- [ ] 4.4 Build + visual verify: orbit camera around spheres — no visible brightness pulsing.

## 5. Prefiltered Specular Resolution (Gold Color Shift Fix)
- [ ] 5.1 Increase `kPrefilteredSize` from 128 to 256 in `ibl_baker.h`.
- [ ] 5.2 Increase `kPrefilteredMipLevels` from 5 to 6 (256→128→64→32→16→8).
- [ ] 5.3 Update `ibl_baker.cpp` bake dispatch to handle new resolution (group counts, UAV descriptors per mip).
- [ ] 5.4 Update `PerFrameCB::prefilteredMipLevels` default to match new mip count.
- [ ] 5.5 Build + visual verify: gold sphere should maintain consistent hue when orbiting camera.

## 6. Final Validation
- [ ] 6.1 Capture final screenshots at same 3 camera positions as Phase 0.
- [ ] 6.2 Compare before/after — all 4 issues (shadow bleed, pulsing, color shift, noise) must be visibly improved.
- [ ] 6.3 FPS benchmark: confirm no regression > 10% from baseline (target 200+ FPS).
- [ ] 6.4 Test with FBX demo scene to verify fixes generalize beyond PBR test spheres.
- [ ] 6.5 Regression: existing CSM shadows, IBL diffuse, SSR compositing still correct.
