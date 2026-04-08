# Proposal: phase0-0.1.0-ue5-component-parity

## Why

UzEngine components have incorrect default values, inconsistent units, missing parameters, and architectural differences compared to UE5. A deep comparison revealed ~50 discrepancies across all 10 component types. The engine must match UE5's parameter base exactly (defaults, units, naming) to ensure artists and programmers have a familiar, production-quality foundation. Without this, every downstream system (lighting, shadows, atmosphere, fog) will produce visually wrong results and require per-project hacks.

## What Changes

### A. Fix Wrong Defaults (Critical — visual correctness)
1. **HeightFogComponent**: `heightFalloff` 0.5 -> 0.2, `color` (0.7,0.75,0.85) -> (1,1,1), `maxOpacity` 0.9 -> 1.0
2. **SkyAtmosphereComponent**: `groundAlbedo` (0.4,0.4,0.4) -> (0.3,0.3,0.3)
3. **SpotLightComponent**: `innerAngle` 0.3rad -> 0.5236rad (30deg), `outerAngle` 0.5rad -> 0.7854rad (45deg)
4. **CameraComponent**: document that `fov=60` is vertical FOV (UE5 uses 90 horizontal)

### B. Standardize Units (Critical — API consistency)
5. All angle parameters in components should store **degrees** (convert to radians internally). Affects: SpotLight cone angles, SkyAtmosphere sunAngularRadius, Camera FOV (already degrees).
6. Document unit convention: UzEngine uses **meters** (not cm like UE5), all distances in meters.

### C. Add Missing Parameters to Existing Components
7. **LightComponent base**: add `useTemperature`, `temperature` (6500K default), `specularScale`, `diffuseScale`
8. **DirectionalLightComponent**: add `atmosphereSunLight` (bool), `atmosphereSunLightIndex` (int), `lightSourceAngle` (degrees, 0.5357 default), `shadowCascades` (int, 4), `cascadeDistributionExponent` (float, 2.0)
9. **PointLightComponent**: add `useInverseSquaredFalloff` (bool, true), `lightFalloffExponent` (float, 8.0), `sourceRadius` (float, 0), `softSourceRadius` (float, 0), `sourceLength` (float, 0)
10. **SpotLightComponent**: (inherits new point light params, cone angles in degrees)
11. **HeightFogComponent**: add `secondFogDensity`, `secondFogHeightFalloff`, `secondFogBaseHeight`, `directionalInscatteringExponent`, `directionalInscatteringStartDistance`, `directionalInscatteringColor`, `fogCutoffDistance`
12. **SkyAtmosphereComponent**: add `rayleighScatteringScale`, `mieScatteringScale`, `mieAbsorptionScale`, `mieAbsorption`, `multiScatteringFactor`, `skyLuminanceFactor`, `heightFogContribution`, `transformMode`. Remove `sunIlluminance`/`sunAngularRadius`/`sunDiskIntensity` (should come from DirectionalLight link). Remove `skyLightIntensity` (should be on SkyLightComponent).
13. **CameraComponent**: add `constrainAspectRatio` (bool), `postProcessBlendWeight` (float)
14. **SceneComponent**: add `mobility` enum (Static/Stationary/Movable), `hiddenInGame` (bool)
15. **PrimitiveComponent**: add `minDrawDistance`, `maxDrawDistance`, `receivesDecals` (bool)

### D. Create Missing Component
16. **SkyLightComponent** — new component with: `intensity`, `color`, `sourceType` (CapturedScene/Cubemap), `cubemapResolution` (128), `realTimeCapture` (bool), `lowerHemisphereIsBlack` (bool, true), `lowerHemisphereColor`, `occlusionMaxDistance`, `minOcclusion`, `occlusionTint`

### E. Architectural Fix
17. **Sun-Atmosphere link**: DirectionalLight with `atmosphereSunLight=true` drives the sky atmosphere sun direction/color. SkyAtmosphereComponent no longer stores sun parameters directly. SceneRenderer gathers the linked light.

### F. Render Proxy & Constant Buffer Updates
18. Update `RenderProxy::light` union to carry new fields (temperature, sourceRadius, falloff, etc.)
19. Update `PerFrameCB` or create new CB for sky light data
20. Update `LightingCB::PointLightData` to carry sourceRadius, falloffExponent
21. Update shaders to use new parameters (area light approximation, temperature-to-color conversion)

## Impact
- Affected specs: `docs/specs/renderer.md`, `docs/specs/lua-scripting.md` (Lua API parity)
- Affected code: `ecs/lighting/`, `ecs/environment/`, `ecs/scene/`, `renderer/core/constant_buffers.h`, `renderer/scene_renderer.cpp`, `shaders/`
- Breaking change: **YES** — default values change, some parameters renamed/moved
- User benefit: Exact UE5 parameter parity means familiar workflow, correct visual defaults, and ability to port UE5 lighting setups directly
