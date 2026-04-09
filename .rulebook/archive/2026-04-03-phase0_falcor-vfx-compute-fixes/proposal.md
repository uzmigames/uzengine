# Proposal: phase0_falcor-vfx-compute-fixes

## Why
Falcor analysis + deep shader audit found quality and performance gaps in VFX, compute, and IBL:
1. **Particles have no lighting** — smoke/volumetric particles appear disconnected from scene. Falcor samples SH ambient.
2. **No soft particles** — particles clip through geometry without depth fade.
3. **Curl noise uses 6×FBM** (finite-difference) — analytical curl is 3×FBM (50% savings, identical output). @ref Bridson 2007.
4. **Hi-Z pyramid uses 11 dispatches** — AMD SPD does full mip chain in 1 dispatch (massive bandwidth savings).
5. **GPU cull extracts frustum planes 64× per group** — should be 1× with `groupshared` + barrier.
6. **Per-thread InterlockedAdd** in cull/compact — `WavePrefixCountBits` reduces to 1 atomic per wave.
7. **Mipgen is box filter only** — no Kaiser filter for quality, no renormalized-average for normal maps.
8. **IBL BRDF LUT uses NDF sampling** — VNDF (Heitz 2018) gives 2-4× lower variance at grazing angles.
9. **IBL BRDF LUT missing multi-scatter compensation** — rough metallic surfaces lose energy (Turquin 2019).
10. **Trail interpolation is linear** — Catmull-Rom spline produces smoother curves.
11. **No GPU particle sorting** — alpha-blended particles render in pool order (wrong overlap).

## What Changes
- Particle lighting: sample ambient SH L2 + directional light per particle
- Soft particles: depth fade with configurable soft distance
- Emissive particles: separate additive blend pass
- Decal blend: fix by reading actual GBuffer albedo for non-replace modes
- Analytical curl noise: 3×FBM replacing 6×FBM finite-difference
- True `prevPosition` per particle for correct motion vectors
- Catmull-Rom trail interpolation
- GPU particle sorting (bitonic sort compute pass)
- GPU-driven indirect dispatch for spawn
- AMD SPD single-pass Hi-Z generation
- Kaiser + normal-renormalized mipgen modes
- `groupshared` frustum planes in GPU cull
- Wave intrinsic slot allocation in cull/compact
- World-space decal normal blending (decode→blend→re-encode)
- VNDF sampling + multi-scatter energy compensation in BRDF LUT
- Mip-bias firefly suppression in prefiltered specular
- BRDF LUT resolution increase (256² → 512²)

## Impact
- Affected specs: `docs/specs/rendering/particle-system.md`, `docs/specs/rendering/gpu-instancing.md`, `docs/specs/rendering/ibl-pipeline.md`
- Affected code: `vfx/` (12 shaders), `compute/` (4 shaders), `ibl/` (3 shaders)
- Breaking change: NO (quality + performance improvements, existing behavior preserved)
- User benefit: Lit particles integrated into scene, smoother trails, faster compute passes, brighter rough metals (energy compensation), no firefly sparkle in reflections
