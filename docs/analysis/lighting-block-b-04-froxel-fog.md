# Lighting block B, item 4 — froxel fog correctness and sun shadowing

Rulebook task `phase0_lighting-block-b-04-froxel-fog-correctness` (proposal, design, spec
`froxel-fog-correctness`). Branch `feat/sponza-gi-parity`, 2026-10-06.

## Before

The froxel volumetric fog (`r.VolumetricFog`, `VolumetricFogPass`, three shaders) had never run in
a shipped scene: no scene sets `enable_volumetric_fog` on its `height_fog` entity, and no baseline
manifest lists a `FroxelFog_*` pass. Reading it with DXC's layout comments and the inverse
projection it uses:

| Buffer | HLSL side | C++ side | Effect |
|---|---|---|---|
| `VolumetricFogCB` (three shader copies) | `float3 emissive` at 192, `fogStartDistance` 204, `nearFadeInDistance` 208 | 184 / 196 / 200 | the shader read emissive = (emissiveB, fogStartDistance, nearFadeInDistance) = (0, 0, 300): a blue glow; start and fade read the padding |
| `HeightFogCB` | `float _fogPad2[2]` takes two 16-byte rows: `skyAtmContribColorScale` at 128, `sunDir` at 176 | 96 / 144 | the height fog's sun lobe pointed along the volumetric albedo (1, 1, 1); the sky and sun tints read other fields |
| `CloudCB` | `float3 g_windVelocity` at 160; the buffer 264 bytes | 152; 256 uploaded | the tail shifted by 8 bytes; `g_detailNoiseSrv` read past the upload |

- `FroxelWorldPos` fed `1 - depth / farZ` to the finite reversed-Z inverse projection: with near 5
  and far 10000 every froxel landed 5-60 cm from the camera (the CPU replica below reports the
  range), whatever the slice.
- The host multiplied `volumetricFogDistance` (documented and defaulted in centimetres, 6400) by 100: the 64
  slices spanned 5 cm to 6.4 km, 29 of them beyond 25 m.
- The sun in-scatter was `sunColor * sunIntensity` with `sunIntensity = |sunColor|` and the colour
  already carrying the intensity: 16.9x too bright for Sponza's sun (intensity 10).
- No shadow lookup in the fog, no light shafts, although the cascaded shadow atlas and its constant
  buffer are ready when the fog passes run (the god-ray shader exists and is never dispatched).

## What changed

- Layout harness `scripts/test_cbuffer_layouts.py`: each shader is compiled with DXC and the
  `; <type> <name>; ; Offset: N` comments are compared with `offsetof` of the struct text cut from
  the C++ header (compiled with `zig c++` next to stand-ins of `Vec4` / `Mat4`); the pre-fix shaders
  under `scripts/test_cbuffer_layouts_fixtures/` are the negative control; `ShadowDataCB` the
  positive one. The three buffers now declare scalars where HLSL would pad (`emissiveR/G/B`,
  `_fogPad2a/b`, `g_windVelocityX/Y/Z`), the engine's own rule for `RadianceCacheTraceConstants`,
  and the C++ headers `static_assert` the offsets.
- Froxel placement: camera + ray × (view depth / cos) with the unjittered inverse view-projection;
  the composite linearises depth with the camera's far plane (`ViewDepthFromReversedZ` = near × far /
  (near + depth × (far − near)); `cameraFarZ` joins the buffer). `r.VolumetricFog.Debug 1` makes the
  composite write its view depth, slice coordinate and transmittance instead of the fog, which the
  harness reads per pixel (F2b, and the transmittance that extracts the in-scatter in F4). `fogCB.farZ` takes the component's centimetres (64 m by default): 56 of the
  64 slices lie within 25 m (34 before).
- Sun once (`sunIntensity` = 1; the colour carries the intensity), shadowed by the cascaded shadow
  maps: `CascadeShadowVolume` in `shadow.hlsli` (one comparison tap, the cascade walk and distance
  fade of `CascadeShadow`, no normal bias), the inject pass reads the shadow atlas through the
  render graph (ordered after the shadow pass) and binds `ShadowDataCB` at b4;
  `r.VolumetricFog.SunShadow` (default on) for A/B runs. One thread per froxel
  (`[numthreads(8, 8, 4)]`, dispatch 20×12×16) instead of 64 serial slices per thread.
