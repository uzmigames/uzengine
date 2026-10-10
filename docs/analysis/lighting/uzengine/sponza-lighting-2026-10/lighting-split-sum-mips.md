# Lighting side item — the split-sum specular on rough metals: the mip curve, the sky SH's frame and packing

Rulebook task `phase0_lighting-split-sum-rough-metals` (proposal, design, spec `split-sum-mips`).
Branch `feat/sponza-gi-parity`, 2026-10-08. Block B.5b's finding ("the split-sum path reads 3-5 × the
integral on rough metals"), taken at the user's request to finish the pending items.

## Before

- The deferred lighting picked the prefiltered mip with the curve written for Three.js PMREM atlases
  (`roughness × (2 − roughness) × maxMip × 1.2`) for every environment, while the engine's own bake
  (`IBLBaker::BakeEnvironment`, 8 mips of 256²) prefilters mip m at roughness m / 7, linearly: roughness
  0.5 read mip 6.3 (baked for 0.9) and roughness 0.3 read mip 4.3 (baked for 0.61), lobes far wider than
  the surface's that gather the bright horizon and the ground the sky capture carries.
- Measuring the linear read alone left the roughness 0.5 floor at 3.73 × and the 0.3 floor at 2.08 ×,
  and the dielectric boxes of the same scene showed the second defect: their `diffuse_indirect / albedo`
  was grey, (0.72, 0.72, 0.72) per channel, where the sky's E / π for an upward normal is
  (0.070, 0.131, 0.238) — the ratios [10.2, 5.6, 3.0] per channel. Two things were wrong with the sky SH. The
  projection (`ProjectCubemapToSH`) accumulates over the DX cube-face directions, +Y up, and
  `EvalSH_L2` took the world normal (Z up), so every surface read the irradiance of a direction with y
  and z swapped (block B.5b fixed the same frame in the sampler; the probes' sky reads and the
  irradiance-cubemap fallback already convert). And the CPU packing (`PackSHToCompactFormat`) copied
  UE5's signs, whose `SHBasisFunction3` negates the y, x, yz and xz basis functions, onto this file's
  Ramamoorthi basis (all positive): the x and y slopes and the yz and xz bands came out negated, the
  irradiance field rotated 180° about the cube's z axis. With both, an upward world normal read the
  horizon (E / π of the cube's horizontal directions is exactly the grey (0.743, 0.746, 0.746)
  measured); with the frame fixed alone it read the ground ((1.33, 1.26, 1.15) measured; a Python
  replica of the projection from the dumped environment gives (1.371, 1.295, 1.188) for the shipped
  packing and (0.075, 0.136, 0.242) for the corrected one). The lower-hemisphere colour the renderer
  injects into the SH was also written into the wrong coefficients (`coeffs[3].xyz`, the red channel's
  L2 band) with Y00 / 2 of the colour as the constant term.
- Measured on build 513e2983 (`test-split-sum/before`, 1080p, the engine changes stashed): sky bake,
  roughness 0.3 / 0.5 metal floors, split-sum / CPU integral 3.255 (p10 2.597, p90 3.483) / 4.850 (p10 3.960, p90 5.420); PMREM, roughness
  0.3 / 0.5: 1.285 (p10 0.611, p90 1.912) / 0.572 (p10 0.197, p90 1.061).

## What changed

- `deferred_lighting_ps.hlsl`: the engine's bake is read linearly (`mip = roughness × maxMip`); a PMREM
  keeps the curve, told by bit 8 of the per-frame `prefilteredMipLevels` (`m_prefilteredIsPmrem`, set by
  `SetPMREMEnvironment`, cleared by `SetHDRIEnvironment`); every consumer of the mip count masks the
  flag (the glass and transmission reads, `translucent_ps.hlsl`). The sky SH is evaluated with
  `WorldToCubemap(N)`.
- `sh_projection.cpp`: the packing's signs follow the file's basis (E / π = Σ (A_l / π) L_lm Y_lm with
  the convolution folded into the constants, all positive). `SHCubemapToWorld` re-expresses packed SH in
  the world frame (the swap of y and z on the packed polynomial; its own inverse) for the light probe
  volume, which the shader evaluates with the world normal (seeded from the sky SH in
  `frame_constants.cpp` and `IrradianceProbeGrid::CaptureFromSkyCubemap`). Three gtests
  (`test_ibl_atmosphere.cpp`): a hemisphere light reads E / π = (1 + n_up) / 2 within 0.03, a cap around
  each axis peaks toward it, the frame map equals the swapped evaluation and inverts itself.
