# Lighting block B, item 7 — clustered light lists (and the light parameters that were ignored)

Rulebook task `phase0_lighting-block-b-07-clustered-lights` (proposal, design, spec
`clustered-lights`). Branch `feat/sponza-gi-parity`, 2026-10-07. The parecer's "clustered lights".

## Before

- `deferred_lighting_ps.hlsl` looped over every point (≤ 64) and spot (≤ 16) light of the frame in
  every pixel; `cluster_assign_cs.hlsl` was a one-line stub left by the clustered-forward-plus task
  (its first three items ticked without code). The arrays live in a 5632-byte constant buffer.
- The light components' `sourceRadius`, `lightFalloffExponent`, `specularScale`, `diffuseScale` and
  `useInverseSquaredFalloff` were packed into the buffer and never read by the shader; the scene
  loader did not read them from files.
- Measured on build edfd7942 (`scripts/rendering/lights_reference.py`, 1080p, frame 60): with 64 point + 16
  spot lights the deferred lighting takes 0.465 ms against 0.285 ms with no local lights. The
  per-pixel loop is cheap at the old cap because the range test rejects most lights per pixel; the
  cap itself was the limit (80 local lights per frame) and the loop grows with every light.

## What changed

- Limits 64 / 16 → 512 / 128 (`LightingCB` 43264 bytes, inside the 64 KB a constant buffer allows,
  uploaded through the 2 MB-per-frame ring); the LightingCB gtests and `scripts/rendering/test_cbuffer_layouts.py`
  (new LightingCB entry; its struct search anchored at line start, the doc comments quote the HLSL
  structs) follow.
- `cluster_assign_cs.hlsl`: one thread per cluster of a 32-pixel-tile × 24-logarithmic-slice grid
  (48 960 clusters at 1080p, 6.5 MB of records); the slice range follows the lights (from the nearest
  view depth a light's range reaches to the farthest, clamped to the camera's planes); the cluster's
  world-space AABB from its eight corners (camera rays through the tile corners with a one-pixel
  margin for the TAA jitter, each ray the line through two unprojected points, at the slice's two
  depths); every point and spot light whose range sphere touches the AABB is appended, points first
  then spots, each in index order (the order the loop used, so the sums are bit-identical); more
  lights than the record's slots (32, `r.Lights.ClusterSlots`) sets an overflow bit.
- Two defects of the first version, found by the harness and a CPU replica of the projection and
  fixed before the second run: (1) slicing the camera's whole [5 cm, 100 m] spent the slices near the
  camera and left the cells at the lights' distance 37 % deep, so 39 % of the 80-light frame's
  clusters overflowed their slots and took the full loop (0 % with the lights' range, median cluster
  4 lights); (2) the ray through a tile corner was built as `unproject(ndc, 0.5) − cameraPos`: a
  centimetre between that constant and the projection's eye bent rays reconstructed from a point ten
  centimetres away by ten per cent, the GPU's boxes were right at the image centre and 11 % too far
  out at the sides (dumped through `r.Lights.ClusterDebug 2/3` and compared with the replica), and
  0.7 % of the pixels lost a light that barely reached them (max 0.038 in HDR). The ray is now the
  line through two unprojected points.
