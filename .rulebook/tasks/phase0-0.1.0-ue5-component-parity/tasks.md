## 1. Fix Wrong Defaults (Critical — visual correctness)
- [x] 1.1 HeightFogComponent: `heightFalloff` 0.5 -> 0.2 (UE5 default)
- [x] 1.2 HeightFogComponent: `color` (0.7, 0.75, 0.85) -> (1.0, 1.0, 1.0) (UE5 = white)
- [x] 1.3 HeightFogComponent: `maxOpacity` 0.9 -> 1.0 (UE5 default)
- [x] 1.4 SkyAtmosphereComponent: `groundAlbedo` (0.4, 0.4, 0.4) -> (0.3, 0.3, 0.3) (UE5 sRGB)
- [x] 1.5 SpotLightComponent: `innerAngle` 0.3 rad -> 30.0 degrees (UE5 default, store as degrees)
- [x] 1.6 SpotLightComponent: `outerAngle` 0.5 rad -> 45.0 degrees (UE5 default, store as degrees)
- [x] 1.7 CameraComponent: document vertical FOV convention, consider adding `fovAxis` enum

## 2. Standardize Units
- [x] 2.1 SpotLight cone angles: change from radians to degrees in component, convert to radians in SyncLightData/shader
- [x] 2.2 SkyAtmosphere `sunAngularRadius`: change from radians to degrees (0.5 deg default), convert internally
- [x] 2.3 Document unit convention in AGENTS.md: UzEngine = meters (not cm), angles = degrees in components
- [x] 2.4 Audit all angle parameters across codebase to confirm degrees convention

## 3. Add Missing Parameters — LightComponent Base
- [x] 3.1 Add `bool useTemperature = false` — enable Kelvin color temperature
- [x] 3.2 Add `float temperature = 6500.f` — blackbody temperature in Kelvin (D65 = daylight white)
- [x] 3.3 Add `float specularScale = 1.0f` — non-physical specular highlight multiplier (0-1)
- [x] 3.4 Add `float diffuseScale = 1.0f` — non-physical diffuse multiplier (0-1)
- [x] 3.5 Add `float volumetricScatteringIntensity = 1.0f` — volumetric fog scatter contribution
- [x] 3.6 Add `float indirectLightingIntensity = 1.0f` — GI contribution scale
- [x] 3.7 Implement Kelvin-to-RGB conversion utility (Planck blackbody -> linear RGB) — created in `engine/math/color_utils.h`

## 4. Add Missing Parameters — DirectionalLightComponent
- [x] 4.1 Add `bool atmosphereSunLight = false` — link this light as the sky atmosphere sun
- [x] 4.2 Add `int atmosphereSunLightIndex = 0` — which sun slot (0=sun, 1=moon)
- [x] 4.3 Add `float lightSourceAngle = 0.5357f` — sun disk angular diameter in degrees (UE5 default)
- [x] 4.4 Add `float lightSourceSoftAngle = 0.0f` — soft edge of sun disk in degrees
- [x] 4.5 Add `int shadowCascades = 4` — number of CSM cascades (0-4)
- [x] 4.6 Add `float cascadeDistributionExponent = 2.0f` — 1=linear, >1=closer to camera
- [x] 4.7 Add `float cascadeTransitionFraction = 0.1f` — fade region between cascades
- [x] 4.8 Add `float shadowDistanceFadeoutFraction = 0.1f` — fade at far extent
- [x] 4.9 Add `float dynamicShadowDistance = 0.f` — max CSM distance in world units (0=default)
- [x] 4.10 Add `float contactShadowLength = 0.f` — screen-space contact shadow ray length
- [x] 4.11 Add `FLinearColor atmosphereSunDiskColorScale = (1,1,1)` — sun disk tint

## 5. Add Missing Parameters — PointLightComponent
- [x] 5.1 Add `bool useInverseSquaredFalloff = true` — physically-based inverse square law
- [x] 5.2 Add `float lightFalloffExponent = 8.0f` — custom falloff exponent (when inverse square disabled)
- [x] 5.3 Add `float sourceRadius = 0.f` — sphere radius for area light approximation
- [x] 5.4 Add `float softSourceRadius = 0.f` — soft edge sphere radius
- [x] 5.5 Add `float sourceLength = 0.f` — tube/capsule length for area light
- [x] 5.6 Update PointLightData GPU struct to carry sourceRadius + falloff params

