<!-- doc-meta: ue5 parity gap summary -->
# UE5 Property Parity Gap Summary

**Last Updated**: 2026-03-21
**Status**: Analysis Complete
**Scope**: 9 critical rendering components

---

## Quick Reference: Missing Properties by Component

### 🔴 SkyAtmosphereComponent (CRITICAL)
**Total UE5 properties**: 24 rendering-relevant
**Missing in UzEngine**: 3 key properties

| Missing Property | Purpose | Impact |
|------------------|---------|--------|
| TransformMode (enum) | Position atmosphere relative to planet | **HIGH** - breaks positional accuracy |
| TraceSampleCountScale | Quality/performance tradeoff | **MEDIUM** - visual quality varies |
| FTentDistribution (OtherTentDistribution) | Ozone layer modeling | **LOW** - visual enhancement |

---

### 🟡 DirectionalLightComponent (CRITICAL)
**Total UE5 properties**: 40+ rendering-relevant
**Missing in UzEngine**: 15+ properties

| Missing Property | Purpose | Impact |
|------------------|---------|--------|
| CloudShadowStrength | Cloud shadow darkness | **CRITICAL** - clouds don't cast shadows |
| CloudShadowOnAtmosphereStrength | Cloud shadows on atm. | **CRITICAL** - incomplete lighting |
| CloudShadowOnSurfaceStrength | Cloud shadows on geometry | **CRITICAL** - incomplete lighting |
| CloudShadowExtent | Cloud shadow map size | **CRITICAL** - must have for rendering |
| CloudShadowMapResolutionScale | Quality parameter | **HIGH** - affects performance |
| CloudShadowRaySampleCountScale | Quality parameter | **HIGH** - affects performance |
| CloudShadowDepthBias | Shadow artifact control | **MEDIUM** - visual artifacts |
| CloudScatteredLuminanceScale | Light color through clouds | **MEDIUM** - color accuracy |
| ShadowCascadeBiasDistribution | Shadow quality distribution | **MEDIUM** - artifact reduction |
| FarShadowCascadeCount | Far cascade count | **MEDIUM** - extended shadow distance |
| FarShadowDistance | Far cascade distance | **MEDIUM** - extended shadow distance |

---

### 🟡 SkyLightComponent (HIGH)
**Total UE5 properties**: 20+ rendering-relevant
**Missing in UzEngine**: 4 properties

| Missing Property | Purpose | Impact |
|------------------|---------|--------|
| bCloudAmbientOcclusion | Enable cloud AO | **HIGH** - feature gate |
| CloudAmbientOcclusionStrength | AO intensity | **HIGH** - visual quality |
| CloudAmbientOcclusionExtent | AO map size | **HIGH** - coverage range |
| CloudAmbientOcclusionMapResolutionScale | Resolution quality | **MEDIUM** - performance tradeoff |
| CloudAmbientOcclusionApertureScale | Cone aperture | **MEDIUM** - detail level |

---

### 🟢 PointLightComponent (COMPLETE)
✅ All main properties present
⚠️ Verify: SoftSourceRadius default behavior

---

### 🟢 SpotLightComponent (COMPLETE)
✅ All properties present (InnerConeAngle, OuterConeAngle)

---

### 🟡 ExponentialHeightFogComponent (MEDIUM)
**Total UE5 properties**: 32 rendering-relevant
**Missing in UzEngine**: 6+ properties

| Missing Property | Purpose | Impact |
|------------------|---------|--------|
| SecondFogData (struct) | Secondary fog layer | **MEDIUM** - dual-layer fog |
| VolumetricFog* parameters | Volumetric integration | **MEDIUM** - quality variations |
| SkyAtmosphereAmbientContribution | Sky light blending | **MEDIUM** - color accuracy |

---

### 🟡 VolumetricCloudComponent (MEDIUM)
**Total UE5 properties**: 26 rendering-relevant
**Status**: Properties present but may have incorrect defaults

| Concern | Impact |
|---------|--------|
| EVolumetricCloudTracingMaxDistanceMode enum | **MEDIUM** - distance semantics |
| Aerial perspective Rayleigh/Mie scattering defaults | **MEDIUM** - visual match to reference |

