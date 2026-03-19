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
- [ ] 1.6 Implement shadow importance sorting — each frame: rank shadowed lights by `screenArea * intensity / distance²`. Highest-importance lights get shadow atlas tiles first. Lower-priority lights are unshadowed.
- [x] 1.7 Add shadow parameters to LightGPU struct — `shadowIndex` (uint, 0xFFFFFFFF=no shadow) added to SpotLightData (C++ + HLSL). `castShadows` + `spotShadowBias` added to SpotLightComponent. Struct grows from 64 to 80 bytes (5 float4s).
- [x] 1.8 Implement per-light shadow CB — LocalLightShadowData embedded in ShadowDataCB (expanded from 768 to 2304 bytes). Array of 16 viewProj matrices + atlas UV rects. No new root signature slot needed.
- [x] 1.9 Add shadow pass variants to render graph — "LocalShadowDepth" render graph pass renders depth-only geometry for each shadowed spot light into its allocated atlas tile. Executes after CSM passes, before GBuffer/lighting.
- [ ] 1.10 Create point/spot shadow demo — room with 2 point lights and 1 spot light. Objects casting shadows on walls/floor from all light sources. Toggle shadows per light.

## 2. Visual Verification

- [ ] 2.1 Screenshot: room with shadowed point light — objects cast shadows on all walls
- [ ] 2.2 Screenshot: spot light cone shadow — objects in cone are shadowed
- [ ] 2.3 Screenshot: multiple shadowed lights — no shadow atlas conflicts
- [ ] 2.4 Screenshot: shadows OFF vs ON — dramatic difference in believability
- [ ] 2.5 Screenshot: shadow atlas debug view — tile allocation visible
- [ ] 2.6 Regression: existing directional shadow (CSM) still works

## 3. Documentation

- [ ] 3.1 Document shadow atlas layout and allocation strategy
- [ ] 3.2 Add point/spot shadow sampling algorithm with UE5 reference
- [ ] 3.3 Document shadow importance ranking and performance budget
