# Design: the cluster spot cone test

## Before
`cluster_assign_cs.hlsl` builds the cell's world AABB (lo, hi) and keeps a spot when the squared
distance from its position to the box is within its range; `SpotLightData` carries `direction`,
`cosOuterAngle` and `invCosConeDifference`, read only by the lighting.

## After
- The cell's bounding sphere (centre `(lo + hi) / 2`, radius `|hi − lo| / 2`) and, per spot kept by the
  range test, the cone-sphere test with the spot's axis `direction` (the lighting's `cosA = dot(Ls,
  −direction)` makes `direction` the axis from the light outward), `cosOuterAngle` and `radius`:
  `distClosest = cosA · sqrt(|v|² − v1²) − v1 · sinA` against the sphere's radius, `v1 > r + range` and
  `v1 < −r` as the front and back culls. A sphere that intersects the cone passes; the test errs on
  keeping.
- `clusterFlags` bit 4 (`kClusterFlag_SpotCone`) from `r.Lights.ClusterSpotCone`; the shader skips the
  test when the bit is clear.

## Harness
`scripts/test_cluster_spot_cone.py`: the lights reference and `lights-reference-400` with the flag on
and off; K1 identity of `taa_output` / `direct_light`, K2 the cluster light count from
`r.Lights.ClusterDebug 1`'s `rc_support` record, K3 / K4 the LightCull and DeferredLighting timings.
