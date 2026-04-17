# UE5 Property Parity - Implementation Checklist

**Date**: 2026-03-21
**For**: cpp-core-expert, shader-engineer agents
**Priority**: BLOCKING issues first, polish last

---

## PHASE 1: CRITICAL (MUST IMPLEMENT)

### Task 1.1: DirectionalLightComponent - Cloud Shadow System
**File**: `src/engine/components/directional_light_component.h`
**Status**: [ ] NOT STARTED

#### Required Properties (15 total)
```
- [ ] float CloudShadowStrength = 1.0f;  // UI: 0-1, Clamp: 0
- [ ] float CloudShadowOnAtmosphereStrength = 1.0f;  // UI: 0-1, Clamp: 0
- [ ] float CloudShadowOnSurfaceStrength = 1.0f;  // UI: 0-1, Clamp: 0
- [ ] float CloudShadowDepthBias = 0.0f;  // UI: -1 to 1
- [ ] float CloudShadowExtent = 128.0f;  // UI: 1+, Clamp: 1
- [ ] float CloudShadowMapResolutionScale = 1.0f;  // UI: 0.25-8, Clamp: 0.25
- [ ] float CloudShadowRaySampleCountScale = 1.0f;  // UI: 0.25-8, Clamp: 0.25
- [ ] FLinearColor CloudScatteredLuminanceScale = FLinearColor(1,1,1,1);
- [ ] float ShadowCascadeBiasDistribution = 1.0f;  // UI: 0-1
- [ ] int32 FarShadowCascadeCount = 0;  // UI: 0-4
- [ ] float FarShadowDistance = 0.0f;  // UI: 0-800000
- [ ] float ShadowSourceAngleFactor = 1.0f;  // UI: 0-5
```

#### Setter Functions Required
```cpp
void SetCloudShadowStrength(float NewValue);
void SetCloudShadowOnAtmosphereStrength(float NewValue);
void SetCloudShadowOnSurfaceStrength(float NewValue);
void SetCloudShadowDepthBias(float NewValue);
void SetCloudShadowExtent(float NewValue);
void SetCloudShadowMapResolutionScale(float NewValue);
void SetCloudShadowRaySampleCountScale(float NewValue);
void SetShadowCascadeBiasDistribution(float NewValue);
void SetFarShadowCascadeCount(int32 NewValue);
void SetFarShadowDistance(float NewValue);
void SetShadowSourceAngleFactor(float NewValue);
```

#### Test Checklist
- [ ] Properties visible in editor with correct UI ranges
- [ ] Properties interpolate correctly (all are Interp=true or yes)
- [ ] Cloud shadows render on terrain (verify with --screenshot-stdout)
- [ ] Shadow intensity controllable 0-1 without visual clipping
- [ ] Far cascade count extends shadow range beyond normal cascades

---

### Task 1.2: SkyAtmosphereComponent - TransformMode
**File**: `src/engine/components/sky_atmosphere_component.h`
**Status**: [ ] NOT STARTED

#### Required Implementation
```cpp
UENUM()
enum class ESkyAtmosphereTransformMode : uint8
{
    PlanetTopAtAbsoluteWorldOrigin = 0,
    PlanetTopAtComponentTransform = 1,
    PlanetCenterAtComponentTransform = 2,
};

UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Planet", meta = (HideAlphaChannel))
ESkyAtmosphereTransformMode TransformMode;
```

#### Logic Required
- Update positioning when TransformMode changes
- Apply offset calculations based on selected mode
- Ensure sky follows component position correctly in each mode

#### Test Checklist
- [ ] All 3 enum modes selectable in editor
- [ ] Mode changes visible in scene (atmosphere position updated)
- [ ] Fixed world origin mode works (space scenarios)
- [ ] Component-relative mode works (moving planets)

---

### Task 1.3: SkyLightComponent - Cloud Ambient Occlusion
**File**: `src/engine/components/sky_light_component.h`
**Status**: [ ] NOT STARTED

