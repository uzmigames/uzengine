# Design: the split-sum's prefiltered mips

## Before
`deferred_lighting_ps.hlsl`: `mipLevel = clamp(roughness × (2 − roughness) × maxMip × 1.2, 0, maxMip)`
for every environment; `ibl_baker.cpp`: `mipRoughness = mip / (kPrefilteredMipLevels − 1)` for the
engine's bake (8 mips, 256²). The two agree only at roughness 0 and 1.

## After
- The per-frame `prefilteredMipLevels` carries the mip count in its low byte and, in bit 8, whether the
  prefiltered cubemap bound is a PMREM (`SetPMREMEnvironment` sets `m_prefilteredIsPmrem`,
  `SetHDRIEnvironment` and the sky bake clear it; `frame_constants.cpp` packs it).
- The lighting decodes both: `maxMip = (levels & 0xFF) − 1`; `mipLevel = isPmrem ? the existing curve :
  roughness × maxMip`. The glass and transmission reads of the mip count mask the flag; the translucent
  pass too.
- Nothing changes in the bake, the DFG, the multi-scatter term (its `cosineIrradiance` is E / π, the
  cosine-weighted mean radiance, as Three.js's `cosineWeightedIrradiance`) or the PMREM path.

## The sky SH
- Frame: `ProjectCubemapToSH` walks the DX cube faces (+Y up); the deferred lighting evaluates it with
  `WorldToCubemap(N)` like every other cubemap read. SH the shader evaluates with the world normal
  (the light probe volume, seeded from the sky SH) goes through `SHCubemapToWorld`, the swap of y and z
  re-expressed on the packed polynomial (`yy = 1/2 − zz/2 − (xx − yy)/2`,
  `xx − zz = 1/2 − 3zz/2 + (xx − yy)/2`); the map is its own inverse.
- Packing: `PackSHToCompactFormat` folds the cosine convolution into the constants
  (c0 = Y00, c1 = 2/3 · 0.4886, c2 = c3 · 3.46 = 1/4 · 1.0925, c3 = 1/4 · 0.3154, c4 = 1/4 · 0.5463) with
  every sign positive, the sign of this file's basis; UE5's negations belong to UE5's basis.
- Injection: the lower-hemisphere colour L is `+L / 2` in the constant term and `−L / 2` in the cube
  y term, the exact L0 + L1 of a hemisphere light (E / π = L (1 − n_y) / 2).

## Harness
`scripts/test_split_sum.py`: the roughness 0.3 / 0.5 floors under the sky bake and under the PMREM
against the CPU integral (P1) and against the CPU model of the engine's own approximations, binned by
NoV (P1b), the chrome floors against the build before, the dielectric boxes reported, the box tops'
irradiance against E / π (P7), the bake's mips against a CPU prefilter (P8), Sponza's direct light as
the invariant with its specular and colour ratios reported; `--before` captures the same
reflection-reference set on the build before (the engine changes stashed, build 513e2983).
