# Lighting side item — the light cull assigns spots by their cone

Rulebook task `phase0_lighting-cluster-spot-cone` (proposal, design, spec `cluster-spot-cone`). Branch
`feat/sponza-gi-parity`, 2026-10-08. Block B.7's open item.

## Before

`cluster_assign_cs.hlsl` kept a spot light in a cluster when its range sphere reached the cell's AABB,
whatever its cone: a spot aimed down with a 35° outer angle and a 700 cm range filled the slots of
every cell within 700 cm, and the per-pixel loop evaluated it there for a cone factor of exactly 0
(`cone = saturate((cosA − cosOuter) · invCosConeDifference)`, squared). Measured on the same binary
with the test off (`r.Lights.ClusterSpotCone 0`): mean cluster light count over lit pixels 5.55 on the
lights reference (16 spots) and 126.63 on the dense variant (100 spots).

## What changed

- After the range test, the cell's bounding sphere (centre `(lo + hi) / 2`, radius `|hi − lo| / 2`) is
  tested against the spot's cone (apex, axis `direction`, `cosOuterAngle`, range): the closest distance
  from the sphere's centre to the cone's surface against the sphere's radius, the range front and the
  apex back (Wronski 2017); a sphere that intersects the cone passes. The culled lights contributed 0, so
  the sums are bit-identical.
- `r.Lights.ClusterSpotCone` (default 1) and `kClusterFlag_SpotCone` (bit 4 of `clusterFlags`).
- `scripts/test_cluster_spot_cone.py` (K1-K4).

## Gates (`scripts/test_cluster_spot_cone.py`)

| Gate | Criterion | Result |
|---|---|---|
| K1 | identity: taa_output and direct_light with the cone test equal the sphere-only assignment (max |diff| <= 1e-5) | PASS: ref: taa 0, direct 0; dense: taa 0, direct 0 |
| K2 | fewer slots: the mean cluster light count over lit pixels drops by >= 20 % on the dense scene with the cone test | PASS: dense 126.63 -> 66.88 (47.2 % fewer); ref 8.9 % fewer |
| K3 | LightCull costs at most 0.05 ms more than the sphere-only assignment (dense scene) | PASS: 0.0703872 -> 0.0704213 ms |
| K4 | DeferredLighting on the dense scene is not slower (<= 1.02 x the sphere-only assignment) | PASS: 0.896932 -> 0.713641 ms (ref 0.38798 -> 0.371923 ms) |

**Numbers (1080p).** Mean cluster light count over lit pixels: lights reference 5.55 → 5.06 (8.9 % fewer), dense 126.63 → 66.88 (47.2 % fewer). LightCull dense 0.070 → 0.070 ms; DeferredLighting dense 0.897 → 0.714 ms, reference 0.388 → 0.372 ms. Harness 4/4.

## Still open

- A tighter cone-vs-AABB test (the bounding sphere of a 32 px × slice cell is conservative, more so
  for the far slices).
- The light list beyond 512 / 128 (its own item).
