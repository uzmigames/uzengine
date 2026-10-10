# Design: clustered light lists

## Grid
Screen tiles of 32 pixels (60 × 34 at 1080p) by 24 depth slices: slice s spans the view depths
`[near · e^{s/k}, near · e^{(s+1)/k})` with `k = slices / log(far / near)`, slice 0 starting at the
camera and the last slice ending at the camera's far plane. The slice range follows the lights:
`near` is the nearest view depth any light's range reaches and `far` the farthest (clamped to the
camera's planes), computed on the host from the light arrays each frame. The first version sliced
the camera's whole [5 cm, 100 m]: the slices were spent near the camera and the cells at the lights'
distance were 37 % deep, so 39 % of the 80-light frame's clusters overflowed their 32 slots (and
took the full loop); with the lights' range the overflow is 0 % and the median cluster holds 4
lights. The pixel's slice is `floor(log(max(d, near) / near) · k)`, with d the view depth
`dot(p − camera, forward)`. 48 960 clusters at 1080p, a record of 33 uints each (6.5 MB), the
buffer sized for the current render resolution and recreated when it grows.

## Cull (`cluster_assign_cs.hlsl`)
One thread per cluster. The cluster's eight corners are the camera rays through the tile's corners
(one pixel of margin because the TAA jitter moves the frustum by half a pixel) at the slice's two
depths; their world-space AABB contains the cell (a conservative, convention-free cell: no view
matrix, no projection parameters). Each ray is the line through two points unprojected with
`invViewProj` (NDC z 0.25 and 0.75), with `cameraPos` used only as the depth reference the lighting
also uses. The first version built the ray as `unproject(ndc, 0.5) − cameraPos`; a CPU replica of
the projection showed the GPU's boxes right at the image centre and 11 % too far out at the sides,
the signature of a ray reconstructed from a point ten centimetres from an origin that is off by a
centimetre, and 0.7 % of the pixels lost a light that barely reached them. Every light whose range
sphere touches the AABB (closest-point distance) is appended: points first, then spots, each in
index order. More lights than slots: the count carries the overflow bit and the lighting evaluates
every light of the frame for that cluster. Spots use their range sphere (no cone test: a tighter
test is a later refinement; the sphere keeps the result exact).

## Lighting
The loop bodies are `ShadePointLight` / `ShadeSpotLight`; the clustered path reads the record and
calls them per slot; the loop path and the overflow path call them for every light. Because the
clustered path visits a superset-filtered subset in the same order and the culled lights contribute
exactly zero (outside their range the early-out returns 0, and `x + 0 = x`), the sums are
bit-identical: the gate is identity, not tolerance. The lighting reads the list through
`LightingCB::clusterListSrv` (bindless), set by the cull pass's registration; the sentinel keeps
the loop.

## Per-light parameters
- `flags & kLightFlag_InverseSquared` clear: `pow(saturate(1 − d²/r²), falloffExponent)` (UE's
  RadialAttenuation; the colour is a brightness), otherwise Karis's windowed inverse square as before.
- `diffuseScale` / `specularScale` multiply the two terms.
- `sourceRadius` (points): the specular direction is the point of the sphere closest to the reflection
  ray (`toLight + centerToRay · saturate(radius / |centerToRay|)`), the GGX alpha widens to
  `saturate(α + radius / (2 d))` with energy `(α / α')²` (Karis 2013). The diffuse term keeps the
  centre direction. With radius 0 the code path is the previous one.

## Ordering and lifetime
`RegisterLightCullPass` runs in `RegisterDeferredLightingPass` before the lighting pass is added. The
render graph orders passes only through declared resources (block B.5): the cull declares the depth
read it does not perform, which places it after the GBuffer and, with the lighting pass's own depth
read, before the lighting. The list buffer is a renderer member destroyed with the SSR resources.

## Harness
`scripts/rendering/lights_reference.py` writes the scenes (floor, pillars, walls; 80 lights; 500 lights; none;
one light per parameter) and `scripts/rendering/test_clustered_lights.py` the gates C1-C4 (the per-pixel loop
and the committed build as references, the overflow path forced through `r.Lights.ClusterSlots 4`,
the attenuation formula for the exponent, the highlight peak for the source radius, Sponza after a
warm-up run).