## 6. Add Missing Parameters — SpotLightComponent
- [x] 6.1 Inherits new PointLight params (sourceRadius, falloff, etc.) — verify propagation
- [x] 6.2 Verify inner/outer angle clamping: outer >= inner, both in [0, 90] degrees

## 7. Add Missing Parameters — HeightFogComponent
- [x] 7.1 Add second fog layer: `secondFogDensity`, `secondFogHeightFalloff`, `secondFogBaseHeight`
- [x] 7.2 Add `Vec3 directionalInscatteringColor = (1.0, 0.8, 0.6)` — sun-facing fog color
- [x] 7.3 Add `float directionalInscatteringExponent = 8.0f` — sun cone size
- [x] 7.4 Add `float directionalInscatteringStartDistance = 0.f` — start distance
- [x] 7.5 Add `float fogCutoffDistance = 1000000.f` — beyond this, no fog
- [x] 7.6 Add `bool enableVolumetricFog = false` — volumetric fog toggle (P3 implementation)
- [x] 7.7 Update HeightFogCB to carry new fields (second layer + directional inscattering)
- [x] 7.8 Update fog shader to support second fog layer and directional inscattering

## 8. Add Missing Parameters — SkyAtmosphereComponent
- [x] 8.1 Add `float rayleighScatteringScale = 1.0f` — artistic multiplier
- [x] 8.2 Add `float mieScatteringScale = 1.0f` — artistic multiplier
- [x] 8.3 Add `float mieAbsorptionScale = 1.0f` — Mie absorption multiplier
- [x] 8.4 Add `Vec3 mieAbsorption = (4.4e-3, 4.4e-3, 4.4e-3)` — Mie absorption coefficients km^-1
- [x] 8.5 Add `float multiScatteringFactor = 1.0f` — multiple scattering approximation
- [x] 8.6 Add `Vec4 skyLuminanceFactor = (1,1,1,1)` — sky brightness art direction
- [x] 8.7 Add `float heightFogContribution = 1.0f` — contribution to height fog
- [ ] 8.8 Add `ESkyAtmosphereTransformMode transformMode` — planet positioning mode
- [ ] 8.9 Remove `sunIlluminance` from SkyAtmosphere (should come from linked DirectionalLight)
- [ ] 8.10 Remove `sunAngularRadius` from SkyAtmosphere (should come from linked DirectionalLight `lightSourceAngle`)
- [ ] 8.11 Remove `sunDiskIntensity` from SkyAtmosphere (should come from linked DirectionalLight intensity)
- [x] 8.12 Remove `skyLightIntensity` from SkyAtmosphere (belongs on SkyLightComponent)
- [x] 8.13 Update SkyAtmosphereCB to carry new fields, remove sun fields
- [x] 8.14 Update sky shader to read sun params from PerFrameCB (linked DirectionalLight data)

## 9. Add Missing Parameters — SceneComponent
- [x] 9.1 Add `EComponentMobility mobility = Movable` — Static/Stationary/Movable enum
- [x] 9.2 Add `bool hiddenInGame = false` — hide in game mode but visible in editor
- [x] 9.3 Define `EComponentMobility` enum in a shared header — Enum defined inside the class as `SceneComponent::Mobility`

## 10. Add Missing Parameters — PrimitiveComponent
- [x] 10.1 Add `float minDrawDistance = 0.f` — cull if closer than this
- [x] 10.2 Add `float maxDrawDistance = 0.f` — cull if farther than this (0=unlimited)
- [x] 10.3 Add `bool receivesDecals = true` — allow decal projection
- [x] 10.4 Add `bool renderInMainPass = true` — include in main render pass
- [x] 10.5 Add `bool renderInDepthPass = true` — include in depth-only pass

## 11. Create SkyLightComponent (New Component)
- [x] 11.1 Create `ecs/environment/sky_light_component.h`
- [x] 11.2 Add `float intensity = 1.0f` — sky light intensity multiplier
- [x] 11.3 Add `Vec3 lightColor = (1,1,1)` — sky light tint color (linear)
- [x] 11.4 Add `ESkyLightSourceType sourceType = CapturedScene` — CapturedScene or SpecifiedCubemap
- [x] 11.5 Add `bool realTimeCapture = false` — recapture every frame vs baked
- [x] 11.6 Add `uint32_t cubemapResolution = 128` — capture resolution (power of 2)
- [x] 11.7 Add `bool lowerHemisphereIsBlack = true` — clamp lower hemisphere
- [x] 11.8 Add `Vec3 lowerHemisphereColor = (0,0,0)` — solid color for lower hemi
- [x] 11.9 Add `float occlusionMaxDistance = 100.f` — max DFAO influence distance (meters)
- [x] 11.10 Add `float minOcclusion = 0.f` — darkest AO level (0-1)
- [x] 11.11 Add `Vec3 occlusionTint = (0,0,0)` — AO shadow tint color
- [x] 11.12 Add `float skyDistanceThreshold = 1500.f` — distance threshold for sky geometry (meters)
- [x] 11.13 Wire SkyLightComponent into SceneRenderer gathering (replaces skyLightIntensity from SkyAtmosphere)
- [x] 11.14 Define `ESkyLightSourceType` enum

