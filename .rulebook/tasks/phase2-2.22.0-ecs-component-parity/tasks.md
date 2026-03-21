## Phase 1: DirectionalLightComponent — 15 Missing Fields
- [ ] 1.1 Add cloud shadow toggle fields to `DirectionalLightComponent` in `light_component.h`:
  - `bool castCloudShadows = true` — gate for cloud shadow rendering
  - `bool castShadowsOnClouds = true` — opaque geometry shadows on cloud layer
  - `bool castShadowsOnAtmosphere = true` — opaque geometry shadows on atmosphere
- [ ] 1.2 Add cloud shadow quality/intensity fields:
  - `float cloudShadowStrength = 1.0f` (0-1, shadow darkness)
  - `float cloudShadowOnAtmosphereStrength = 1.0f` (0-1)
  - `float cloudShadowOnSurfaceStrength = 1.0f` (0-1)
  - `float cloudShadowDepthBias = 0.0f` (-1 to 1)
  - `float cloudShadowExtent = 128.0f` (km, min 1.0)
  - `float cloudShadowMapResolutionScale = 1.0f` (0.25-8)
  - `float cloudShadowRaySampleCountScale = 1.0f` (0.25-8)
  - `Vec3 cloudScatteredLuminanceScale = {1,1,1}` (per-channel)
- [ ] 1.3 Add light shaft fields:
  - `bool enableLightShaftOcclusion = true`
  - `float occlusionMaskDarkness = 0.5f` (0-1)
  - `float occlusionDepthRange = 10000.0f` (cm, 0-500000)
  - `Vec3 lightShaftOverrideDirection = {0,0,0}` (zero = auto from rotation)
- [ ] 1.4 Add contact shadow fields to base `LightComponent`:
  - `float contactShadowLength = 0.0f` (0=disabled, screen-space fraction)
  - `bool contactShadowLengthInWorldSpace = false`
  - `float contactShadowCastingIntensity = 1.0f` (0-1)
  - `float contactShadowNonCastingIntensity = 0.0f` (0-1)
- [ ] 1.5 Add far shadow cascade fields:
  - `float shadowSourceAngleFactor = 1.0f` (0-5, ray-traced shadow softness)
  - `float shadowCascadeBiasDistribution = 1.0f` (0-1)
  - `float distanceFieldShadowDistance = 0.0f` (cm, 0=disabled)
  - `float traceDistance = 100000.0f` (cm, DFS trace distance)
  - `int32_t forwardShadingPriority = 0` (min 0)
- [ ] 1.6 Build verification — zero errors, no behavioral change (data-only additions).

## Phase 2: ExponentialHeightFogComponent — 12 Missing Fields
- [ ] 2.1 Add volumetric fog parameters to `HeightFogComponent`:
  - `float volumetricFogScatteringDistribution = 0.2f` (-0.9 to 0.9, Henyey-Greenstein)
  - `Vec3 volumetricFogAlbedo = {1,1,1}` (linear RGB)
  - `Vec3 volumetricFogEmissive = {0,0,0}` (linear RGB)
  - `float volumetricFogExtinctionScale = 1.0f` (0.1-10)
  - `float volumetricFogDistance = 6400.0f` (cm, view distance)
  - `float volumetricFogStartDistance = 0.0f` (cm)
  - `float volumetricFogNearFadeInDistance = 300.0f` (cm, 0-1000)
  - `float volumetricFogStaticLightingScatteringIntensity = 1.0f` (0-10)
  - `bool overrideLightColorsWithFogInscatteringColors = false`
- [ ] 2.2 Add inscattering texture fields:
  - `uint32_t inscatteringColorCubemapId = 0` (0=none, cubemap texture)
  - `float inscatteringColorCubemapAngle = 0.0f` (degrees, 0-360)
  - `Vec3 inscatteringTextureTint = {1,1,1}`
  - `float fullyDirectionalInscatteringColorDistance = 100000.0f` (cm)
  - `float nonDirectionalInscatteringColorDistance = 50000.0f` (cm)
- [ ] 2.3 Add atmosphere contribution:
  - `Vec3 skyAtmosphereAmbientContributionColorScale = {1,1,1}`
- [ ] 2.4 Build verification.

## Phase 3: VolumetricCloudsComponent — 10 Missing Fields
- [ ] 3.1 Add tracing quality controls to `VolumetricCloudsComponent`:
  - `float viewSampleCountScale = 0.5f` (0.05-8, quality/perf tradeoff)
  - `float reflectionViewSampleCountScale = 0.1f` (0.05-8)
  - `float shadowViewSampleCountScale = 0.5f` (0.05-8)
  - `float shadowReflectionViewSampleCountScale = 0.1f` (0.05-8)
  - `float shadowTracingDistance = 10.0f` (km, 0.1-50)
  - `float stopTracingTransmittanceThreshold = 0.01f` (0-1, early-out)
