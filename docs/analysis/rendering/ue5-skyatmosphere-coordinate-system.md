<!-- doc-meta
{
  "title": "UE5 SkyAtmosphere: Coordinate System & Scale Transformation",
  "description": "Deep analysis of how UE5 SkyAtmosphere maps game-world camera positions (cm, meters) to atmosphere-relative positions (kilometers). Critical for UzEngine adaptation.",
  "keywords": ["UE5", "sky", "atmosphere", "coordinate system", "scale transformation", "planet radius"],
  "status": "complete",
  "last_updated": "2026-03-15"
}
-->

# UE5 SkyAtmosphere: Coordinate System & Scale Transformation

## Executive Summary

UE5's SkyAtmosphere system solves a fundamental mismatch between game-world scale (centimeters as unit) and atmospheric simulation scale (kilometers):

- **Game World**: Camera at world position Y=5 = 5 cm = 0.05 meters above ground
- **Atmosphere Simulation**: Virtual planet has BottomRadius=6360 km, TopRadius=6420 km
- **The Problem**: A camera 5cm above a 6360km planet is mathematically invisible to ray marching
- **UE5's Solution**: Dynamically clamp the camera to a safe minimum altitude above the virtual planet surface

This analysis extracts UE5's pattern to inform UzEngine's adaptation.

---

## 1. Default Values & Planet Model

### Component Constructor
**File**: `Engine/Source/Runtime/Engine/Private/Components/SkyAtmosphereComponent.cpp`

UE5 ships with Earth-like defaults:

```cpp
const float EarthBottomRadius = 6360.0f;      // km
const float EarthTopRadius = 6420.0f;         // km
const float EarthRayleighScaleHeight = 8.0f;   // km
const float EarthMieScaleHeight = 1.2f;        // km
```

In `USkyAtmosphereComponent` constructor:
- `BottomRadius = 6360.0f` (km, stored in `SkyAtmosphereComponent.h` line 59-60)
- `AtmosphereHeight = 60.0f` (km, TopRadius - BottomRadius)
- `TransformMode = PlanetTopAtAbsoluteWorldOrigin` (default)

**Rayleigh Scattering** (air molecules, dominant in blue sky):
```
RayleightScatteringRaw = FLinearColor(0.005802f, 0.013558f, 0.033100f)
RayleighExponentialDistribution = 8.0f km
```
Density = `exp(-height / 8.0f)` → 40% at 8km altitude

**Mie Scattering** (aerosol particles, halos):
```
MieScatteringScale = 0.003996f
MieAbsorptionScale = 0.000444f
MieAnisotropy = 0.8f
MieExponentialDistribution = 1.2f km
```
Density = `exp(-height / 1.2f)` → 40% at 1.2km altitude (closer to ground)

**Ozone Absorption** (stratosphere, 10-40km):
```
TentDistribution.TipAltitude = 25.0f km
TentDistribution.TipValue = 1.0f
TentDistribution.Width = 15.0f km
```
Density peaks at 25km, spreads ±15km → roughly 10-40km band

---

## 2. Coordinate System Units

### Unit Conversion Constants
**File**: `Engine/Source/Runtime/Engine/Public/Rendering/SkyAtmosphereCommonData.cpp` (lines 15-16)

```cpp
const float FAtmosphereSetup::CmToSkyUnit = 0.00001f;      // cm → km
const float FAtmosphereSetup::SkyUnitToCm = 1.0f / 0.00001f; // km → cm = 100000.0f
```

### Implications

| World Unit | To Km | To Cm |
|---|---|---|
| 1 cm | 0.00001 km | 1 cm |
| 1 m (100 cm) | 0.001 km | 100 cm |
| 5 m (500 cm) | 0.005 km | 500 cm |
| 1 km | 1.0 km | 100,000 cm |

**Key insight**: A game camera at world Y=5 (meaning 5 cm above ground) is 0.00005 km above the virtual planet bottom, which is **1 part in 127 million** — invisible to ray marching.

