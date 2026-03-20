<!-- doc-meta: volumetric-clouds-fix-plan; date: 2026-03-20; author: unreal-architect -->

# Volumetric Cloud Ray-Sphere Intersection Fix Plan

**Problem**: Cloud rays are NOT intersecting the cloud layer shell even though math should work.

**Root Cause**: Inconsistent coordinate system usage — camera is shifted by `+planetRadius` but shells use implicit world-relative coordinates.

**Solution**: Match UE5's approach — use pure world-space coordinates for both camera and planet center.

---

## Before (Broken)

```hlsl
// CPU
float3 planetCenterCm = FVector3f(0, 0, -6360.0f * 100000.0f);  // -636M cm
float3 cameraPosWorldCm = ...;

// Shader
float3 rayOriginKm = cameraPosWorldCm * CM_TO_KM;
float3 rayOriginPlanet = rayOriginKm + float3(0, 0, 6360.0f);  // WRONG OFFSET
float bottomRadius = 6360.0f + 5.0f;   // 6365 km
float topRadius = 6365.0f + 10.0f;     // 6375 km

// Ray-sphere intersection (implicit planet center at origin)
RayIntersectSphereSolution(rayOriginPlanet, rayDir,
    float4(0, 0, 0, topRadius), tTop);
```

**Problem**:
- Camera is shifted to planet-relative space: (0, 0, 6860) km
- Shells are defined relative to origin: 6365-6375 km
- Camera is 6860 km from origin, well outside the 6365-6375 km shells
- Ray misses shells (no intersection)

---

## After (Correct — Match UE5)

```hlsl
// CPU
float3 planetCenterCm = FVector3f(0, 0, -6360.0f * 100000.0f);  // -636M cm
float3 cameraPosWorldCm = ...;
float planetRadiusKm = 6360.0f;
float layerBottomAltitudeKm = 5.0f;
float layerHeightKm = 10.0f;

// Pass to shader:
// - cameraPosWorldKm (NOT shifted)
// - planetCenterKm (absolute world position)
// - layerBottomAltitudeKm, layerHeightKm

// Shader
float3 cameraWorldPosKm = cameraPosWorldCm * CM_TO_KM;
float3 planetCenterKm = planetCenterCm * CM_TO_KM;  // e.g., (0, 0, -6360) km

float bottomRadiusKm = planetRadiusKm + layerBottomAltitudeKm;   // 6365 km
float topRadiusKm = bottomRadiusKm + layerHeightKm;              // 6375 km

// Ray-sphere intersection (world space - world space)
RayIntersectSphereSolution(cameraWorldPosKm, rayDir,
    float4(planetCenterKm, topRadiusKm), tTop);
```

**Why this works**:
- Camera at (0, 0, 50) km (50 km altitude above origin)
- Planet center at (0, 0, -6360) km
- Distance = 6410 km (50 km altitude above surface)
- Shells at 6365-6375 km from center
- Ray toward planet intersects shells ✓

---

## Step-by-Step Fix

### Step 1: Identify Current Shell Definition Code

Find where you define:
```cpp
bottomRadius = planetRadius + layerBottomAltitude
topRadius = bottomRadius + layerHeight
```

These are correct. **DO NOT CHANGE THEM.**

### Step 2: Remove the `+ float3(0, 0, planetRadius)` Offset

**Before**:
```hlsl
float3 rayOriginPlanet = rayOriginKm + float3(0, 0, planetRadius);
```

**After**:
```hlsl
float3 rayOriginWorldKm = rayOriginKm;  // Just rename, no offset
```

### Step 3: Ensure Planet Center is in World Space

**Before** (implicit):
```hlsl
float4 sphere = float4(0, 0, 0, topRadius);  // Implicit origin
```

**After** (explicit):
```hlsl
float3 planetCenterKm = ...;  // CPU passes this as constant buffer
float4 sphere = float4(planetCenterKm, topRadius);
```

### Step 4: Pass Planet Center from CPU to Shader

Ensure your constant buffer includes:

```cpp
// C++ side
struct CloudParams {
    float3 cameraPosWorldKm;     // NOT offset
    float3 planetCenterKm;       // Absolute world position, e.g., (0, 0, -6360)
    float planetRadiusKm;
    float layerBottomAltitudeKm;
    float layerHeightKm;
    // ...
};
```

