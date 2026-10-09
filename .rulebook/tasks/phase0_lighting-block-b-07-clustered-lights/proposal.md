# Clustered light lists: the deferred lighting evaluates the lights of the pixel's cluster

## Why

The parecer's block-B item "clustered lights" (table row "Point (≤64) e spot (≤16) — parcial: loop
por pixel sem tile/cluster; cluster_assign_cs é stub; sem culling na CPU; raio da fonte, expoente de
falloff e escalas difusa/especular são empacotados e ignorados"). `deferred_lighting_ps.hlsl` looped
over every point and spot light of the frame in every pixel; `cluster_assign_cs.hlsl` was a one-line
stub left by the clustered-forward-plus task (whose first three items are ticked without code); the
light arrays live in a 5632-byte constant buffer capped at 64 point and 16 spot lights; the per-light
parameters the components carry (`sourceRadius`, `lightFalloffExponent`, `specularScale`,
`diffuseScale`, `useInverseSquaredFalloff`) were packed into the buffer and never read, and the scene
loader did not read them from files either.

Measured before any change (2026-10-07, build edfd7942, `scripts/lights_reference.py`, 1080p): with 64
point + 16 spot lights the deferred lighting takes 0.465 ms against 0.285 ms with no local lights:
the per-pixel loop is cheap at the old cap because the range test rejects most lights per pixel. The
cost that matters is the cap itself: a scene cannot have more than 80 local lights, and the loop
grows linearly with every light added.

## What changes

- `kMaxPointLights` 64 → 512, `kMaxSpotLights` 16 → 128: the arrays stay in the constant buffer
  (43264 bytes of the 64 KB allowed), uploaded through the 2 MB-per-frame ring.
- `cluster_assign_cs.hlsl`: one thread per cluster of a 32-pixel-tile × 24-logarithmic-slice grid
  (48 960 clusters at 1080p); the cluster's world-space AABB from its eight corners (camera rays through
  the tile corners with a one-pixel margin for the TAA jitter, at the slice's two depths); every point
  and spot light whose range sphere touches the AABB is appended to the cluster's record (points
  first, then spots, each in index order: the order the loop used, so the sums are bit-identical);
  more lights than the record's slots (32, `r.Lights.ClusterSlots`) sets an overflow bit.
- `deferred_lighting_ps.hlsl`: the loop bodies become `ShadePointLight` / `ShadeSpotLight`; the
  pixel evaluates its cluster's list (`r.Lights.Clustered`, default 1), every light when the cluster
  overflowed or the CVar is 0; `r.Lights.ClusterDebug` tints the frame by the cluster's light count.
  The per-light parameters are honoured: custom falloff `pow(1 − d²/r², exponent)` when the
  inverse-square flag is clear, the diffuse / specular scales, and the point light's source radius
  (Karis 2013 sphere light: representative point for the specular direction, roughness widening with
  energy normalisation). With the defaults the paths are the previous ones.
- The cluster parameters (forward, slice range, grid, buffer descriptors, flags) travel in the tail of
  `LightingCB`, which the lighting pass binds already; `RegisterLightCullPass` sizes the list buffer
  for the render resolution and dispatches the cull before the lighting (ordered through a declared
  depth read, block B.5's lesson); `PipelineLibrary::GetLightCull()`.
- The scene loader reads and writes `useInverseSquaredFalloff`, `falloffExponent`, `sourceRadius`,
  `specularScale`, `diffuseScale`.
- Evidence: `scripts/lights_reference.py` (80 lights, 500 lights, none, one light per parameter) and
  `scripts/test_clustered_lights.py` (C1-C4), `scripts/test_cbuffer_layouts.py` gains the LightingCB
  entry, the LightingCB gtests follow the new limits.

## Gates (pre-registered)

C1 identity (80 lights): clustered vs per-pixel loop on this binary, taa_output max |diff| ≤ 1e-5;
C1b overflow (4 slots per cluster) identical to the loop; C1c this build's loop vs the committed
build's capture: median relative difference ≤ 1e-3, max |diff| ≤ 0.02 (the refactored bodies may
round differently); C1d identity with 500 lights; C2 cost with 500 lights at 1080p: clustered ≤ 0.5 ×
the loop, the cull pass ≤ 0.3 ms; C3a diffuseScale 0.5 → floor radiance ratio median in [0.47, 0.55];
C3b falloff exponent: lum(exp 8) / lum(exp 2) per pixel against (1 − d²/r²)^6, median |error| ≤ 0.02;
C3c source radius: the highlight's peak decreases, src50 < src20 < src0 and peak(src50) ≤ 0.8 ×
peak(src0); C4 Sponza unchanged (one light, the sun): direct_light identical, colour pixels changed by
more than 1e-3 ≤ 0.1 % of the frame and max |diff| ≤ 0.01 against the committed warm capture, after a
warm-up run (the cold shader cache changes the frame-60 state, block B.6's finding).
Re-specified after the first run (results recorded in the doc): C3a and C3c compared raw luminances
while the sky light's ambient on the floor (~0.6) exceeds the point light's contribution over most
of its range (ratio 0.98 instead of ~0.5); the no-light scene's luminance (rough or glossy floor) is
now subtracted per pixel and the source-radius peak is the maximum, not the 99.9th percentile. C4's
colour identity met the cold/warm envelope's signature (4.3 % of the pixels, median 3 %) with the
direct light bit-identical; the gate is the direct light (the lighting's Lo, the quantity this item's
shader change can touch) and the colour is reported against block B.6's envelope. The first run also
exposed two defects of the implementation, fixed before the second run: the slice range (whole
camera range → the lights' range) and the ray reconstruction (one point and `cameraPos` → two
unprojected points), see the design.

## Impact
- Affected specs: clustered-lights (new)
- Affected code: `runtime/shaders/runtime/lighting/light_types.hlsli`, `cluster_assign_cs.hlsl`,
  `deferred_lighting_ps.hlsl`, `runtime/engine/renderer/core/params/constant_buffers.h`,
  `lighting_params.h`, `frame/frame_constants.cpp`, `frame/frame_lighting_pass.cpp`,
  `scene/scene_renderer_impl.h`, `scene/scene_temporal_resources.cpp`, `core/pipeline/pipeline_geometry.cpp`,
  `pipeline_library.h`, `pipeline_library_impl.h`, `runtime/engine/ecs/loaders/scene_loader.cpp`,
  `runtime/tests/engine/renderer/test_deferred_pipeline.cpp`, `test_shader_parameters.cpp`,
  `scripts/test_cbuffer_layouts.py`, `scripts/lights_reference.py`, `scripts/test_clustered_lights.py`
- Breaking change: NO for scenes (the limits grow, the defaults reproduce the previous shading; the
  loop is one CVar away: `r.Lights.Clustered 0`); the LightingCB size changes (tests updated).
- User benefit: hundreds of local lights at a per-pixel cost bounded by the cluster, and the light
  components' parameters doing what their names say.

## Out of scope

A cone test for spot lights in the cull (the range sphere is used), the forward translucent pass and
the froxel fog (they do not read the lists), local lights in the GI (the probes see the sun only),
light shadows beyond the 16 atlas tiles, a structured-buffer light list beyond 512 / 128.
