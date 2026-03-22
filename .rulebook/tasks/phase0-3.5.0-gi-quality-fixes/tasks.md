## 0. Diagnostic Baseline
- [x] 0.1 Capture baseline screenshots at 3 camera positions (default, z=700, z=1500). Saved to `.screenshots/baseline_*.png`.
- [ ] 0.2 Capture SSGI-only contribution (disable radiance cache via `radianceCacheEnabled=0`, screenshot).
- [ ] 0.3 Capture Radiance Cache-only contribution (disable SSGI via `ssgiEnabled=0`, screenshot).
- [x] 0.4 Verify GTAO output — baseline screenshots show darkening under spheres, confirming GTAO is active but insufficient due to RC/SSGI bleed.

## 1. SSGI Sky Bleeding Fix
- [x] 1.1 Read `ssgi_trace_cs.hlsl` — ray hits evaluated at line 508, sky skip at line 372 (march loop). Problem: silhouette edge hits sample sky-bright prev color.
- [x] 1.2 Added post-hit sky depth rejection (re-sample depth at hit UV, reject if <= FAR_DEPTH_VALUE+0.001). Added luminance clamp (cap hitColor at luminance 2.0).
- [x] 1.3 Capped `maxTraceDist = min(sceneDepth, 2000.0f)` (20m max). Was unbounded `sceneDepth`.
- [x] 1.4 Build + visual verify: clean build passes. Screenshot captured — subtle improvement, main benefit is during motion.

## 2. SSGI Quality & Denoise
- [x] 2.1 Increased SSGI default quality from 1 (4 rays) to 2 (8 rays) in `ssgi_renderer.h:49`.
- [ ] 2.2 Read `ssgi_spatial_denoise_cs.hlsl` — check kernel size and edge-stop functions. Increase radius or iterations if too small.
- [ ] 2.3 Read `ssgi_temporal_accum_cs.hlsl` — verify temporal blend factor and velocity-based rejection handles TAA jitter.
- [x] 2.4 Build + visual verify: clean build passes. Shadows visibly darker after combined fixes.

## 3. Radiance Cache Shadow Bleed Fix
- [x] 3.1 Fixed `deferred_lighting_ps.hlsl:1145`: replaced `max(diffuseIBL, rcDiffuse)` with `diffuseIBL += rcDiffuse * rcWeight * ao * shadow`. Additive blend modulated by AO and direct shadow.
- [x] 3.2 Direct shadow factor applied — `shadow` term multiplied into RC diffuse to prevent light injection in CSM-shadowed areas.
- [x] 3.3 Build + visual verify: shadows under spheres are darker with proper contrast. Before/after comparison confirms improvement.

## 4. Temporal Stability (Anti-Pulsing)
- [x] 4.1 Increased RadianceCache EMA alpha from 0.05 to 0.12 in `radiance_cache.h:78`. ~8 frame convergence (was ~20).
- [ ] 4.2 Add TAA jitter offset compensation to radiance cache probe ray origins — probes should trace from unjittered world positions.
- [ ] 4.3 Verify SSGI temporal reprojection correctly subtracts TAA jitter from velocity vectors.
- [ ] 4.4 Build + visual verify: orbit camera around spheres — no visible brightness pulsing.

## 5. Prefiltered Specular Resolution (Gold Color Shift Fix)
- [x] 5.1 Increased `kPrefilteredSize` from 128 to 256 in `ibl_baker.h`. Moved constants to public section.
- [x] 5.2 Increased `kPrefilteredMipLevels` from 5 to 6 (256→128→64→32→16→8).
- [x] 5.3 `ibl_baker.cpp` uses `kPrefilteredSize`/`kPrefilteredMipLevels` throughout — no hardcoded values, auto-adapts.
- [x] 5.4 Fixed `frame_setup.cpp:224`: changed `prefilteredMipLevels = 5` to `IBLBaker::kPrefilteredMipLevels`. Updated test from 5→6.
- [x] 5.5 Build passes. Visual verify: specular reflections are sharper at default position. Gold color shift requires interactive test.

## 6. Final Validation
- [x] 6.1 Captured final screenshots at default + z=700 positions. `.screenshots/all_fixes*.png`.
- [x] 6.2 Before/after comparison: shadows visibly darker and more defined. Contact shadows under spheres improved.
- [x] 6.3 FPS benchmark: ~24ms frame time (same as baseline). No regression.
- [x] 6.4 FBX demo test: mushroom monster renders correctly with proper shadows. Fixes generalize.
- [x] 6.5 Regression: CSM shadows, IBL diffuse, specular reflections all correct in both demos.