## 12. Sun-Atmosphere Link (Architectural Fix)
- [x] 12.1 SceneRenderer: gather DirectionalLight with `atmosphereSunLight=true`
- [x] 12.2 Pass linked light's direction, color, intensity to SkyAtmosphereCB as sun params
- [x] 12.3 Pass linked light's `lightSourceAngle` as sun angular radius
- [x] 12.4 Fallback: if no linked light, use default sun direction (45 deg from zenith)
- [x] 12.5 Update sky shader to read sun from CB (already does, just change source) — SceneRenderer now gathers DirectionalLight with atmosphereSunLight=true and passes direction/color/intensity/lightSourceAngle to SkyAtmosphereCB

## 13. RenderProxy & GPU Data Updates
- [x] 13.1 Extend `RenderProxy::light` union with: `temperature`, `useTemperature`, `sourceRadius`, `falloffExponent`, `useInverseSquaredFalloff`, `specularScale`, `diffuseScale`
- [x] 13.2 Update `PointLightData` GPU struct: add `sourceRadius`, `falloffExponent` (may need to expand from 32 bytes)
- [x] 13.3 Update `SkyAtmosphereCB`: add artistic multipliers, remove sun fields
- [x] 13.4 Update `HeightFogCB`: add second fog layer, directional inscattering, cutoff distance
- [x] 13.5 Create or extend CB for SkyLightComponent data (intensity, tint, lower hemi)

## 14. Shader Updates
- [x] 14.1 Implement `KelvinToRGB(float temperature)` utility in shaders (or CPU-side)
- [x] 14.2 Update deferred lighting shader: apply specularScale, diffuseScale per light
- [x] 14.3 Update point/spot light attenuation: support inverse square toggle + falloff exponent
- [x] 14.4 Update fog shader: second fog layer + directional inscattering
- [x] 14.5 Update sky shader: read sun from linked light data instead of hardcoded fields
- [x] 14.6 Area light approximation using sourceRadius (representative point technique)

## 15. Lua API Parity
- [ ] 15.1 Expose all new LightComponent params: `uz.scene.set_light_temperature()`, `uz.scene.set_light_specular_scale()`, etc.
- [ ] 15.2 Expose DirectionalLight cascade/atmosphere params to Lua
- [ ] 15.3 Expose PointLight falloff/source radius params to Lua
- [ ] 15.4 Expose HeightFog new params (second layer, directional inscattering) to Lua
- [ ] 15.5 Expose SkyAtmosphere artistic multipliers to Lua
- [ ] 15.6 Create `uz.scene.create_sky_light()` and `uz.scene.set_sky_light_*()` API
- [ ] 15.7 Expose SceneComponent mobility/hiddenInGame to Lua
- [ ] 15.8 Expose PrimitiveComponent draw distance to Lua

## 16. Testing
- [ ] 16.1 Unit tests: verify all default values match UE5 spec
- [ ] 16.2 Unit tests: angle degree/radian conversion correctness
- [ ] 16.3 Unit tests: Kelvin-to-RGB conversion against known values (3000K warm, 6500K white, 10000K blue)
- [ ] 16.4 Unit tests: SkyLightComponent creation and gathering
- [ ] 16.5 Unit tests: Sun-Atmosphere link (DirectionalLight -> SkyAtmosphere)
- [ ] 16.6 Visual verification: demo with all light types + fog + sky showing correct defaults
- [ ] 16.7 Visual verification: compare UzEngine default scene vs UE5 default scene

## 17. Documentation
- [ ] 17.1 Update `docs/specs/renderer.md` with new component parameters
- [ ] 17.2 Update `docs/lua-api-reference.md` with new Lua bindings
- [x] 17.3 Update AGENTS.md with unit convention (meters, degrees)
- [ ] 17.4 Create comparison table doc: `docs/reference/ue5-parameter-parity.md`
- [x] 17.5 Update CHANGELOG.md
