# The split-sum specular reads the prefiltered mips with a PMREM curve the engine's bake does not follow

## Why

Block B.5b measured the prefiltered-cubemap (split-sum) specular on the roughness 0.3 and 0.5 metal
floors of the reflection reference at 3.26 × and 4.85 × the CPU integral of the GGX BRDF over the
environment the engine samples, while the environment-MIS estimate read 0.99 × and the mirror floor
1.08 ×. The lighting picks the prefiltered mip as `roughness × (2 − roughness) × maxMip × 1.2`, a curve
written for Three.js PMREM atlases (their LODs follow non-linear roughness breakpoints), while the
engine's own bake (`IBLBaker::BakeEnvironment`) prefilters mip m at roughness `m / (mips − 1)`,
linearly. Roughness 0.5 therefore reads mip 6.3 of 7, baked for roughness 0.9, and roughness 0.3 reads
mip 4.3, baked for 0.61: lobes far wider than the surface's, which gather the bright horizon and the
ground the sky capture carries. The same curve is applied to a PMREM environment, where it is the
intended convention.

Measuring the first fix (the linear mip read) left the roughness 0.5 floor at 3.73 × and the 0.3 floor
at 2.08 ×, and a second defect surfaced from the dielectric boxes of the same scene: their
`diffuse_indirect / albedo` is grey (0.72 per channel) where the sky's irradiance for an upward normal
is (0.07, 0.13, 0.24) — the sky's SH is projected in the cubemap's frame (+Y up) and evaluated with the
world normal (Z up), so every normal reads the irradiance of a direction with y and z swapped (the
floor and the box tops read the horizon's). Block B.5b fixed the same frame in the sampler. The
multi-scatter term of the split-sum uses that irradiance, and every scene without a GI probe source
lights its ambient with it.

Measuring the frame fix (the second build) made the box tops read (1.33, 1.26, 1.15), the irradiance
of the ground: the CPU packing (`PackSHToCompactFormat`) copies UE5's signs, whose `SHBasisFunction3`
defines the y, x, yz and xz basis functions with a minus sign, onto this file's Ramamoorthi basis (all
positive). The x and y slopes and the yz and xz bands came out negated, which is the irradiance field
rotated 180° about the cube's z axis: with the frame swap on top an upward world normal read the
horizon's grey, and with the frame fixed it read the ground. A Python replica of the projection from the
dumped environment reproduces both numbers (0.74 grey; (1.371, 1.295, 1.188)).

## What changes

- The deferred lighting reads the engine's own bake linearly (`mip = roughness × maxMip`, the bake's
  convention) and keeps the PMREM curve for PMREM environments; the renderer says which it bound with a
  flag packed in the per-frame `prefilteredMipLevels` (bit 8), set by `SetPMREMEnvironment` and cleared
  by the sky bake and `SetHDRIEnvironment`; every consumer of the mip count masks the flag.
- The sky SH is evaluated with the normal in the cubemap's frame (`EvalSH_L2(WorldToCubemap(N), ...)`),
  and the lower-hemisphere colour the renderer injects into the SH is written for that frame's down
  axis.
- `sh_projection.cpp`: the packing's signs follow the file's basis (E / π = Σ (A_l / π) L_lm Y_lm with
  the convolution folded into the constants); `SHCubemapToWorld` re-expresses packed SH in the world
  frame for the light probe volume, which the shader evaluates with the world normal (seeded from the
  sky SH in `frame_constants.cpp` and `IrradianceProbeGrid::CaptureFromSkyCubemap`); gtests for the
  hemisphere irradiance, a cap around each axis and the frame map.
- Diagnostics: the sampler can dump any cubemap at any mip (`EnvMapSampler::DumpRadiance`); after the sky
  bake, with a region dump active, the eight prefiltered mips are dumped beside the environment, so the
  bake itself can be measured against a CPU prefilter (P8).
- Evidence: `scripts/rendering/test_split_sum.py` (P1-P6): the rough floors against the CPU integral on the sky
  bake and on the PMREM (`reflection-reference-hdri-rough30` / `-hdri-rough50`, new variants), the
  chrome floors as mip-0 controls, Sponza as the direct-light invariant.

## Gates (pre-registered)

P1 sky bake, roughness 0.3 and 0.5 metal floors (`r.EnvMIS 0`): split-sum / CPU integral median ratio
within [0.75, 1.3] (before 3.26 / 4.85). P2 the chrome floor (sky bake): specular against the build
before within [0.95, 1.05]. P3 PMREM: the rough floors' ratio reported; the chrome floor under the
PMREM unchanged within [0.95, 1.05]. P4 the dielectric boxes' specular against before, reported. P5
Sponza Camera A and C: positions and direct light identical; specular and colour ratios reported. P6
the deferred lighting pass within 10 % of before. P7 the sky irradiance frame: the dielectric box tops'
`diffuse_indirect / albedo` against E / π of the dumped environment for the upward normal, per channel
within [0.85, 1.15] (before 10.3 / 5.5 / 3.0). P8 the bake: each dumped prefiltered mip against the CPU
GGX prefilter of the environment at the mip's roughness (N = V = R), median ratio within [0.9, 1.1]
over the sphere of directions (reported per mip; gated on mips 0-5).

P1b (added 2026-10-08 after the first after-run and before the second): the engine against a CPU model
of its own approximations (the prefiltered radiance at the lerped R with the bake's N = V = R lobe, the
Karis DFG, the multi-scatter term with the dump's L2 irradiance), median ratio within [0.9, 1.1] on both
sky-bake floors, with the P1 ratio binned by NoV. The first after-run (the mip and the frame fixed, the
packing still wrong) read P1 at 2.73 / 5.07 with the engine matching the model at 0.98 / 1.00 and the
excess concentrated at grazing incidence (the floors sit at NoV 0.1-0.7): the remainder is the
split-sum's assumption, which P1's bound did not anticipate. P1 stays as registered and is reported as
it falls; P1b says whether the implementation is the model.

The before captures: the first before-run reused block B.5b's sky-bake captures, taken before the sky
capture fix (B.5c, 1 / √3 on the whole sky), which made P2 read 0.60 for the wrong reason; the
references are captured on the build before itself (513e2983) with the engine changes stashed.

## Impact
- Affected specs: split-sum-mips (new)
- Affected code: `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`,
  `runtime/shaders/runtime/gbuffer/translucent_ps.hlsl`, `runtime/engine/renderer/frame/frame_constants.cpp`,
  `runtime/engine/renderer/scene/scene_upscaler.cpp`, `scene/scene_renderer_impl.h`,
  `runtime/engine/renderer/ibl/sh_projection.{h,cpp}`, `gi/irradiance_probe_grid.cpp`,
  `runtime/tests/engine/renderer/test_ibl_atmosphere.cpp`;
  `scripts/rendering/reflection_reference.py`, `scripts/rendering/test_split_sum.py`
- Breaking change: NO for PMREM scenes (unchanged); scenes lit by the engine's bake change by design
  (rough metals darken to the integral's level)
- User benefit: rough metals under the sky reflect what the BRDF says, not a lobe three times wider.

## Out of scope

The PMREM curve's fidelity to Three.js's `roughnessToMip` (the ktx2's LOD convention is not
documented here; the rough PMREM floors are reported for a later decision), the bake's source-mip bias
against a 64² sky cubemap without mips, the N = V = R assumption and the analytic DFG fit.