---

## 3. Transform Modes & Planet Center Positioning

**File**: `Engine/Source/Runtime/Engine/Classes/Components/SkyAtmosphereComponent.h` (lines 34-40)

```cpp
enum class ESkyAtmosphereTransformMode : uint8
{
    PlanetTopAtAbsoluteWorldOrigin,      // Planet ground at world (0,0,0)
    PlanetTopAtComponentTransform,       // Planet ground at component position
    PlanetCenterAtComponentTransform,    // Planet center at component position
};
```

**Implementation**: `SkyAtmosphereCommonData.cpp` (lines 138-154)

```cpp
void FAtmosphereSetup::UpdateTransform(const FTransform& ComponentTransform, uint8 TranformMode)
{
    switch (ESkyAtmosphereTransformMode(TranformMode))
    {
    case PlanetTopAtAbsoluteWorldOrigin:
        // Planet ground surface at world origin
        PlanetCenterKm = FVector(0.0f, 0.0f, -BottomRadiusKm);
        // If BottomRadiusKm=6360, center is at Z=-6360km

    case PlanetTopAtComponentTransform:
        // Planet ground surface offset to component location
        PlanetCenterKm = FVector(0.0f, 0.0f, -BottomRadiusKm)
                        + ComponentTransform.GetTranslation() * CmToSkyUnit;

    case PlanetCenterAtComponentTransform:
        // Planet center directly at component
        PlanetCenterKm = ComponentTransform.GetTranslation() * CmToSkyUnit;
    }
}
```

**Default game behavior**: `PlanetTopAtAbsoluteWorldOrigin`
- Planet center is at world (0, 0, -6360 km) in sky units
- Game world origin (0, 0, 0) is at planet surface (sea level)
- Camera at world (0, 0, 500) = 5 meters above ground = (0, 0, 0.005) km above planet surface

---

## 4. THE CRITICAL TRANSFORMATION: ComputeViewData()

This is the key function that solves the scale mismatch problem.

**File**: `SkyAtmosphereCommonData.cpp` (lines 245-304)