#### Required Properties (5 total)
```cpp
- [ ] uint32 bCloudAmbientOcclusion : 1;  // Default: 1 (enabled)
- [ ] float CloudAmbientOcclusionStrength = 0.5f;  // UI: 0-1, Clamp: 0
- [ ] float CloudAmbientOcclusionExtent = 64.0f;  // UI: 1+, Clamp: 1
- [ ] float CloudAmbientOcclusionMapResolutionScale = 1.0f;  // UI: 0.25-8, Clamp: 0.25
- [ ] float CloudAmbientOcclusionApertureScale = 0.0f;  // UI: 0.0-0.1, Clamp: 0.0-1.0
```

#### Setter Functions Required
```cpp
void SetCloudAmbientOcclusion(bool bNewValue);
void SetCloudAmbientOcclusionStrength(float NewValue);
void SetCloudAmbientOcclusionExtent(float NewValue);
void SetCloudAmbientOcclusionMapResolutionScale(float NewValue);
void SetCloudAmbientOcclusionApertureScale(float NewValue);
```

#### Test Checklist
- [ ] Cloud AO toggle enable/disables feature
- [ ] Strength 0 = no shadow, 1 = full shadow
- [ ] Extent controls coverage area around camera
- [ ] Resolution scale affects quality/performance tradeoff
- [ ] Visual effect visible in reflections

---

### Task 1.4: PostProcessComponent - FPostProcessSettings Audit
**File**: `src/engine/post_process/post_process_settings.h`
**Status**: [ ] NOT STARTED

#### Requirements
1. **Document all FPostProcessSettings properties** (100+ properties)
   - List every UPROPERTY with type and default
   - Identify any missing categories vs UE5

2. **Key sub-structures to verify**
   - FLensSettings (Bloom, Imperfections, ChromaticAberration)
   - FColorGradingSettings (Shadows, Midtones, Highlights)
   - FCameraExposureSettings (Method, speed parameters)
   - FGaussianSumBloomSettings (6 tint colors, 6 size values)
   - FConvolutionBloomSettings (kernel, scale, buffer)
   - FFilmStockSettings (Slope, Toe, Shoulder, clips)

3. **Verify all property defaults match UE5 exactly**
   - No invented defaults
   - No "close enough" values
   - Defaults from actual UE5 constructor calls

#### Output
- CSV file: `ue5-postprocess-settings-inventory.csv`
- Checklist of missing vs present properties
- Defaults table (UE5 vs UzEngine)

---

## PHASE 2: HIGH PRIORITY (SHOULD IMPLEMENT)

### Task 2.1: SkyAtmosphereComponent - TraceSampleCountScale
**File**: `src/engine/components/sky_atmosphere_component.h`
**Status**: [ ] NOT STARTED

#### Required Implementation
```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere", AdvancedDisplay,
    meta = (UIMin = "0.25", UIMax = "8", ClampMin = "0.25", SliderExponent = 3.0))
float TraceSampleCountScale = 1.0f;
```

#### Setter Function
```cpp
void SetTraceSampleCountScale(float NewValue);
```

#### Test Checklist
- [ ] Property visible in Advanced Display section
- [ ] Slider works 0.25 to 8 with exponent 3.0
- [ ] Affects sky quality/performance visibly

---

### Task 2.2: SkyAtmosphereComponent - FTentDistribution
**File**: `src/engine/components/sky_atmosphere_component.h`
**Status**: [ ] NOT STARTED

#### Required Struct Implementation
```cpp
USTRUCT(BlueprintType)
struct FTentDistribution
{
    GENERATED_USTRUCT_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Tent",
        meta = (UIMin = 0.0, UIMax = 60.0))
    float TipAltitude = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Tent",
        meta = (UIMin = 0.0, UIMax = 1.0, ClampMin = 0.0, SliderExponent = 4.0))
    float TipValue = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Tent",
        meta = (UIMin = 0.01, UIMax = 20.0, ClampMin = 0.0))
    float Width = 1.0f;
};
```

#### Usage
```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere - Absorption",
    meta = (DisplayName = "Tent Distribution"))
FTentDistribution OtherTentDistribution;
```

#### Test Checklist
- [ ] Struct editable in details panel
- [ ] All 3 properties have correct sliders
- [ ] Interp works for animation