```hlsl
// HLSL side
float bottomRadiusKm = CloudParams.planetRadiusKm + CloudParams.layerBottomAltitudeKm;
float topRadiusKm = bottomRadiusKm + CloudParams.layerHeightKm;

RayIntersectSphereSolution(CloudParams.cameraPosWorldKm, rayDir,
    float4(CloudParams.planetCenterKm, topRadiusKm), tTop);
```

### Step 5: Verify Altitude Calculation

After intersection, when you sample the cloud at a point:

```hlsl
// Sample position along the ray
float3 samplePosWorld = cameraWorldPosKm + rayDir * t;

// Distance from planet center
float distFromPlanetCenterKm = length(samplePosWorld - planetCenterKm);

// Altitude above surface
float altitudeKm = distFromPlanetCenterKm - planetRadiusKm;

// Use altitudeKm for density lookups, not the old computed value
```

---

## Validation Checklist

- [ ] Remove `+ float3(0, 0, planetRadius)` offset from camera position
- [ ] Ensure planet center is passed as an explicit shader constant
- [ ] Verify ray-sphere intersection uses same coordinate space for both ray origin and sphere center
- [ ] Ensure all altitude calculations use `distFromPlanetCenter - planetRadiusKm`
- [ ] Test with camera at different altitudes (0 km, 10 km, 500 km, above top shell)
- [ ] Verify ray directions toward/away from planet produce correct intersections
- [ ] Check that density samples at known altitudes return expected values

---

## Expected Results After Fix

| Test Case | Before (Broken) | After (Fixed) |
|-----------|---|---|
| Camera at 50 km alt, ray toward planet | No intersection (ray misses) | Intersects shells ✓ |
| Camera at 500 km alt, ray toward planet | No intersection | Intersects shells ✓ |
| Camera inside shells, ray outward | No intersection | Intersects top shell ✓ |
| Camera below shells, ray upward | No intersection | Intersects bottom shell ✓ |
| Altitude at shell bottom | Wrong value | 5 km ✓ |
| Altitude at shell top | Wrong value | 15 km ✓ |

---

## Code References

### UE5 Ray-Sphere Intersection (correct)
File: `W:\UE_5.6_Source\Engine\Shaders\Private\VolumetricCloud.usf`
Lines: 231-253

```hlsl
bool RayIntersectSphereSolution(float3 RayOrigin, float3 RayDirection, float4 Sphere, inout float2 Solutions)
{
    float3 LocalPosition = RayOrigin - Sphere.xyz;  // ← The key transformation
    // ... rest of quadratic solution
}
```

### UE5 Camera Positioning (correct)
File: `W:\UE_5.6_Source\Engine\Shaders\Private\VolumetricCloud.usf`
Lines: 490-493

```hlsl
float3 RayWorldOriginKm = select(IsOrthoProjection(),
    DFHackToFloat(DFSubtract(RayTranslatedWorldOrigin, PrimaryView.PreViewTranslation)) * CENTIMETER_TO_KILOMETER,
    DFHackToFloat(ResolvedView.WorldCameraOrigin) * CENTIMETER_TO_KILOMETER
);
// Note: NO offset added here. Just direct conversion to kilometers.
```

### UE5 Shell Definition (correct)
File: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\VolumetricCloudRendering.cpp`
Lines: ~250

```cpp
CloudGlobalShaderParams.BottomRadiusKm = PlanetRadiusKm + CloudProxy.LayerBottomAltitudeKm;
CloudGlobalShaderParams.TopRadiusKm = CloudGlobalShaderParams.BottomRadiusKm + CloudProxy.LayerHeightKm;
// These remain the same — they're correct.
```

---

## Why UE5's Approach is Correct

1. **Avoids floating-point precision loss**: Both camera and planet center are in the same world-space coordinate system. No accumulation of errors from shifting operations.

2. **Handles arbitrary camera positions**: Works whether camera is at origin, at planet center, or 1000000 km away. The ray-sphere math handles all cases.

3. **Consistent with rest of engine**: World positions in cameras, lights, and objects are all in the same space. No implicit assumptions about coordinate origin.

4. **Simple mental model**: "Camera is at position X, planet center is at position Y, compute intersection in the space they both inhabit."

---

## Summary

**The one-line fix**:
- Remove: `rayOriginPlanet = rayOriginKm + float3(0, 0, planetRadius)`
- Use: `rayOriginWorld = rayOriginKm` (no offset)
- Ensure planet center is explicitly passed and used in the sphere equation

This aligns UzEngine with UE5's volumetric cloud system and is the only mathematically correct approach.