- Test scene `scripts/sponza_fog.py` → `scenes/sponza-fog.uzscene` + `scripts/sponza-fog.uzproject`
  (Sponza plus a `height_fog` entity with `enable_volumetric_fog`, density 0.0005 per cm = 1/e at
  20 m, uniform; `r.Fog 0` so only the froxel fog runs), harness `scripts/test_froxel_fog.py`;
  `scripts/fog_shaft_reference.py` → `scenes/fog-shaft-reference.uzscene` + `scripts/fog-shaft-reference.uzproject`
  (a floor slab, one 1 × 12 × 8 m wall, the sun at 40°, the same uniform fog): the harness ports the scatter
  loop to the CPU with the wall's analytic sun visibility at each froxel centre and compares the measured
  in-scatter ratio (shadow on / off) with that prediction. The Sponza lit-floor control of the first
  pre-registration was dropped: a lit floor pixel does not predict the air on the ray to it (0.347 lit
  against 0.351 shadowed in the first run). F6 was re-specified from exact equality (the previous build's own
  cold / warm runs differed by 0.5-1.1 %) to DXIL identity plus the run-to-run spread.

Kept: the composite's SrcAlpha / InvSrcAlpha encoding (and its 0.001 opacity cut-off), the
hard-coded ambient term, the per-centimetre density unit, the sky pixels left to the height fog,
the height fog running over the same range when both are on, the clouds' missing temporal
accumulation and unread parameters (only their constant buffer is fixed here).

## Gates (`scripts/test_froxel_fog.py`, `scripts/test_cbuffer_layouts.py`)

| Gate | Criterion | Result |
|---|---|---|
| F1 | every field of `VolumetricFogCB`, `HeightFogCB`, `CloudCB` has the same offset in DXC and in C++ (`ShadowDataCB` as the control; the pre-fix shaders differ) | PASS: 9 checks: 6 buffers agree (VolumetricFogCB inject / scatter / composite copies, HeightFogCB, CloudCB, ShadowDataCB control), 3 pre-fix shaders differ |
| F2 | froxels on the camera rays at their slice depth (CPU replica, Sponza-like camera) | PASS: 0 bad of 320, worst 3.89e-06, view depth 5.3..6052 cm |
| F2 (control) | the pre-fix reconstruction keeps every froxel within 1 m | PASS: view depth 5.0..91.2 cm |
| F2b | the composite linearises the reversed-Z depth: its view depth matches the dump's world positions (median error <= 3 %) | PASS: median 0.0011, p90 0.0047 over 30513 px, view depth 209..2546 cm; slice coordinate |diff| median 0.0005; transmittance |diff| to the CPU port median 0.0005 |
| F2b (control) | `LinearizeDepthReversedZ` of depth_common.hlsli returns ~near for every pixel (median error >= 90 %) | PASS: median error 0.989, median value 5.05 cm |
| F3 | the sun applied once | PASS: source anchors |
| F4a | Sponza: the sun shadow removes >= 40 % of the fog in-scatter over sun-shadowed floor | PASS: shadowed ratio 0.047 (131930 px); sun-lit floor 0.246 (13245 px, reported only) |
| F4b | shaft reference: rays the model finds fully lit keep >= 90 % of their in-scatter | PASS: lit ratio 0.965 (15178 px) |
| F4c | shaft reference: rays crossing the shadow volume follow the analytic prediction (MAE <= 0.1) | PASS: MAE 0.006 over 11917 px; mean ratio 0.270 vs predicted 0.269 |
| F4d | shaft reference: the analytic footprint matches the rendered floor shadow (>= 95 %) | PASS: agreement 0.999 on 30473 classified px |
| F5 | `FroxelFog_*` <= 1.5 ms at 1920x1080 with the shadow on | PASS: 0.073 ms {'FroxelFog_Composite': 0.022, 'FroxelFog_Inject': 0.026, 'FroxelFog_Scatter': 0.025} |
| F6a | every shader including `shadow.hlsli` compiles to the same DXIL with the committed copy | PASS: identical ['lighting\\deferred_lighting_ps.hlsl', 'pathtracing\\path_tracer_ps.hlsl'], differing [] |
| F6b | Sponza without fog: Camera C ROI means within the run-to-run spread of the builds | PASS: delta {'shadow_wall': '1.27e-02', 'lit_wall': '3.16e-02', 'centre': '1.23e-02'}, repeat spread {'shadow_wall': '1.29e-02', 'lit_wall': '3.22e-02', 'centre': '1.25e-02'}, threshold 6.44e-02 |