---

### Task 2.3: ExponentialHeightFogComponent - SecondFogData
**File**: `src/engine/components/exponential_height_fog_component.h`
**Status**: [ ] NOT STARTED

#### Required Implementation
Copy FExponentialHeightFogData struct from UE5 if not already present, then:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ExponentialHeightFogComponent")
FExponentialHeightFogData SecondFogData;
```

#### Setter Function
```cpp
void SetSecondFogData(FExponentialHeightFogData NewValue);
```

#### Test Checklist
- [ ] Second fog layer properties editable
- [ ] Can set density, height falloff independently
- [ ] Visual layering works (second fog renders correctly)

---

## PHASE 3: POLISH (NICE TO HAVE)

### Task 3.1: Validate All UI Ranges
**Status**: [ ] NOT STARTED

For each component:
- [ ] SkyAtmosphereComponent: All UIMin/UIMax match UE5 exactly
- [ ] DirectionalLightComponent: All shadow UI ranges verified
- [ ] SkyLightComponent: Cloud AO ranges correct
- [ ] ExponentialHeightFogComponent: Volumetric fog ranges match
- [ ] VolumetricCloudComponent: Sampling ranges correct

### Task 3.2: Ensure All Interp Properties Support Animation
**Status**: [ ] NOT STARTED

For all properties marked `Interp=true`:
- [ ] Can be keyed in animation curves
- [ ] Smooth interpolation between values
- [ ] No sudden jumps or discontinuities

### Task 3.3: Test All Setters at Runtime
**Status**: [ ] NOT STARTED

For each new setter function:
- [ ] Call from Lua works correctly
- [ ] From C++ works correctly
- [ ] Changes visible on screen immediately
- [ ] No crashes or asserts

---

## Testing Template

After implementing each task, run this battery:

```bash
# Build runtime
cd runtime && zig build --clean && zig build

# Build demo
cd demos/atmosphere_test && zig build --clean && zig build

# Test 1: Properties visible in editor
demos/atmosphere_test/zig-cache/bin/demo --frames 5 2>&1 | grep -i "property"

# Test 2: Visual verification - default position
demos/atmosphere_test/zig-cache/bin/demo --screenshot-stdout --screenshot-frame 5 --frames 8

# Test 3: Visual verification - component position
demos/atmosphere_test/zig-cache/bin/demo --screenshot-stdout --screenshot-frame 5 --frames 8 --camera-pos "1000 1000 500"

# Test 4: Property setters work
demos/atmosphere_test/zig-cache/bin/demo --lua-script "test_cloud_shadows.lua" --frames 30
```

---

## Dependency Graph

```
Phase 1.1 (Cloud Shadows) → Phase 1.2 (Transform Mode) → Phase 1.3 (Cloud AO)
                                           ↓
                                  Phase 1.4 (PostProcess)
                                           ↓
                          Phase 2.x (Non-blocking enhancements)
                                           ↓
                          Phase 3.x (Polish/validation)
```

---

## Success Criteria

✅ **Phase 1 complete when:**
- All 15 cloud shadow properties on DirectionalLightComponent
- TransformMode enum working with all 3 modes
- Cloud AO affecting sky light with visible results
- FPostProcessSettings audit document complete

✅ **Phase 2 complete when:**
- TraceSampleCountScale affects sky quality
- FTentDistribution editable in editor
- SecondFogData creates visible dual-layer fog

✅ **Phase 3 complete when:**
- All properties have correct UI ranges
- All Interp properties animate smoothly
- All setters work from C++ and Lua

---

## Files to Edit Summary

| File | Phase | Props | Setters | Test |
|------|-------|-------|---------|------|
| directional_light_component.h | 1.1 | 15 | 12 | clouds |
| sky_atmosphere_component.h | 1.2+2.1+2.2 | 3 | 3 | positioning |
| sky_light_component.h | 1.3 | 5 | 5 | reflections |
| exponential_height_fog_component.h | 2.3 | 1 | 1 | layering |
| post_process_settings.h | 1.4 audit | 100+ | N/A | effects |
| test_cloud_shadows.lua | all | N/A | N/A | runtime |

