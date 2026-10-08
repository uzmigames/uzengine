# Environment-MIS specular: black on smooth metals, made an estimator and measured

## Why

Block B.5's side finding: the environment-MIS specular (`EnvMIS_Temporal`, the term that replaces the
split-sum specular on smooth metals whenever the SDF reflections are off) is black in every scene
without an HDRI: the chrome sphere of `chrome_test` is a black disc, the chrome floor of the reflection
reference shows nothing of the sky with SSR off, although the sampler builds its alias table from a sky
cubemap of positive luminance. Reading the pass for this item found four defects, each enough on its
own to make the term wrong:

1. The pass never binds the per-frame constants at b1 for its compute dispatch (it binds the root
   constants and the UAV table only); the shader reads the camera position and the sky light tint from
   whatever the previous compute pass left in that root slot.
2. The estimator draws environment samples only, but weights them with the balance heuristic
   `p_env / (p_env + p_brdf)` as if a BRDF-sampling strategy existed. The missing half is exactly the
   one that carries a smooth lobe: on a mirror the environment strategy hits the lobe with probability
   ~1e-6 per sample, so the estimate converges to zero; on rough metals it converges to a fraction.
3. The pdf is too small by sin(theta): the pyramid stores luminance × sin(theta), and the solid-angle
   pdf divides by the texel's solid angle, which carries the same sin(theta); the code multiplied the
   stored value by texelCount / (2π²) only.
