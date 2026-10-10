# The light cull assigns spots by their range sphere alone

## Why

Block B.7's cluster assignment (`cluster_assign_cs.hlsl`) tests every light against the cluster's
AABB with its range sphere; for a spot light that is the whole sphere, although the lighting's cone
factor is exactly 0 outside the outer angle (`cone = saturate((cosA − cosOuter) · invCosConeDifference)`,
squared). A spot aimed down with a 35° outer angle and a 700 cm range therefore occupies the slots of
every cluster within 700 cm, most of which it cannot light, and the per-pixel loop evaluates it there
for nothing. Block B.7 left "a cone test for spots" open; the user asked for the pending items to be
finished (2026-10-08).

## What changes

- After the range-sphere test, a spot is kept only if its cone reaches the cluster's bounding sphere
  (the conservative cone-sphere test: the closest distance from the sphere's centre to the cone's
  surface against the sphere's radius, the range front and the apex back; Wronski 2017). The sum of
  the kept lights is bit-identical to the sphere-only assignment because the culled lights contribute
  exactly 0.
- `r.Lights.ClusterSpotCone` (default 1) and `kClusterFlag_SpotCone` (bit 4 of `clusterFlags`) for
  the A/B on one binary.
- Evidence: `scripts/rendering/test_cluster_spot_cone.py` (K1-K4) on the lights reference (16 spots) and its
  dense variant (100 spots).

## Gates (pre-registered)

K1 identity: on both scenes `taa_output` and `direct_light` with the cone test equal the sphere-only
assignment (max |diff| ≤ 1e-5). K2 fewer slots: the mean cluster light count over lit pixels drops by
≥ 20 % on the dense scene (`r.Lights.ClusterDebug 1`'s record). K3 the LightCull pass costs at most
0.05 ms more than the sphere-only assignment. K4 the deferred lighting pass on the dense scene is not
slower than the sphere-only assignment (≤ 1.02 ×).

## Impact
- Affected specs: cluster-spot-cone (new)
- Affected code: `runtime/shaders/runtime/lighting/cluster_assign_cs.hlsl`,
  `runtime/shaders/runtime/lighting/light_types.hlsli`,
  `runtime/engine/renderer/core/params/constant_buffers.h`, `runtime/engine/renderer/frame/frame_constants.cpp`;
  `scripts/rendering/test_cluster_spot_cone.py`
- Breaking change: NO (identical lighting by gate)
- User benefit: spot lights cost what they light.

## Out of scope

A tighter cone-vs-AABB test (the bounding sphere is conservative), the spots' shadow atlas, the light
list beyond 512 / 128 (its own item).