- The lower-hemisphere injection writes `L / 2` into the constant term and `−L / 2` into the cube-frame
  y (up) term per channel, the exact L0 + L1 of a hemisphere light.
- Diagnostics: `EnvMapSampler::DumpRadiance(srv, mip, name)` dumps any cubemap mip as an equirect; after
  the sky bake, with a region dump active, the eight prefiltered mips land beside the environment dump
  (`prefiltered_mip<N>.f32`), and the harness checks the bake against a CPU GGX prefilter (P8).
- `scripts/rendering/reflection_reference.py`: the rough metal floors under the PMREM; `scripts/rendering/test_split_sum.py`
  (P1-P8 and P1b).

## Gates (`scripts/rendering/test_split_sum.py`)

| Gate | Criterion | Result |
|---|---|---|
| P1 | sky bake, roughness 0.3 metal floor: split-sum / CPU integral median ratio within [0.75, 1.3] | FAIL: median ratio 1.309 (p10 1.136, p90 2.003) over 1943 px |
| P1b | sky bake, roughness 0.3 metal floor: the engine against the CPU model of its own approximations, median ratio within [0.9, 1.1] | PASS: median ratio 0.967 (p10 0.960, p90 0.976); engine / integral by NoV [0.00,0.20) 1.97, [0.20,0.35) 1.42, [0.35,0.50) 1.22, [0.50,0.70) 1.15 |
| P1 | sky bake, roughness 0.5 metal floor: split-sum / CPU integral median ratio within [0.75, 1.3] | FAIL: median ratio 2.331 (p10 1.753, p90 3.192) over 1943 px |
| P1b | sky bake, roughness 0.5 metal floor: the engine against the CPU model of its own approximations, median ratio within [0.9, 1.1] | PASS: median ratio 0.994 (p10 0.990, p90 1.004); engine / integral by NoV [0.00,0.20) 3.17, [0.20,0.35) 2.58, [0.35,0.50) 2.04, [0.50,0.70) 1.75 |
| P7 | the sky irradiance frame: the box tops' diffuse_indirect / albedo against E / pi of the environment for the upward normal, per channel within [0.85, 1.15] | PASS: ratio r/g/b [1.032, 1.015, 0.985] over 3 boxes |
| P8 | the bake: each dumped prefiltered mip against the CPU GGX prefilter at the mip's roughness, median ratio within [0.9, 1.1] (mips 0-5 gated) | PASS: mip 0 (r 0.00): 0.999; mip 1 (r 0.14): 1.000; mip 2 (r 0.29): 1.000; mip 3 (r 0.43): 1.001; mip 4 (r 0.57): 1.003; mip 5 (r 0.71): 1.009; mip 6 (r 0.86): 1.027; mip 7 (r 1.00): 1.048 |
| P2 | the chrome floor (sky bake): specular_indirect against the build before, median ratio within [0.95, 1.05] | FAIL: median ratio 0.896 (p10 0.873, p90 0.919) over 1142581 px |
| P3 | the chrome floor under the PMREM: specular_indirect against the build before, median ratio within [0.95, 1.05] | PASS: median ratio 0.972 (p10 0.928, p90 0.988) over 1142581 px |
| P5 | Sponza Camera A and C: positions and direct light identical to the build before (specular and colour ratios reported) | PASS: camera_a: direct 0, specular ratio 0.9996647387503023, colour ratio 1.000; camera_c: direct 0, specular ratio 0.9999331183237126, colour ratio 1.001 |
| P6 | the deferred lighting pass within 10 % of the build before | PASS: 10.881 -> 10.722 ms; 9.202 -> 8.995 ms |