4. The pass writes alpha = 1 on every lit pixel (the coverage test only gates the estimate's value),
   and the lighting replaces the split-sum specular wherever alpha > 0 (block A.3's "replace"), so every
   dielectric and rough metal lost its environment specular in every scene with `r.GI.Reflections 0`.
5. The sampler's directions are in the cube's frame: the engine's cubemaps follow the DX convention
   with +Y up and every consumer converts world directions with `WorldToCubemap` (y and z swapped),
   while the sampler's equirect mapping treated the cube as Z-up and the pass used its directions as
   world directions. Found by dumping the environment: its luminance varied with the azimuth and was
   symmetric about the horizon, and the mirrored view's sky matched the dump only after the swap.
6. The alias table's accept test took the fractional part of `u.x × texelCount`, but `u.x` had 16 bits
   and the table 131072 entries, so that fraction was always 0: every sample accepted its own (even)
   texel and the alias was never taken. The environment was sampled uniformly while the estimator
   divided by the luminance pdf (+11 % at roughness 0.3, +25 % at 0.5 against the CPU integral; a
   numpy replica of the sampling reproduces 1.12 / 1.27). The accept test now uses the second uniform
   and the randoms carry 32-bit floats.

Measured before any change (2026-10-08, build 5470d37f, `scripts/test_env_mis.py --before`): the
numbers are in the item document's "Before" table (the chrome floor's sky class against F × the
mirrored view, the dielectric boxes' specular, the chrome sphere's specular, the pass cost).

## What changes

- `env_mis_temporal_cs.hlsl`: a two-strategy estimator (environment samples through the alias table,
  BRDF samples through the visible GGX normals; balance heuristic with the visible-normal pdf used on
  both sides; 4 + 4 samples per pixel; the sky light tint applied; the EMA as before), coverage in alpha
  (1 on metals below the roughness threshold, fading to 0 across the thresholds).
- `envmap_sampling.hlsli`: the solid-angle pdf divides by sin(theta) (one helper used by the sample
  and the evaluation paths).
- `env_mis_pass.cpp` / `frame_lighting.cpp`: the frame's PerFrameCB allocation is bound at b1 for the
  dispatch.
- `deferred_lighting_ps.hlsl`: the estimate is blended over the split-sum specular by its coverage
  (and takes the split-sum's specular occlusion); `r.EnvMIS` (default 1 = the pass active, as before)
  switches the pass off for the control.
- Diagnostics: when a region dump is active the sampler writes the environment it builds from as an
  equirect radiance map (`env_equirect.f32` + `.json`), the ground truth of the harness.
- Evidence: `scripts/test_env_mis.py` (E1-E7) on the reflection reference (the chrome floor against
  the mirrored view; the roughness 0.3 and 0.5 metal floors, new variants of
  `scripts/reflection_reference.py`, against a CPU integral of the dumped environment; the dielectric
  boxes), `chrome_test` (the PMREM path) and Sponza.

## Gates (pre-registered)

E1 the chrome floor reflects the sky: floor pixels whose reflected ray sees the sky match F × the
mirrored view, median relative error ≤ 0.15 (SSR off). E2 energy: on the roughness 0.3 and 0.5 metal
floors the estimate (`specular_indirect`) against the CPU integral of the dumped environment, median
ratio within [0.85, 1.15]. E2m the chrome floor against F × the dumped environment along the reflected
ray, median relative error ≤ 0.15. E3 coverage: the dielectric boxes' `specular_indirect` equals the
`r.EnvMIS 0` value (max |diff| ≤ 1e-4) and is not zero. E4 the chrome sphere of `chrome_test` against
F × the dumped PMREM environment along the reflected ray, median relative error ≤ 0.2. E5 the
`EnvMIS_Temporal` pass ≤ 1.5 ms at 1080p on the reference scene. E6 Sponza unchanged (`r.GI.Reflections
1` keeps the pass out): positions and direct light identical to the committed capture, colour within the
loading-state envelope. E7 temporal stability: mean per-frame |Δ luminance| over the roughness 0.5
floor ≤ 2 % of its median luminance. Reported, not gated: the split-sum path against the same
references, the cost before.

## Impact
- Affected specs: env-mis (new)
- Affected code: `runtime/shaders/runtime/lighting/env_mis_temporal_cs.hlsl`,
  `runtime/shaders/runtime/include/envmap_sampling.hlsli`,
  `runtime/shaders/runtime/ibl/envmap_luminance_pyramid_cs.hlsl`,
  `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`,
  `runtime/engine/renderer/passes/lighting/env_mis_pass.{h,cpp}`,
  `runtime/engine/renderer/ibl/env_map_sampler.{h,cpp}`, `frame/frame_lighting.cpp`,
  `frame/frame_lighting_pass.cpp`, `frame/frame_constants.cpp`, `scene/scene_renderer_impl.h`;
  `scripts/reflection_reference.py` (rough floors), `scripts/test_env_mis.py`
- Breaking change: NO (Sponza identical by gate; scenes with the pass active change by design: metals
  reflect the environment, dielectrics regain their split-sum specular)
- User benefit: metals reflect the sky and the HDRI instead of rendering black; the split-sum specular
  of everything else is no longer discarded.

## Out of scope

Reprojection of the history under camera motion (the EMA smears), the pass on resize (`OnResize` is
never called), a sun disc captured in the sky cubemap counted by both the analytic sun and the
environment term (shared with the split-sum path), the SDF reflection layer. Found on the way and left
to its own decision: the sky cubemap capture scales the sun by the length of the illuminance vector
(`skySunColor.Length()`, √3 × the per-channel value of a white sun) while the sky pass uses the
per-channel illuminance, so the environment every image-based term samples (the prefiltered specular,
the sky SH, this estimate) is √3 brighter than the sky the frame renders; E1 measures it. Fixing the
capture changes the ambient of every atmosphere scene, Sponza's calibrated look included, so it is
recorded in known-issues, not changed here. Also found: the sphere of `sample/content/meshes/sphere.uzmesh`
renders inside-out (the visible positions are the far hemisphere's, the normals face away from the camera),
so `chrome_test` cannot measure the estimate; E4 is measured on the chrome floor under the same PMREM
(`reflection-reference-hdri`) and the sphere is reported. E3 is measured on the boxes' interior: the
lighting reads the previous frame's estimate, jittered by the TAA, so a silhouette pixel can carry the
neighbouring floor's coverage (0.3 % of the boxes' pixels; the one-frame latency is listed as open).
