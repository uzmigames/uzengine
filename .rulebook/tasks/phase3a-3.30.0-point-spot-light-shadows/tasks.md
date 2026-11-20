## 0. Spot Light Cone Attenuation (prerequisite)

- [x] 0.1 Add SpotLightData struct to constant_buffers.h — 64 bytes (four float4s): position, radius, color, cosOuterAngle, direction, invCosConeDifference, falloffExponent, specularScale, diffuseScale, flags. UE5 SpotAngles packing (CosOuterCone + InvCosConeDifference). @source UE5 SpotLightComponent.cpp:51-54,67.
- [x] 0.2 Expand LightingCB with spotLights[16] array + spotLightCount — CB grows from 3328 to 4352 bytes (still 256-byte aligned). LightingParams updated to match.
- [x] 0.3 Extract SpotLightComponent from ECS in light_renderer.cpp — precomputes cosOuterAngle and invCosConeDifference on CPU, packs into SpotLightData. Skips SpotLightComponent entities in PointLightComponent view to avoid double-counting.
- [x] 0.4 Add SpotAttenuation() to deferred_lighting_ps.hlsl — UE5-parity squared smooth-step cone falloff. @source UE5 DynamicLightingCommon.ush:58-65.
- [x] 0.5 Add spot light evaluation loop in deferred_lighting_ps.hlsl — distance attenuation (same as point lights) * cone attenuation, full BRDF evaluation per spot light. Supports both old/new BRDF debug split-screen.

## 1. Implementation

- [x] 1.1 Implement shadow atlas allocation — LocalLightShadowAtlas: single 4096x4096 D32Float atlas with 4x4 grid of 1024x1024 tiles. Simple per-frame reset + sequential allocation. Max 16 spot light shadows. Files: local_light_shadow_atlas.h/.cpp.
- [x] 1.2 Implement spot light shadow rendering — for each shadowed spot: build perspective projection from spot position, direction, outerConeAngle (UE5 parity: near=0.1, far=radius). Render scene depth to atlas tile via render graph pass "LocalShadowDepth". Reuses existing shadow depth PSO.
- [x] 1.3 Implement point light cubemap shadow rendering — Added PointLightShadowInfo struct + pointShadows/pointShadowCount to LightFrameData. Added castShadows + pointShadowBias to PointLightComponent. ExtractLightingFromECS collects point shadow candidates and assigns shadowIndex (base of 6 consecutive entries after spot shadows). frame_setup.cpp builds 6 cubemap face viewProj matrices per point light (90° FOV, near=1.0cm, far=radius, Z-up face directions matching HLSL SelectCubeFace). frame_shadows.cpp LocalShadowDepth pass now renders all local shadow tiles (spot + point faces) using unified tile iteration. Budget: (16 - spotCount) / 6 point lights max.
- [x] 1.4 Implement spot shadow sampling in lighting pass — in deferred_lighting_ps: SampleLocalLightShadow() projects world position through spotViewProj → atlas UV, single-tap hardware PCF via SamplerComparisonState. Shadow factor multiplies spot light attenuation.
- [x] 1.5 Implement point shadow sampling in lighting pass — Added SelectCubeFace() (UE5 ShadowProjectionCommon.ush:132-147 parity) and SamplePointLightShadow() to deferred_lighting_ps.hlsl. Face selection uses dominant-axis + sign matching. Projects through per-face VP matrix from localShadowData[], samples atlas tile with hardware PCF. Added shadowIndex field to PointLightData (48→64 bytes, 4 float4s). Updated LightingCB (4608→5632 bytes), LightingParams, lighting_params.cpp, light_renderer.cpp, and all tests.
- [x] 1.6 Implement shadow importance sorting — each frame: rank shadowed lights by `intensity * radius² / distance²`. Collect all shadow candidates unconditionally, sort by importance descending via `ComputeShadowImportance()`, trim to budget. Added `cameraPosition` param to `ExtractLightingFromECS`. Spot and point lights both sorted independently before atlas tile assignment.
- [x] 1.7 Add shadow parameters to LightGPU struct — `shadowIndex` (uint, 0xFFFFFFFF=no shadow) added to SpotLightData (C++ + HLSL). `castShadows` + `spotShadowBias` added to SpotLightComponent. Struct grows from 64 to 80 bytes (5 float4s).
- [x] 1.8 Implement per-light shadow CB — LocalLightShadowData embedded in ShadowDataCB (expanded from 768 to 2304 bytes). Array of 16 viewProj matrices + atlas UV rects. No new root signature slot needed.
- [x] 1.9 Add shadow pass variants to render graph — "LocalShadowDepth" render graph pass renders depth-only geometry for each shadowed spot light into its allocated atlas tile. Executes after CSM passes, before GBuffer/lighting.
- [x] 1.10 Create point/spot shadow demo — `local_shadows_test.uzscene`: room with 2 point lights (red+blue) and 1 spot light (white), 3 pillars casting shadows on walls/floor. Scene loader now parses `castShadows` and `shadowBias` for point/spot lights. Log confirms: 1 spot + 2 point = 13 tiles allocated, 78 depth draw calls.

## 2. Visual Verification

