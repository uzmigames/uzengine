# Design: froxel fog correctness and sun shadowing (block B, item 4)

## Layouts
- HLSL packs a `float3` or an array element into 16-byte rows; a `float3` at a 4-byte-aligned
  offset moves to the next row, an array element takes a whole row. The engine's rule
  (`RadianceCacheTraceConstants`): scalars only in shared buffers. Applied to `emissive`
  (`VolumetricFogCB`, three shader copies), `_fogPad2` (`HeightFogCB`) and `g_windVelocity`
  (`CloudCB`, rebuilt as `float3` through a function).
- Harness `scripts/test_cbuffer_layouts.py`: `dxc -T <profile> -E main -D UZ_BINDLESS=1 -I runtime/shaders/runtime <file>`
  prints the DXIL with `; <type> <name>; ; Offset: N` lines per cbuffer field; the C++ struct text is
  cut from its header and compiled with `zig c++` next to minimal `Vec4` / `Mat4` stand-ins of the
  engine's sizes and alignment, printing `offsetof` per field; a per-buffer map ties HLSL field
  names to C++ fields (first component). The old shaders are the negative control (kept as a
  fixture under `scripts/test_cbuffer_layouts_fixtures/`).
- `static_assert(offsetof(...))` lines next to each struct keep the C++ side honest.

## Froxel placement
- Inject: `ray = normalize(unproject(ndc, z = 0 (far, reversed-Z)) − cameraPos)`,
  `forward = normalize(unproject(0, 0, 0) − cameraPos)`, `worldPos = cameraPos + ray * depth / dot(ray, forward)`
  so that the froxel's view-space depth is the slice depth, which is what the composite looks up
  (`DepthToFroxelUVW(viewDepth)`). The inverse view-projection is the unjittered one.
- Composite: `viewDepth = LinearizeDepthReversedZ(depth, nearZ, cameraFarZ)` (finite far plane)
  instead of `nearZ / depth` (11 % off at 10 m, 2x at 50 m with far 10000). `cameraFarZ` joins the
  CB (the fog's own `farZ` is the fog distance).

## Sun
- `sunInscatter = albedo * extinction * phase * sunColor * shadow`; the host writes `sunIntensity = 1`
  (the colour carries the intensity; the field stays for the layout).
- Shadow: `CascadeShadowVolume(worldPos, viewDepth)` in `shadow.hlsli`: picks the cascade by the
  split, walks outward on the sentinel, one `SampleCmpLevelZero` tap with the cascade's depth bias
  only (N = 0, NdotL = 1), the global distance fade of `CascadeShadow`. The inject includes
  `shadow.hlsli` (needs `shadowPCFPenumbra` in scope and `UZ_BINDLESS`), binds `ShadowDataCB` at
  slot 3 (b4) from `ctx.shadowAlloc`, and declares `pb.Read(shadowAtlasRes, ShaderResource)` so the
  graph orders it after the shadow pass. `r.VolumetricFog.SunShadow` (bool, default 1) travels in
  the CB as `sunShadowEnabled`.
- Dispatch `[numthreads(8, 8, 4)]` over (20, 12, 16): one thread per froxel.

## Test scene and harness
- `scripts/sponza_fog.py` writes `scenes/sponza-fog.uzscene` (Sponza + a `height_fog` entity with
  `enable_volumetric_fog`, density per cm chosen for 1/e at ~20 m) and `scripts/sponza-fog.uzproject`
  (`r.Fog 0`, so only the froxel fog runs; `r.TechPanel.Show 0`).
- `scripts/test_froxel_fog.py`: frame-wide `components` dump with and without fog, with the sun
  shadow on and off; floor pixels (z ≈ 0) at 8-25 m classified lit / shadowed by `direct_light`;
  fog in-scatter per pixel = `taa_output`(fog on) − `taa_output`(fog off) × T; gate F4a (shadowed floor),
  F5 from the manifest; F6a = DXIL identity of the shaders that include `shadow.hlsli` with the committed
  copy, F6b = the Camera C ROIs of the unchanged Sponza project within the run-to-run spread (a repeat run;
  the previous build's own cold / warm pair differed by 0.5-1.1 %); F2 / F3 on the extracted HLSL (CPU,
  `zig c++`, the `test_gdf_raw_composite` pattern).
- `scripts/fog_shaft_reference.py`: a floor slab, one wall (1 × 12 × 8 m), the sun at 40° (rotation
  [-40, 0, 0]; `SUN_DIR` = (cos, 0, sin) towards the sun), the same uniform fog. The harness ports the scatter
  loop to the CPU with the wall's analytic sun visibility at each froxel centre and compares the measured
  in-scatter ratio (shadow on / off) with the prediction: F4b fully lit rays, F4c rays crossing the shadow,
  F4d the footprint against `direct_light`. Added after the first Sponza run showed the lit-floor control
  cannot work there (0.347 lit against 0.351 shadowed): a lit floor pixel does not predict the air on the ray.
- `fogCB.farZ` = the component's `volumetricFogDistance` in centimetres (the host multiplied it by 100:
  the 64 slices spanned 5 cm to 6.4 km, 29 of them beyond 25 m; now 56 of them lie within 25 m).

## Side finding: `LinearizeDepthReversedZ`
`depth_common.hlsli` documents its function as the reversed-Z inverse but implements the standard-Z one
(near × far / (far − depth × (far − near)) returns ~near for every reversed-Z depth; the correct form,
used by GTAO and SSR, is near × far / (near + depth × (far − near))). The composite used it for a few
hours of this item and sampled slice 0 everywhere; it now carries its own `ViewDepthFromReversedZ`.
The other user, `contact_shadows.hlsli`, is a pre-existing bug in a pass that runs in Sponza; fixing the
shared function there changes Sponza, so it is left to its own item with its own evidence.

## Rejected / deferred
- The Sponza lit-floor control (the first F4 pre-registration: the sun-lit floor changes by ≤ 10 %): the
  in-scatter on the ray to a lit floor pixel comes from air that is mostly shadowed in Sponza; measured
  0.347 against 0.351 for the shadowed floor. Replaced by the shaft scene's analytic prediction.
- F6 as exact equality of the Camera C ROI means: the previous build's own cold / warm runs differed by
  0.5-1.1 %. Replaced by DXIL identity (F6a) plus the run-to-run spread (F6b).
- Fixing the layouts on the C++ side (padding): the C++ side is the producer; the HLSL copies are
  the ones that drift, and scalars are the rule already in use.
- `CascadeShadow` (16 taps, blends) in the volume: 921,600 froxels; one tap is the Frostbite /
  UE5 practice for fog with temporal filtering (which this item does not add).
- Combining the height fog and the froxel fog over the same range: a separate item.
- Fixing `LinearizeDepthReversedZ` for the contact shadows inside this item (see the side finding).