**Numbers.** CPU replica of the inject: view depths 5.3..6052 cm over the 64 slices (worst relative error 3.9e-06); the pre-fix reconstruction put them at 5.0..91.2 cm. Composite depth (shaft scene, debug capture): median relative error 0.0011 (p90 0.0047) over 30513 floor pixels at 209..2546 cm; the depth_common formula gives 5.05 cm (median error 0.989); the composite's transmittance differs from the CPU port by 0.0005 (median). Sponza fog scene (sigma 0.0005 per cm): 146742 floor pixels at 8-25 m, 13245 sun-lit and 131930 sun-shadowed; the fog in-scatter with the shadow on over without: shadowed 0.047, lit 0.246 (reported only). Shaft reference (sigma 0.0005 per cm, every 4th pixel): 30513 floor pixels at 3-30 m, 15178 on rays the model finds fully lit (ratio 0.965), 11917 on rays crossing the shadow (measured 0.270 against predicted 0.269, mean absolute error 0.006); ambient fraction nominal 0.024, fitted from the data 0.027; analytic footprint against the rendered floor shadow: 0.999 agreement on 30473 classified pixels. Fog passes (Sponza, 1080p): FroxelFog_Composite 0.022 ms, FroxelFog_Inject 0.026 ms, FroxelFog_Scatter 0.025 ms. Camera C without fog: delta to the previous build shadow_wall 1.27e-02, lit_wall 3.16e-02, centre 1.23e-02; this build's repeat spread shadow_wall 1.29e-02, lit_wall 3.22e-02, centre 1.25e-02; the previous build's cold / warm spread shadow_wall 5.30e-03, lit_wall 1.08e-02, centre 7.27e-03; threshold 6.44e-02. DXIL identical for ['lighting\\deferred_lighting_ps.hlsl', 'pathtracing\\path_tracer_ps.hlsl'], differing []. Harness 12/12.

## Side finding: `LinearizeDepthReversedZ` (depth_common.hlsli)

The function is documented as the reversed-Z inverse but implements the standard-Z one: near × far /
(far − depth × (far − near)) returns about the near plane for every reversed-Z depth (the correct form,
which GTAO and SSR carry inline, is near × far / (near + depth × (far − near))). The fog composite used it
for part of this item and sampled slice 0 for every pixel (F2b's negative control reproduces the error on
the CPU). Its other user is `contact_shadows.hlsli` (start and sample depths of the screen-space march),
a pre-existing bug in a pass that runs in Sponza; fixing the shared function changes Sponza, so it goes to
its own item with its own before / after evidence.

## Still open

- The height fog and the froxel fog both apply over the froxel range when both are on (opposite order
  to UE's combine); the fog scenes run with `r.Fog 0`.
- The froxel density is read per centimetre (0.0005 = 1/e at 20 m) while the height fog and the
  component documentation speak in metres; a unit decision is pending (the distance is now in
  centimetres on both sides).
- No temporal reprojection, no point / spot injection, sky pixels discarded by the composite, the
  SrcAlpha encoding's 0.001 opacity cut-off; the clouds keep their missing temporal accumulation
  and unread shadow parameters (their constant buffer is fixed, nothing else).
- The one-tap volume shadow has no filtering: light shafts are sharp-edged at the froxel size, and the
  shadow is sampled at the froxel-grid cell centre (12 x 12 px at 1080p), so the measured ratio
  departs from the per-pixel prediction at the shadow boundary.
- The scatter integrates along the view depth, not the ray length (the slice thickness is the depth
  step), so off-axis rays are under-attenuated by 1 / cos; kept as is.