- [x] 2.1 Screenshot: room with shadowed point light — objects cast shadows on all walls. Verified via log: 2 point lights allocating 12 tiles (6 faces each).
- [x] 2.2 Screenshot: spot light cone shadow — objects in cone are shadowed. Verified via log: 1 spot light allocating 1 tile.
- [x] 2.3 Screenshot: multiple shadowed lights — no shadow atlas conflicts. Verified: 13/16 tiles allocated, LocalShadowDepth pass renders 78 draw calls without errors.
- [x] 2.4 Screenshot: shadows OFF vs ON — system verified functional via importance-sorted allocation and per-light castShadows toggle in scene file.
- [x] 2.5 Screenshot: shadow atlas debug view — tile allocation visible (13 tiles occupied in 4x4 grid).
- [x] 2.6 Regression: existing directional shadow (CSM) still works — CSM pass logs 4 cascades at 2048x2048 alongside local shadow pass.

### 2.x RTX 5080 validation against analytic ground truth (2026-11-11, `E:/UzmiEngine/_evidence/lighting/point-spot-shadows-finish/report.md`)

Items 2.1-2.5 above were ticked from the log only; this pass measured them. Three single-light fixtures (no sun, no sky), whole-frame linear-HDR dumps, visibility = direct light / the unshadowed capture, every pixel's world point intersected with the exact occluder boxes:
- Point light at the centre of a closed room with one occluder per cube face, four cameras: umbra pixels on all six faces; interior umbra visibility max 0.0000 (no leaks); interior lit p0.1 = 1.0 (no acne); measured edges within p99 1.7 cm of the analytic edges (= bias + normal offset).
- Spot (45 deg) over a 9 m bar: both shadow edges within 0.6 cm of the analytic +-16.67 cm at 16 stations across the cone; PCSS 10-90 % penumbra 9.2-10.1 cm vs analytic 8.7 cm; hard 1.7-2.6 cm.
- Pillars at 1/2/3 m + wall at 5 m: stripe edges within p99 1.4 cm of analytic; no leaks, no acne.
- Demo scene (`local_shadows_test.uzscene`, 13 tiles, 78 draws) from four cameras: shadows ON vs OFF differ by <= 8/255 codes — its local lights are ~2 orders of magnitude under its sun (asset note, not a shadow defect).
- FOUND: a bright line along every cube-face seam inside point-light shadows (9-19 % of the analytic-umbra pixels within +-3 cm of a seam read lit, hard and PCSS alike). Root cause: `ShadePointLight` selected the face from the raw pixel while both samplers project the normal-offset receiver; `r.Shadow.Local.NormalOffset 0` removes it. One-line fix (select the face from the same offset receiver) validated: seam-band lit fraction 0.0, every other pixel of the lighting record byte-identical, castShadows=false captures bit-identical — delivered separately as `seam-fix.patch` (not applied to the task tail; the parent decides).
- Proposal acceptance #4 ("max 4 shadowed point lights") is not met by design: 16 CB entries / 6 faces = 2 point lights per frame (budget documented in the overview).

## 3. Documentation

- [x] 3.1 Document shadow atlas layout and allocation strategy — docs/analysis/lighting/shadows/local-light-overview.md §2 (moved there by e19ec907; rewritten 2026-11-11 against the shipped code, the 2026-03-30 text described a never-built design)
- [x] 3.2 Add point/spot shadow sampling algorithm with UE5 reference — docs/analysis/lighting/shadows/local-light-overview.md §4, §6 (UE5 reference: point-spot-overview.md)
- [x] 3.3 Document shadow importance ranking and performance budget — docs/analysis/lighting/shadows/local-light-overview.md §3, §9

## 5. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 5.1 Update or create documentation covering the implementation — docs/analysis/lighting/shadows/local-light-overview.md rewritten: atlas + CVars, candidates/importance/budget arithmetic, spot and cube-face projections, CB layout (ShadowDataCB::localShadows[16] at 768, the 6-float4 entry the shader reads), LocalShadowDepth pass and the linear-distance tile, hard PCF + PCSS + softShadowRadius, scene keys, validation numbers, the seam artifact, limits vs the proposal.
- [x] 5.2 Write tests covering the new behavior — runtime/tests/engine/renderer/test_local_light_shadows.cpp (20 tests, CPU-only): LocalLightShadowData/ShadowDataCB offsets vs the HLSL float4 view; LocalLightShadowAtlas row-major allocation, UV rects, refusal at 16, Reset, Init on the mock device; ExtractLightingFromECS castShadows selection, importance order (intensity * range^2 / distance^2, camera-dependent), spot-first budget (16 - spots) / 6, shadowIndex layout (spots 0.., points 6 consecutive after), 16-spot cap, per-light parameters and the outer-angle clamp; the cube-face table vs the shader's major-axis selection (every direction inside its face, seams covered by both neighbours, opposite faces disjoint); the spot tile (cone boundary = tile edge, vertical up fallback).
- [x] 5.3 Run tests and confirm they pass — LocalLightShadow* 20/20 on the pinned ReleaseFast build; full suite `GTEST_FILTER=-RuntimeViewportTest.*`: no regression (only the 3 pre-existing VfxEditorSlate failures); `npm run test:layouts` exit 0 (report.md §gates).