**Numbers (after, 1080p).** Sky bake: roughness 0.3 floor split-sum / integral 1.309 (p10 1.136, p90 2.003), roughness 0.5 2.331 (p10 1.753, p90 3.192) (integral medians 0.1467 / 0.1085, the engine 0.1919 / 0.2578). The engine against the CPU model of its own approximations: 0.967 (p10 0.960, p90 0.976) / 0.994 (p10 0.990, p90 1.004) (the model's single-scatter part 0.1799 / 0.2286, its multi-scatter part 0.0195 / 0.0321). PMREM: roughness 0.3 1.149 (p10 0.547, p90 1.598), 0.5 0.396 (p10 0.136, p90 0.735) (reported). Chrome floor against the build before: sky bake 0.896 (p10 0.873, p90 0.919), PMREM 0.972 (p10 0.928, p90 0.988); the chrome floor against the mirror reference of the dumped environment (F(NoV) × L(R)): before 1.110, after 0.996 (the old curve put roughness 0.02 at mip 0.33, a third of the roughness-0.14 mip, which at the horizon mixes the ground into the sky's reflection, so P2's premise that the chrome floor would not move was wrong). Dielectric boxes' specular against before: 1.059 over 42189 px (0.0188 → 0.0199). Box tops' irradiance against E / π: r/g/b [1.032, 1.015, 0.985] (E / π [0.0703, 0.1306, 0.2376]). Prefiltered mips against the CPU prefilter: mip 0 (roughness 0.00) 0.999, mip 1 (roughness 0.14) 1.000, mip 2 (roughness 0.29) 1.000, mip 3 (roughness 0.43) 1.001, mip 4 (roughness 0.57) 1.003, mip 5 (roughness 0.71) 1.009, mip 6 (roughness 0.86) 1.027, mip 7 (roughness 1.00) 1.048. Sponza Camera A: positions 0, direct 0, `specular_indirect` ratio median 1.000 (p10 0.893, p90 1.110), colour ratio 1.000, deferred 10.881 → 10.722 ms. Sponza Camera C: positions 0, direct 0, `specular_indirect` ratio median 1.000 (p10 0.988, p90 1.015), colour ratio 1.001, deferred 9.202 → 8.995 ms. Harness 7/10.

**The remainder above the integral is the split-sum's assumption, not the implementation.** The engine
reproduces the CPU model of its own approximations (the prefiltered radiance at the lerped R with the
bake's N = V = R lobe, the Karis DFG, the multi-scatter term with the L2 irradiance) within a few
percent on both floors, and the excess over the GGX integral grows as the view grazes the floor: the
lobe the prefiltered lookup averages reaches below the floor's horizon, where the integral clips it,
and the single-scatter DFG assumes the lobe is centred on R. The floors of the reference sit at
NoV 0.1-0.7 (the camera looks almost horizontally across them). P1 was registered before this was
understood and is reported as it falls; P1b was added between the first and the second after-run.

| Floor | NoV | px | engine / integral | single-scatter / integral | engine / model |
|---|---|---|---|---|---|
| roughness 0.3 | [0.00, 0.20) | 421 | 1.97 | 1.92 | 0.970 |
| roughness 0.3 | [0.20, 0.35) | 589 | 1.42 | 1.35 | 0.962 |
| roughness 0.3 | [0.35, 0.50) | 655 | 1.22 | 1.10 | 0.966 |
| roughness 0.3 | [0.50, 0.70) | 271 | 1.15 | 0.99 | 0.974 |
| roughness 0.5 | [0.00, 0.20) | 421 | 3.17 | 2.92 | 0.991 |
| roughness 0.5 | [0.20, 0.35) | 589 | 2.58 | 2.30 | 0.992 |
| roughness 0.5 | [0.35, 0.50) | 655 | 2.04 | 1.73 | 0.999 |
| roughness 0.5 | [0.50, 0.70) | 271 | 1.75 | 1.41 | 1.005 |

## Still open

- The split-sum's excess at grazing incidence on rough metals (the table above): a horizon term for
  metals is the usual mitigation (`deferred_lighting_ps.hlsl` applies one to dielectrics only, following
  Three.js), or the environment-MIS estimate (block B.5b), which reads the integral.
- The PMREM curve against Three.js's `roughnessToMip` (the ktx2's LOD convention is not documented
  here; the rough PMREM floors read 1.149 / 0.396 of the integral).
- The bake's source-mip bias is computed against the output mip's texel size (`faceSize` is the output
  size), moot for the 64² sky cubemap (no mips) and double-filtering for a PMREM source.
- The radiance cache's and DDGI's own SH (`PackL2` in the trace shaders) use a third basis convention,
  self-consistent; not touched.