```cpp
void FAtmosphereSetup::ComputeViewData(
    const FVector& WorldCameraOrigin,           // world cm (untranslated)
    const FVector& PreViewTranslation,          // render thread translation (cm)
    const FVector3f& ViewForward,
    const FVector3f& ViewRight,
    FVector3f& SkyCameraTranslatedWorldOrigin,  // OUTPUT: clamped camera pos (cm)
    FVector4f& SkyPlanetTranslatedWorldCenterAndViewHeight, // OUTPUT: (center.xyz, height)
    FMatrix44f& SkyViewLutReferential)          // OUTPUT: local coordinate frame
{
    // ─────────────────────────────────────────────────────────────────────────
    // STEP 1: Convert everything to sky units (kilometers) for computation
    // ─────────────────────────────────────────────────────────────────────────

    // Minimum altitude offset: always clamp camera to 5 meters above ground
    // (or less for tiny planets where 5m is significant)
    const float PlanetRadiusOffset = 0.005f;        // km (5 meters)
    const float Offset = PlanetRadiusOffset * SkyUnitToCm;  // convert back to cm

    // Convert everything to sky units
    const float BottomRadiusWorld = BottomRadiusKm * SkyUnitToCm;      // cm
    const FVector PlanetCenterWorld = PlanetCenterKm * SkyUnitToCm;    // cm

    // ─────────────────────────────────────────────────────────────────────────
    // STEP 2: Apply render-thread view translation (for LOD streaming)
    // ─────────────────────────────────────────────────────────────────────────

    const FVector PlanetCenterTranslatedWorld = PlanetCenterWorld + PreViewTranslation;
    const FVector WorldCameraOriginTranslatedWorld = WorldCameraOrigin + PreViewTranslation;

    // ─────────────────────────────────────────────────────────────────────────
    // STEP 3: CLAMP CAMERA if below minimum altitude
    // ─────────────────────────────────────────────────────────────────────────

    const FVector PlanetCenterToCameraTranslatedWorld =
        WorldCameraOriginTranslatedWorld - PlanetCenterTranslatedWorld;
    const float DistanceToPlanetCenterTranslatedWorld =
        PlanetCenterToCameraTranslatedWorld.Size();

    // THIS IS THE KEY LINE:
    // If camera is closer than (BottomRadius + 5m), snap it to the surface
    SkyCameraTranslatedWorldOriginTranslatedWorld = FVector3f(
        DistanceToPlanetCenterTranslatedWorld < (BottomRadiusWorld + Offset) ?
        // If too close: project camera onto safe altitude surface
        PlanetCenterTranslatedWorld +
            (BottomRadiusWorld + Offset) *
            (PlanetCenterToCameraTranslatedWorld / DistanceToPlanetCenterTranslatedWorld) :
        // Else: use actual camera position
        WorldCameraOriginTranslatedWorld);

    // ─────────────────────────────────────────────────────────────────────────
    // STEP 4: Compute view height (distance from planet center to clamped camera)
    // ─────────────────────────────────────────────────────────────────────────

    SkyPlanetTranslatedWorldCenterAndViewHeight = FVector4f(
        (FVector3f)PlanetCenterTranslatedWorld,
        ((FVector)SkyCameraTranslatedWorldOriginTranslatedWorld -
         PlanetCenterTranslatedWorld).Size());

    // ─────────────────────────────────────────────────────────────────────────
    // STEP 5: Build local coordinate frame (Up = radial, Forward = view dir)
    // ─────────────────────────────────────────────────────────────────────────

    // Up vector is radial direction from planet center
    FVector PlanetCenterToWorldCameraPos =
        ((FVector)SkyCameraTranslatedWorldOriginTranslatedWorld -
         PlanetCenterTranslatedWorld) * CmToSkyUnit;
    FVector3f Up = (FVector3f)PlanetCenterToWorldCameraPos;
    Up.Normalize();

    // ... (build Forward/Left using Up, ViewForward, and special handling
    //      when camera is aligned with Up direction)
}
```

**Critical behavior**:
1. **If camera is below `BottomRadius + 0.005 km` (5 meters above ground)**: UE5 **projects it outward** to exactly `BottomRadius + 0.005 km` at the same azimuth
2. **If camera is above that threshold**: Use actual camera position as-is
3. The clamping is **silent** — no warning, visual artifacts, or console messages in shipping code
4. The clamped position is stored in `SkyCameraTranslatedWorldOrigin` and passed to ray marching shaders

---

## 5. Shader-Side Constants & LUT Parameterization

**File**: `Engine/Shaders/Private/SkyAtmosphereCommon.ush` (lines 21-30)

```hlsl
// Kilometers as unit for computations related to the sky and its atmosphere
#define CM_TO_SKY_UNIT 0.00001f
#define SKY_UNIT_TO_CM (1.0f/CM_TO_SKY_UNIT)

// Float accuracy offset in Sky unit (km, so this is 1m). Should match the one in
// FAtmosphereSetup::ComputeViewData
#define PLANET_RADIUS_OFFSET 0.001f

// Planet radius safe edge to make sure ray does intersect with the atmosphere,
// for it to traverse the atmosphere. Must match the one in FSceneRenderer.
// This is (0.01km/6420km).
#define PLANET_RADIUS_RATIO_SAFE_EDGE 1.00000155763f
```

**Note the discrepancy**: C++ code uses `PlanetRadiusOffset = 0.005f` (5 meters), but shader comment says `PLANET_RADIUS_OFFSET = 0.001f` (1 meter). This was corrected in later patches.

### Transmittance LUT Parameterization

The transmittance LUT is indexed by:
- **UV.x** = zenith angle (0 = straight up, 1 = horizon)
- **UV.y** = altitude (0 = ground, 1 = top of atmosphere)