- [ ] 3.2 Add aerial perspective scattering fields:
  - `float aerialPerspectiveRayleighScatteringStartDistance = 0.0f` (km, 0-100)
  - `float aerialPerspectiveRayleighScatteringFadeDistance = 10.0f` (km, 0-100)
  - `float aerialPerspectiveMieScatteringStartDistance = 0.0f` (km, 0-100)
  - `float aerialPerspectiveMieScatteringFadeDistance = 10.0f` (km, 0-100)
- [ ] 3.3 Add sky light occlusion:
  - `float skyLightCloudBottomOcclusion = 0.2f` (0-1)
- [ ] 3.4 Add tracing distance mode enum:
  - `enum class TracingMaxDistanceMode { DistanceFromCloudLayerEntryPoint, DistanceFromPointOfView }`
  - `TracingMaxDistanceMode tracingMaxDistanceMode = TracingMaxDistanceMode::DistanceFromCloudLayerEntryPoint`
  - `float tracingStartMaxDistance = 350.0f` (km, 100-500)
  - `float tracingStartDistanceFromCamera = 0.0f` (km, 0-100)
- [ ] 3.5 Build verification.

## Phase 4: SkyLightComponent — 7 Missing Fields
- [ ] 4.1 Add cloud ambient occlusion fields to `SkyLightComponent`:
  - `bool cloudAmbientOcclusion = true`
  - `float cloudAmbientOcclusionStrength = 0.5f` (0-1)
  - `float cloudAmbientOcclusionExtent = 64.0f` (km, min 1)
  - `float cloudAmbientOcclusionMapResolutionScale = 1.0f` (0.25-8)
  - `float cloudAmbientOcclusionApertureScale = 0.0f` (0-0.1, clamped 0-1)
- [ ] 4.2 Add remaining missing fields:
  - `float skyDistanceThreshold = 150000.0f` (cm, distance beyond which geometry is treated as sky)
  - `float occlusionContrast = 0.2f` (0-1, DFAO contrast)
  - `bool captureEmissiveOnly = false` (advanced)
  - `float sourceCubemapAngle = 0.0f` (degrees, 0-360, rotation)
- [ ] 4.3 Build verification.

## Phase 5: SkyAtmosphereComponent — 3 Missing Fields
- [ ] 5.1 Add quality/art direction fields to `SkyAtmosphereComponent`:
  - `float traceSampleCountScale = 1.0f` (0.25-8, quality/perf tradeoff)
  - `float aerialPerspectiveStartDepth = 0.1f` (km, 0.001-10)
  - `float transmittanceMinLightElevationAngle = -90.0f` (degrees, -90 to 90)
- [ ] 5.2 Build verification.

## Phase 6: SceneEnvironmentGatherer + Constant Buffers
- [ ] 6.1 Update `SceneEnvironmentGatherer` to read all new fields from each component and copy them into the per-frame `SceneEnvironment` snapshot struct.
- [ ] 6.2 Add new fields to relevant constant buffer structs in `constant_buffers.h`:
  - `FogCB` — volumetric fog params, inscattering cubemap, atmosphere contribution
  - `CloudCB` — tracing quality scales, aerial perspective, occlusion
  - `LightCB` — cloud shadow params, contact shadow, light shaft
  - `SkyLightCB` — cloud AO params, distance threshold, contrast
  - `AtmosphereCB` — trace sample count scale, aerial perspective start depth
- [ ] 6.3 Update renderers that fill CBs: `fog_renderer.cpp`, `cloud_pass.cpp`, `shadow_cascade.cpp`, `deferred_lighting_pass.cpp`, `sky_atmosphere_pass.cpp` — read new fields from gathered environment, write to CB.
- [ ] 6.4 Build verification — all new CB fields zero-initialized, existing rendering unchanged.

## Phase 7: Lua Bindings
- [ ] 7.1 Add getter/setter Lua bindings for DirectionalLightComponent new fields (cloud shadow, light shaft, contact shadow, far cascade).
- [ ] 7.2 Add getter/setter Lua bindings for HeightFogComponent new fields (volumetric fog, inscattering texture).
- [ ] 7.3 Add getter/setter Lua bindings for VolumetricCloudsComponent new fields (tracing quality, aerial perspective).
- [ ] 7.4 Add getter/setter Lua bindings for SkyLightComponent new fields (cloud AO, distance threshold).
- [ ] 7.5 Add getter/setter Lua bindings for SkyAtmosphereComponent new fields.
- [ ] 7.6 Write Lua test script: set each new property, verify round-trip via getter.

## Phase 8: Validation
- [ ] 8.1 Full build — runtime + pbr_test demo, zero errors.
- [ ] 8.2 Visual verification: existing screenshots unchanged (all new fields use standard defaults).
- [ ] 8.3 Lua test: run property round-trip test script, all pass.
- [ ] 8.4 Update `docs/analysis/ue5-component-property-parity.md` with updated parity percentages.
- [ ] 8.5 Update `docs/specs/rendering/screen-space-effects.md` and relevant spec docs with new parameters.
