# Heterogeneous volumetric media (DXR path tracer)

General-purpose participating media for the DXR pipeline path tracer: a scalar
density grid (`VolumeAsset`, OpenVDB-style dense voxels) inside an oriented box
(`VolumeComponent`), rendered with Woodcock/delta tracking for free flights,
ratio tracking for every transmittance, a Henyey-Greenstein phase function and
absorption-weighted emission. It is the path tracer's own medium: the raster
froxel `VolumetricFog` is a separate screen-space system and ignores it.

## Gate

Everything is opt-in behind the existing `r.PathTracer.Volume` CVar
(`--path-trace --volume`). With it off the path tracer never reads the grid
root fields and the raster frame is unchanged. With it on and no
`VolumeComponent` in the scene, the stage-1/2 procedural sphere
(`r.PathTracer.Volume.*`) renders exactly as before; one or more enabled
components replace the sphere.

## Data

`VolumeAsset` (`engine/asset/formats/volume_asset.h`) holds the grid and uploads
it as a `Texture3D<float>` (R8_UNORM or R16_FLOAT, up to 256^3). The `.uzvol`
file is a 32-byte header (`UZVOL`, version 1, width/height/depth, format) and
raw voxels, x fastest. `GetMaxDensity()` is the delta-tracking majorant factor.
`VolumeAsset::Resolve` accepts a project-relative `.uzvol` path or a built-in
procedural grid: `builtin:sphere`, `builtin:cloud` (deterministic fBm cumulus),
`builtin:constant`, `builtin:ramp` (an R16F linear ramp whose optical depth is
analytic).

`VolumeComponent` (`engine/ecs/environment/volume_component.h`, scene key
`volume`): `density_grid`, `half_extents` (cm, multiplied by the entity scale),
`sigma_a` and `sigma_s` (per cm at unit density), `phase_g`, `emission`
(radiance per unit absorption), `density_scale`, `enabled`. Up to four enabled
components are gathered per frame.

## Transport

`PipelinePathTracerPass` uploads one 96-byte `PathVolumeGridDesc` per component
(world-to-box affine rows, coefficients, majorant, emission, grid SRV) into a
pass-private structured buffer and points the existing 12-DWORD `PathVolumeRoot`
at it through its two former padding DWORDs (`volumeGridSrv`,
`volumeGridCount`). No root signature or shared constant buffer grows.

In `pathtracer_volume.hlsli`:

- `PathVolumeGridInterval` is a slab test in the box's normalized frame; the
  direction is mapped by the linear part only, so the distances are world ray
  parameters.
- `PathVolumeGridDensity` samples the grid trilinearly with UVW clamped to the
  voxel-centre range (clamp addressing with the pass's wrap sampler).
- `PathVolumeGridDelta` thins majorant-Poisson candidates with probability
  `density(x) / maxDensity`; there is no null-collision cap. The accepted
  distance follows the exact heterogeneous free-flight law, so surface survival
  carries weight 1, scattering the albedo `sigmaS/sigmaT` and emission the
  absorption fraction `sigmaA/sigmaT` (`L_e(x) = sigma_a(x) * emission`).
- `PathVolumeGridRatio` multiplies `1 - density(x)/maxDensity` over the
  candidates: an unbiased estimate of `exp(-int sigmaT)`. Shadow rays from
  medium and surface vertices multiply one estimate per box, which is exact
  even where boxes overlap because extinction is additive.
- `PathVolumeGridFreeFlight` tracks the boxes in nearest-entry order up to the
  surface. Absorption-only, non-emissive boxes apply their ratio weight;
  the others delta-track. Overlapping boxes are tracked one at a time, which
  is exact only for disjoint boxes.

The raygen loop handles a collision like the stage-1 sphere: NEE through
`PathVolumeDirect` with the box's phase, HG-sampled continuation, Russian
roulette, and multiple scattering up to `r.PathTracer.MaxBounces`. Active media
bypass the surface-only ReSTIR and NRD paths.

## Proof

`scripts/rendering/test_path_volume_grid.py` captures linear-HDR regions
(`--region-fields extra`, channel `taa_tap_centre`) of the fixtures
`path-volume-grid-{slab,ramp,emission,scatter,cloud}.uzscene` and compares them
with references built from the same DXR rays: Beer-Lambert through the constant
box, the quadrature of the R16F ramp's trilinear density, `L_bg e^-tau +
L_e (1 - e^-tau)` for the emissive slab, and the isotropic single-scatter
integral for the scattering slab. The cloud case renders four camera angles
at 640x360 into a montage and measures the 1080p GPU time with and without the
medium. `runtime/tests/engine/renderer/test_path_volume_grid.cpp` covers the
`.uzvol` round trip, the generators, the shader-equivalent sampler and the
delta/ratio estimators on sampled grids against midpoint quadrature.