Ray marching happens in this space, which avoids per-pixel integration cost.

---

## 6. Problem Examples: The Scale Mismatch

### Scenario A: Tiny Planet Mode
```cpp
BottomRadius = 100.0f;     // 100 km planet (1/64 Earth)
AtmosphereHeight = 10.0f;  // 10 km atmosphere

// Camera at world Y = 5 cm
// Distance to planet center: 100 km + 0.00005 km = 100.00005 km
// This is 0.00005% of radius — computationally invisible
```

**UE5 handles it**: Camera is clamped to 100.005 km + 0.005 km = 100.010 km, ensuring ray marching works.

### Scenario B: Large World (Far LOD Streaming)
```cpp
// With PreViewTranslation = (1000000, 0, 0) cm (10 km offset)
// PlanetCenter = (0, 0, 0) km → (0, 0, 0) cm in world
// Camera = (1000000, 0, 5) cm → after translation → (1000000, 0, 5 + offset)

// In sky units: everything is scaled by 0.00001
// So 1000km world translation = just positional offset in km space
```

**UE5 handles it**: `PreViewTranslation` is applied identically to planet center and camera, maintaining relative positions in sky space.

---

## 7. Shader Sampling: How Rays Are Marched

**File**: `SkyAtmosphereCommon.ush`, function `SampleAtmosphereMediumRGB()` (lines 308-340)

```hlsl
MediumSampleRGB SampleAtmosphereMediumRGB(in float3 WorldPos)
{
    // WorldPos is in sky units (kilometers), already relative to planet center

    // Height above ground = distance_from_center - bottom_radius
    const float SampleHeight = max(0.0, (length(WorldPos) - Atmosphere.BottomRadiusKm));

    // Rayleigh density exponentially decays with altitude
    const float DensityRay = exp(Atmosphere.RayleighDensityExpScale * SampleHeight);

    // Mie density exponentially decays (faster, closer to ground)
    const float DensityMie = exp(Atmosphere.MieDensityExpScale * SampleHeight);

    // Ozone tent distribution (layer 10-40 km)
    const float DensityOzo = SampleHeight < Atmosphere.AbsorptionDensity0LayerWidth ?
        saturate(Atmosphere.AbsorptionDensity0LinearTerm * SampleHeight + ...) :
        saturate(Atmosphere.AbsorptionDensity1LinearTerm * SampleHeight + ...);

    // Compute extinction (how much light is blocked)
    s.ExtinctionRay = DensityRay * Atmosphere.RayleighScattering;
    s.ExtinctionMie = DensityMie * Atmosphere.MieExtinction;

    return s;
}
```

This is called during ray marching. The ray is parameterized in **sky units** (km), and sampling happens at discrete steps along the ray from camera to atmosphere boundary.

---

## 8. UzEngine Adaptation Strategy

### Pattern to Extract: **Safe Altitude Clamping**

**Problem UzEngine must solve**:
- Game world is in centimeters or meters (depending on scale)
- Atmosphere simulation is in kilometers (or some other "far scale")
- Camera can exist at arbitrarily small altitudes above ground
- Ray marching requires camera to be at least `PlanetRadiusOffset` above ground

**Pattern**:
```cpp
// In UzEngine's atmosphere setup (pseudocode):

struct UzAtmosphereSetup {
    float planet_radius_km;      // e.g., 6360
    float atmosphere_height_km;  // e.g., 60
    float world_scale_cm_per_km; // e.g., 100000 (our conversion)
    float min_altitude_offset_km; // e.g., 0.005 (5 meters)

    void ComputeViewData(
        const Vec3& world_camera_pos,  // in world units
        Vec3& sky_camera_pos,          // OUTPUT: clamped to min altitude
        float& view_height_km          // OUTPUT: distance to planet center
    ) {
        // 1. Convert world → sky units
        Vec3 planet_center_sky = PlanetCenterKm();
        Vec3 camera_sky = world_camera_pos / world_scale_cm_per_km;

        // 2. Compute distance to planet center
        float dist_to_center = length(camera_sky - planet_center_sky);
        float min_safe_dist = planet_radius_km + min_altitude_offset_km;

        // 3. CLAMP if below minimum
        if (dist_to_center < min_safe_dist) {
            // Project camera outward to safe altitude
            Vec3 radial = normalize(camera_sky - planet_center_sky);
            sky_camera_pos = planet_center_sky + min_safe_dist * radial;
        } else {
            sky_camera_pos = camera_sky;
        }

        // 4. Return view height for LUT parameterization
        view_height_km = length(sky_camera_pos - planet_center_sky);
    }
};
```