- `deferred_lighting_ps.hlsl`: the loop bodies are `ShadePointLight` / `ShadeSpotLight`; the pixel
  evaluates its cluster's list (`r.Lights.Clustered`, default 1), every light when the cluster
  overflowed or the CVar is 0; `r.Lights.ClusterDebug` tints the frame by the cluster's light count.
  The per-light parameters are live: custom falloff `pow(1 − d²/r², exponent)` when the inverse-square
  flag is clear (UE's RadialAttenuation), the diffuse / specular scales, the point light's source
  radius (Karis 2013 sphere light: representative point for the specular direction, GGX alpha widened
  to `α + r / 2d` with energy `(α/α')²`). With the defaults the code paths are the previous ones.
- Host: the cluster parameters (forward, slice range, grid, descriptors, flags) in the tail of
  `LightingCB` (`frame_constants.cpp`); `RegisterLightCullPass` sizes the list buffer for the render
  resolution and dispatches the cull before the lighting, ordered through a declared depth read (the
  graph orders passes only through declared resources, block B.5); `PipelineLibrary::GetLightCull()`;
  the buffer is destroyed with the resolution-dependent resources.
- The scene loader reads and writes `useInverseSquaredFalloff`, `falloffExponent`, `sourceRadius`,
  `specularScale`, `diffuseScale`.
- `scripts/rendering/lights_reference.py` (80 lights, 500 lights, none, one light per parameter) and
  `scripts/rendering/test_clustered_lights.py` (C1-C4, montage).

## Gates (`scripts/rendering/test_clustered_lights.py`)

| Gate | Criterion | Result |
|---|---|---|
| C1 | identity (80 lights): clustered vs per-pixel loop, taa_output max |diff| <= 1e-5 | PASS: max |diff| 0 (mean luminance 0.6632 vs 0.6632) |
| C1b | overflow (4 slots per cluster): identical to the per-pixel loop, max |diff| <= 1e-5 | PASS: max |diff| 0 |
| C1c | across builds: this build's per-pixel loop vs the committed build (median rel <= 1e-3, max |diff| <= 0.02) | PASS: median rel 0.00e+00, p99 0.00e+00, max |diff| 0.000831 over 2073600 lit px |
| C1d | identity (500 lights): clustered vs per-pixel loop, max |diff| <= 1e-5 | PASS: max |diff| 0 (mean luminance 0.9262) |
| C2 | cost (500 lights, 1080p): DeferredLighting clustered <= 0.5 x the per-pixel loop and LightCull <= 0.3 ms | FAIL: clustered 0.909165 ms vs loop 1.80015 ms, cull 0.0703755 ms; 80 lights: clustered 0.381984 / loop 0.519647 (cull 0.0137675), committed 0.464937; no lights 0.296582 (committed 0.284603) |
| C3a | diffuseScale 0.5: floor radiance ratio (the ambient of the no-light scene subtracted) median in [0.47, 0.55] | PASS: median ratio 0.5264559013588258 over 53887 px (ambient 0.632, light 0.069) |
| C3b | falloff exponent: lum(exp 8) / lum(exp 2) matches (1 - d^2/r^2)^6 (median |error| <= 0.02) | PASS: median |err| 0.0004040035853542298, p90 0.0013285052631578087 over 64668 px |
| C3c | source radius: the highlight peak (light only, glossy floor) decreases, src50 < src20 < src0 and peak(src50) <= 0.8 x peak(src0) | PASS: peak src0 2.9439, src20 0.9646, src50 0.3795; floor sums 5666.3 / 5550.7 / 5226.0 |
| C4 | Sponza unchanged: direct_light (Lo: sun + local lights) identical to the committed capture; colour within the cold/warm envelope of block B.6 | PASS: direct 0.00e+00; colour changed 4.2982 % (89127 px), max |diff| 4.25, median rel 2.98e-02 (envelope 90656 px, median 0.034) |

**Numbers (after, 1080p, frame 60).** Identity: clustered vs loop max |diff| 0 with 80 lights (mean luminance 0.6632), 0 with 500 lights (mean 0.9262), 0 with every cluster overflowing (4 slots); this build's loop against the committed build: median relative 0, p99 0, max |diff| 0.000831 over 2073600 lit pixels. Cost: 500 lights, DeferredLighting 0.909 ms clustered against 1.800 ms with the loop (cull 0.070 ms; frame 2.176 vs 2.762 ms); 80 lights 0.382 ms clustered, 0.520 ms loop (cull 0.014 ms), committed build 0.465 ms; no local lights 0.297 ms (committed 0.285 ms). Parameters: diffuseScale 0.5 → floor ratio median 0.5265 over 53887 px; falloff exponent 8 / 2 against (1 − d²/r²)^6: median |error| 0.0004, p90 0.0013 over 64668 px (floor mean luminance, the whole 40 m floor: inverse square light only 0.0041, exponent 2 493.0360, exponent 8 100.0259: the custom falloff treats the colour as a brightness, 250 000 at the source); source radius 0 / 20 / 50 cm: highlight peak 2.9439 / 0.9646 / 0.3795, floor sums 5666.317 / 5550.685 / 5225.953. Sponza (fog project, frame 60, warm): direct_light max |diff| 0, colour pixels changed 4.2982 %, max |diff| 4.25, median relative 0.0298. Harness 8/9. Montage: `.screenshots/flicker-elimination/test-clustered-lights/clustered_lights_montage.jpg`.

## Still open

- Spot lights are culled with their range sphere; a cone test (done 2026-10-08, `lighting-cluster-spot-cone.md`) would shrink their lists (the result is
  the same, the cost is not).
- The forward translucent pass and the froxel fog do not read the lists (nor the local lights, for
  the fog).
- Local lights do not reach the GI: the probe writers see the sun only.
- Shadows stay at 16 atlas tiles; beyond that, lights render unshadowed.
- The old clustered-forward-plus task (phase3a-3.32.0) planned a structured-buffer light list and
  1024 lights; the constant buffer holds 512 / 128, which is where this item stops.
- The centimetre between `cameraPos` and the eye the unprojection implies (the cause of the first
  cull's bent rays) is not explained; the cull no longer depends on it, and any other shader that
  builds a ray as `unproject(ndc, z) − cameraPos` from a near point inherits the same error.
