## 1. LTC LUT Precomputation and Data Assets

- [ ] 1.1 Precompute `ltc_mat.dds` (64x64 RG16F) containing the 3x3 LTC matrix inverse coefficients encoded as RGBA using the Heitz et al. reference precomputation tool
- [ ] 1.2 Precompute `ltc_amp.dds` (64x64 RG16F) containing amplitude and Fresnel normalization factors for the GGX BRDF LUT lookup
- [ ] 1.3 Store both `.dds` files under `data/engine/` and register them in the engine asset manifest so they load at startup via the existing `AssetManager`
- [ ] 1.4 Bind `ltc_mat.dds` and `ltc_amp.dds` as `Texture2D` samplers in the PBR pixel shader root signature (assign dedicated register slots in `shader-library.md` cbuffer table)

## 2. HLSL Area Light Evaluation Library

- [ ] 2.1 Create `shaders/include/AreaLightEval.hlsli` with function `EvalRectLight(float3 P, float3 N, float3 V, float roughness, float4x4 ltcMat, float2 ltcAmp, float3 lightPos, float3 lightRight, float3 lightUp, float2 halfExtents, bool twoSided)` returning `float3 specular` and `float3 diffuse`
- [ ] 2.2 Implement `EvalTubeLight` in `AreaLightEval.hlsli`: compute the closest point on the tube segment to the shading point, form a representative direction, evaluate LTC using an equivalent capsule solid-angle approximation
- [ ] 2.3 Implement `EvalDiskLight` in `AreaLightEval.hlsli`: transform disk endpoints into the LTC-normalized hemisphere and integrate using the polygon clipping formula
- [ ] 2.4 Implement the `LTC_Evaluate` helper that transforms the area polygon into the cosine hemisphere, clips against the hemisphere, and computes the solid-angle integral used by all three `Eval*` functions
- [ ] 2.5 Add diffuse fallback path in `EvalRectLight` using the analytic polygon irradiance formula (no LUT required for Lambertian diffuse) so diffuse is exact rather than LTC-approximated

## 3. CAreaLight ECS Component

- [ ] 3.1 Define `CAreaLight` struct in `src/ecs/components/CAreaLight.h` with fields: `type` (enum `AreaLightType { Rect, Tube, Disk }`), `width` (float, meters), `height` (float, meters), `length` (float, meters, tube only), `color` (float3), `intensity` (float, lux), `two_sided` (bool), `cast_specular` (bool), `cast_diffuse` (bool), `range` (float, meters)
- [ ] 3.2 Add `no_shadows` flag field to `CAreaLight` initialized to `true`, with a `static_assert` comment documenting it as a known limitation of this implementation phase
- [ ] 3.3 Register `CAreaLight` in the ECS component registry and ensure it serializes/deserializes correctly to/from the `.uzscene` format

## 4. PBR Pixel Shader Integration

- [ ] 4.1 In `shaders/pbr/PBR.hlsl`, add a loop over the clustered area light list, calling `EvalRectLight`, `EvalTubeLight`, or `EvalDiskLight` based on the light type field packed in the light struct
- [ ] 4.2 Pack the area light HLSL struct in the light index buffer: `type` (uint, 2 bits), `width`, `height`, `length`, `color`, `intensity`, `world_to_light` (float4x3 transform), `two_sided` (bool bit)
- [ ] 4.3 Accumulate `diffuse += eval.diffuse * cast_diffuse_mask` and `specular += eval.specular * cast_specular_mask` into the PBR lighting accumulator before tonemapping

## 5. Clustered Shading AABB Integration

- [ ] 5.1 In `src/renderer/passes/ClusteredLightPass.cpp`, compute the AABB bounding volume for each `CAreaLight`: for rect/disk use the OBB corners transformed to world space + range sphere; for tube use capsule AABB
- [ ] 5.2 Insert area light AABBs into the cluster assignment compute dispatch alongside point and spot lights; pack area light index into the same light index list buffer with a type discriminator bit
- [ ] 5.3 Raise the maximum lights-per-cluster count in `ClusterLightAssign.hlsl` to account for mixed point/spot/area sources in dense scenes

## 6. Editor Gizmos

- [ ] 6.1 Create `src/editor/gizmos/AreaLightGizmo.cpp` implementing `DrawAreaLightGizmo(const CAreaLight& light, const CTransform& transform)` that issues wireframe DX12 draw calls for the appropriate shape: rectangle outline for `Rect`, capsule edges for `Tube`, circle outline for `Disk`
- [ ] 6.2 Draw a range sphere gizmo around the light origin using the existing gizmo wireframe sphere helper, respecting the `range` field
- [ ] 6.3 Register the gizmo with the editor's gizmo dispatch table so it renders in all scene viewport passes when the entity is selected

## 7. Inspector Panel

- [ ] 7.1 Add `CAreaLight` Inspector widget in `src/editor/panels/InspectorPanel.cpp`: type enum dropdown (`Rect / Tube / Disk`), color picker, intensity float slider (0–100 000 lux), range float slider
- [ ] 7.2 Show `width` and `height` sliders only when type is `Rect` or `Disk`; show `length` slider only when type is `Tube`; hide irrelevant controls dynamically
- [ ] 7.3 Expose `two_sided`, `cast_specular`, `cast_diffuse` as checkboxes; show `no_shadows` as a read-only disabled checkbox labelled "Shadows (not yet supported)"

## 8. Testing

- [ ] 8.1 Write `tests/renderer/test_area_light_eval.cpp`: unit tests for `EvalRectLight` with known analytical ground-truth values for a perpendicular rect directly above a shading point; verify diffuse and specular output match reference within 1% tolerance
- [ ] 8.2 Test `EvalTubeLight` at zero length (degenerates to point) and verify result matches point-light LTC evaluation within floating-point epsilon
- [ ] 8.3 Test `EvalDiskLight` with zero radius (degenerate) and very large radius (approaches hemisphere) and verify output is clamped and finite
- [ ] 8.4 Test `CAreaLight` component serialization round-trip: create component, serialize to `.uzscene` string, deserialize, compare all fields including enum value
- [ ] 8.5 Test `ClusteredLightPass` AABB computation for each area light type: verify rect AABB fully contains all four corners, tube AABB contains both end caps, disk AABB contains circumscribed box
- [ ] 8.6 Test that `cast_specular = false` produces zero specular contribution from `EvalRectLight`
- [ ] 8.7 Test that `two_sided = false` produces zero contribution when the shading point is behind the rect plane (normal facing away), and `two_sided = true` produces non-zero contribution in the same configuration
- [ ] 8.8 Verify all tests achieve ≥95% line and branch coverage on `AreaLightEval.hlsli` logic paths via the HLSL unit test harness or equivalent CPU-side reference implementation
- [ ] 8.9 Run the full renderer test suite to confirm no regressions in point/spot light evaluation after the PBR shader changes

## 9. Documentation

- [ ] 9.1 Update `docs/specs/renderer.md`: document `CAreaLight` component fields, LTC LUT slots, and the cluster extension for area sources
- [ ] 9.2 Update `docs/specs/shader-library.md`: document `ltc_mat` and `ltc_amp` texture register assignments and the `AreaLightEval.hlsli` public function signatures
- [ ] 9.3 Update `docs/specs/editor-gizmos.md`: document `AreaLightGizmo` shape rendering behavior and gizmo selection highlight
- [ ] 9.4 Add "Area Lights" section to `docs/specs/editor-panels.md` describing Inspector controls and the `no_shadows` known limitation notice
