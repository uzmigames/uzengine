## VFX + Compute Improvements (from Falcor analysis)
> Source: `docs/analysis/falcor/shader-improvement-table-vfx-compute.md`
> Estimate: 4 sessions

## Session 1: VFX particle quality

- [ ] 1.1 `vfx/particle_billboard_ps.hlsl` — add particle lighting: sample ambient SH L2 + directional light at particle position. Scale color by lighting result.
- [ ] 1.2 `vfx/particle_billboard_ps.hlsl` — separate additive blend pass for emissive particles (don't mix emissive into alpha-blended output). Add `emissiveBlendMode` flag to particle CB.
- [ ] 1.3 `vfx/particle_billboard_ps.hlsl` — add soft particles: depth fade at geometry intersections. `softFactor = saturate((sceneDepth - particleDepth) / softDistance)`.
- [ ] 1.4 `vfx/vfx_collision.hlsli` — fix standoff distance: use `collisionOffset / (zfar - znear)` instead of `collisionOffset * 0.001`. Pass znear/zfar in VFX CB.
- [ ] 1.5 `vfx/decal_deferred_ps.hlsl` — fix non-replace blend modes: bind GBuffer albedo RT0 as SRV input. Read actual surface color for additive/multiply/screen blending.
- [ ] 1.6 Build + verify: particles should react to scene lighting. Soft particles should fade at intersections.

## Session 2: VFX advanced features

- [ ] 2.1 `vfx/vfx_common.hlsli` — replace finite-difference curl noise (6×FBM) with analytical curl (3×FBM). @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow".
- [ ] 2.2 `vfx/vfx_motion_vectors.hlsli` — store true `prevPosition` per particle (write during simulation). Replace backward Euler estimate `pos - vel*dt`.
- [ ] 2.3 `vfx/trail_vs.hlsl` — upgrade trail interpolation from linear to Catmull-Rom spline. @ref Catmull & Rom 1974. Smoother trail curves.
- [ ] 2.4 `vfx/particle_spawn_cs.hlsl` — add GPU-driven indirect dispatch: write spawn count to indirect args buffer. Remove CPU readback of alive count.
- [ ] 2.5 `vfx/particle_billboard_vs.hlsl` — implement GPU particle sorting: bitonic sort compute pass before rendering. @ref Peters & Winkelmann 2021 or radix sort.
- [ ] 2.6 Build + verify: curl noise turbulence should look identical but run ~50% faster. Trail ribbons should be smooth curves.

## Session 3: Compute infrastructure upgrades

- [ ] 3.1 `compute/hiz_pyramid_cs.hlsl` — replace 11-dispatch mip chain with AMD Single Pass Downsampler (SPD). 1 dispatch, groupshared cascade. @ref AMD 2020 "FidelityFX SPD".
- [ ] 3.2 `compute/mipgen_cs.hlsl` — add Kaiser filter mode (for color textures) and renormalized-average mode (for normal maps). Selector via root constant.
- [ ] 3.3 `compute/gpu_cull_cs.hlsl` — move frustum plane extraction to `groupshared`: compute once per group, barrier, then all threads use shared planes. @ref Wihlidal 2015.
- [ ] 3.4 `compute/gpu_cull_cs.hlsl` + `compute/draw_compact_cs.hlsl` — replace per-thread `InterlockedAdd` with `WavePrefixCountBits` for slot allocation (1 atomic per wave). @ref SM6.0 wave intrinsics.
- [ ] 3.5 `compute/decal_apply_cs.hlsl` — fix normal blending: decode from octahedral → blend in world space → renormalize → re-encode. Currently blends in encoded space (geometrically wrong for large factors).
- [ ] 3.6 Build + verify: Hi-Z generation should be 1 dispatch. GPU cull should show fewer atomics in PIX.

## Session 4: IBL pipeline quality

- [ ] 4.1 `ibl/brdf_lut_cs.hlsl` — switch to VNDF sampling (Heitz 2018) for lower variance. @ref Heitz 2018.
- [ ] 4.2 `ibl/brdf_lut_cs.hlsl` — add multi-scatter energy compensation (Turquin 2019). Store Ems in blue channel. @ref Turquin 2019.
- [ ] 4.3 `ibl/prefilter_cs.hlsl` — add mip-bias firefly suppression: `mipLevel = max(mipLevel, log2(texWidth * sqrt(pdf)))`. @ref Lagarde 2014.
- [ ] 4.4 `ibl/irradiance_cs.hlsl` — verify sample count is sufficient (should be 4096+ for L2 SH quality).
- [ ] 4.5 Increase BRDF LUT resolution from 256×256 to 512×512 (matches Falcor). Verify rough metallic surfaces have smoother Fresnel falloff.
- [ ] 4.6 Build + verify: metallic surfaces with roughness > 0.5 should appear brighter (energy compensation). No firefly sparkle on rough reflections.