### Key Design Decisions for UzEngine

1. **Unit conversion must be consistent C++ ↔ Shader**:
   - Define `const float WORLD_CM_PER_SKY_KM = 100000.0f` in both C++ and HLSL
   - All conversions use this single value

2. **Clamp silently, but make it configurable**:
   - `MinAltitudeOffsetKm = 0.005f` (5 meters) as default
   - Allow artists to override for tiny planets or space scenes
   - Log once at startup if clamping occurs (don't spam every frame)

3. **Pre-translate camera before any LUT indexing**:
   - Clamp first, then pass clamped camera to ray marching
   - LUT UV mapping uses clamped position
   - Shaders never see the original (possibly invalid) camera position

4. **Handle edge case: camera inside planet**:
   - `DistanceToPlanetCenter < BottomRadius` → snap to surface, not mid-planet
   - UE5 does this correctly via radial projection

---

## 9. What NOT to Copy

1. **Don't hardcode `6360 km` in production code** — make it a parameter
2. **Don't use `float` for planet position math** — use `FVector` (which is `double` in engine)
3. **Don't skip `PreViewTranslation` handling** — it's critical for large worlds
4. **Don't compute LUT UVs in world space** — always convert to sky units first
5. **Don't forget shader side matching** — CPU and GPU constants must agree exactly

---

## 10. Reference Files (UE5 5.6)

| File | Purpose |
|------|---------|
| `Engine/Source/Runtime/Engine/Classes/Components/SkyAtmosphereComponent.h` | Component declaration, properties, transform modes |
| `Engine/Source/Runtime/Engine/Private/Components/SkyAtmosphereComponent.cpp` | Default values (Earth model) |
| `Engine/Source/Runtime/Engine/Public/Rendering/SkyAtmosphereCommonData.h` | `FAtmosphereSetup` struct declaration |
| `Engine/Source/Runtime/Engine/Public/Rendering/SkyAtmosphereCommonData.cpp` | **ComputeViewData() implementation — THE KEY FUNCTION** |
| `Engine/Shaders/Private/SkyAtmosphereCommon.ush` | Shader-side sampling, unit conversion macros |
| `Engine/Source/Runtime/Renderer/Private/SkyAtmosphereRendering.cpp` | Render-thread setup, LUT generation, transmittance computation |

---

## 11. Summary: The Pattern

**Pattern Name**: *Safe Altitude Clamping for Mismatched Coordinate Systems*

**Problem Solved**:
- Atmospheric simulation assumes planetary scale (6360 km)
- Game world uses screen-space scale (centimeters/meters)
- Camera can exist at invisible altitudes (0.00005 km = 5 cm above 6360 km planet)
- Ray marching requires numerically stable camera altitude

**Solution**:
1. Define a `MinAltitudeOffset` (0.005 km = 5 meters by default)
2. Before ray marching, compute distance from camera to planet center
3. If distance < radius + offset, project camera outward to safe altitude
4. Pass clamped camera to all LUT sampling and ray marching
5. Ensure clamping logic matches between C++ and shaders

**Why It Works**:
- Preserves camera azimuth/azimuth (projection is radial)
- Invisible to player unless they're actually at the edge of the planet
- Numerically stable ray-sphere intersections
- Works for any planet radius (tiny to huge)