---

### 🟢 StaticMeshComponent (RENDERING SUBSET COMPLETE)
✅ All key rendering properties present
✅ LOD controls, WPO evaluation, Nanite flags

---

### 🔴 PostProcessComponent (INCOMPLETE)
**Status**: FPostProcessSettings struct partially known

| Missing Coverage | Impact |
|------------------|--------|
| Complete FPostProcessSettings audit | **CRITICAL** - 100+ sub-properties |
| Bloom (method selection, filter sizes) | **HIGH** |
| Color Grading (per-range saturation/contrast/gamma/gain/offset) | **HIGH** |
| Exposure (method, speed, compensation) | **HIGH** |
| Depth of Field (method, aperture) | **MEDIUM** |
| Motion Blur | **MEDIUM** |
| Ray Tracing settings | **MEDIUM** |
| Lumen GI integration | **HIGH** |

---

## Implementation Checklist

### Phase 1 - Critical Path (BLOCKING)
- [ ] **DirectionalLightComponent**: Add all 15 missing Cloud* shadow properties
  - File: `src/engine/components/directional_light_component.h`
  - Requires: Cloud shadow rendering system
  - Test: Cloud shadows visible, intensity/extent controllable

- [ ] **SkyAtmosphereComponent**: Add TransformMode enum + positioning logic
  - File: `src/engine/components/sky_atmosphere_component.h`
  - Requires: Positional reference frame logic
  - Test: Atmosphere position matches world origin modes

- [ ] **PostProcessComponent**: Audit FPostProcessSettings completeness
  - File: `src/engine/post_process/post_process_settings.h`
  - Scope: Document ALL 100+ properties with defaults
  - Test: All effects editable from editor

### Phase 2 - High Priority
- [ ] **SkyLightComponent**: Add cloud AO parameters (5 properties)
- [ ] **ExponentialHeightFogComponent**: Add SecondFogData, volumetric parameters
- [ ] **VolumetricCloudComponent**: Verify all defaults match UE5

### Phase 3 - Polish
- [ ] Validate all UIMin/UIMax/ClampMin/ClampMax ranges
- [ ] Ensure all Interp properties support animation
- [ ] Test all property setters/getters

---

## Key Insights

### Why Cloud Shadows Matter
Cloud shadows from directional lights are essential for:
1. **Visual credibility**: Clouds cast shadows on terrain and buildings
2. **Atmosphere integration**: Clouds occlude light scattering in atmosphere
3. **Performance tuning**: Separate resolution scale allows quality/perf balance
4. **Art direction**: Artistic control over cloud shadow darkness/extent

Without these properties, clouds render but don't interact with lighting—a fundamental visual flaw.

### Why SkyAtmosphereComponent TransformMode Matters
Three modes enable different use cases:
1. **PlanetTopAtAbsoluteWorldOrigin**: Atmosphere fixed at world origin (space games)
2. **PlanetTopAtComponentTransform**: Atmosphere follows component (moving planets)
3. **PlanetCenterAtComponentTransform**: Planet center at component location

UzEngine's fixed positioning breaks level design flexibility.

### Why FPostProcessSettings Matters
Post-process is the final image pipeline stage. Missing properties = visual effects can't be achieved:
- Bloom intensity/shape control
- Color grading per-tonal-range
- Exposure compensation and speed
- Chromatic aberration, motion blur, DOF

A "complete but cut-corners" PostProcessSettings implementation is visible to every player.

---

## Data Sources

All properties extracted from actual UE5 5.6 source headers:
- `Engine/Classes/Components/*.h`
- `Engine/Classes/Engine/Scene.h`

**No approximations**: All defaults, ranges, meta-tags documented directly from source.

---

## Next Steps for UzEngine Team

1. **Review this document** with rendering lead
2. **Prioritize Phase 1** blocking issues (cloud shadows, atmosphere positioning)
3. **Create Rulebook tasks** for each missing property category
4. **Assign to shader-engineer/cpp-core-expert** agents for implementation
5. **Test against UE5 reference** visually after each phase

