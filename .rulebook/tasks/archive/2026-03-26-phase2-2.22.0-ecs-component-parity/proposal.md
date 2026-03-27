# Proposal: phase2-2.22.0-ecs-component-parity

## Priority: P1 — HIGH

## Why

UzEngine's ECS components are missing critical configurable parameters compared to standard implementations. Of 9 rendering components analyzed, only 3 (PointLight, SpotLight, StaticMesh) have full parameter parity. The remaining 6 are missing 47+ parameters total — blocking runtime configurability via Lua scripts and the future editor inspector panel. Without these parameters, artists and scripters cannot control cloud shadows, atmosphere positioning, volumetric fog quality, or post-process effects.

**Current Parity Status:**

| Component | UzEngine Fields | Standard Fields | Parity | Priority |
|-----------|----------------|-----------------|--------|----------|
| DirectionalLightComponent | 29 | 44+ | 66% | CRITICAL |
| ExponentialHeightFogComponent | 15 | 32 | 47% | HIGH |
| VolumetricCloudsComponent | 16 | 26 | 62% | HIGH |
| SkyLightComponent | 13 | 20 | 65% | MEDIUM |
| SkyAtmosphereComponent | 26 | 28 | 93% | MEDIUM |
| PostProcessComponent | ~15 | 100+ | ~15% | DEFERRED (separate task) |
| PointLightComponent | 26 | 26 | 100% | DONE |
| SpotLightComponent | 30 | 30 | 100% | DONE |
| StaticMeshComponent | 20 | 20 | 100% | DONE |

## What Changes

### Phase 1: DirectionalLightComponent (15 missing fields)
Cloud shadow controls, far cascade system, light shaft parameters, contact shadow. Data-only additions — rendering systems consume them when implemented.

### Phase 2: ExponentialHeightFogComponent (12 missing fields)
Volumetric fog params, inscattering cubemap, atmosphere contribution scale.

### Phase 3: VolumetricCloudsComponent (10 missing fields)
Tracing quality scales, aerial perspective, early-out threshold, shadow tracing distance.

### Phase 4: SkyLightComponent (7 missing fields)
Cloud AO parameters, sky distance threshold, occlusion contrast.

### Phase 5: SkyAtmosphereComponent (3 missing fields)
TraceSampleCountScale, aerial perspective start depth, transmittance min elevation.

### Phase 6: SceneEnvironmentGatherer + Constant Buffers
Wire new fields through the gathering pipeline into GPU constant buffers.

### Phase 7: Lua Bindings
Expose all new parameters to Lua scripting API.

## Impact
- Affected code:
  - `runtime/engine/ecs/lighting/light_component.h`
  - `runtime/engine/ecs/environment/height_fog_component.h`
  - `runtime/engine/ecs/environment/volumetric_clouds_component.h`
  - `runtime/engine/ecs/environment/sky_atmosphere_component.h`
  - `runtime/engine/ecs/environment/sky_light_component.h`
  - `runtime/engine/renderer/scene/scene_environment_gatherer.cpp`
  - `runtime/engine/renderer/core/params/constant_buffers.h`
  - Lua binding files
- Breaking change: NO — all new fields have standard defaults
- User benefit: Full runtime configurability of all rendering components via script and editor

## Analysis Documents
- `docs/analysis/ue5-component-property-parity.md` — full property-by-property comparison
- `docs/analysis/ue5-parity-summary.md` — executive summary with priorities
- `docs/analysis/ue5-property-inventory.csv` — machine-readable inventory
- `docs/analysis/IMPLEMENTATION-CHECKLIST.md` — agent-ready implementation guide
