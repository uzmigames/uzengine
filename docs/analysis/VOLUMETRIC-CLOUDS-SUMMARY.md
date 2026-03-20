<!-- doc-meta: volumetric-clouds-summary; date: 2026-03-20; author: unreal-architect -->

# Volumetric Cloud Ray-Sphere Intersection: Complete Analysis Summary

**TL;DR**: Your current approach uses `rayOriginPlanet = cameraPos + float3(0,0,planetRadius)`, which shifts the camera into planet-relative space. UE5 does NOT do this. UE5 keeps everything in world space and lets the ray-sphere intersection function internally compute relative positions. **Remove the offset.**

---

## The Four Questions You Asked

### 1. How does UE5 define the cloud layer shell (inner/outer radius)?

**Answer**: Two radii from planet center, in kilometers:
- `BottomRadiusKm = PlanetRadiusKm + LayerBottomAltitudeKm`
- `TopRadiusKm = BottomRadiusKm + LayerHeightKm`

Example: If planet radius is 6360 km, bottom altitude is 5 km, and layer height is 10 km:
- `BottomRadiusKm = 6365 km` (distance from planet center to shell bottom)
- `TopRadiusKm = 6375 km` (distance from planet center to shell top)

These are **distances from planet center**, not world coordinates.

**UE5 Source**: `VolumetricCloudRendering.cpp` lines ~250-256

---

### 2. How does UE5 handle camera position relative to planet center for ray marching?

**Answer**: It keeps the camera in **absolute world space**. The planet center is ALSO in world space. The ray-sphere intersection function internally computes the relative position.

```hlsl
// UE5 approach
float3 cameraWorldPosKm = ...;        // e.g., (0, 0, 50) km — world space
float3 planetCenterKm = ...;          // e.g., (0, 0, -6360) km — world space
float topRadiusKm = 6375.0f;

// Ray-sphere intersection happens in world space
RayIntersectSphereSolution(cameraWorldPosKm, rayDir,
    float4(planetCenterKm, topRadiusKm), tTop);

// Inside the function:
// float3 LocalPosition = cameraWorldPosKm - planetCenterKm;  // ← Relative position computed here
```

**Key insight**: The ray-sphere intersection function handles the transformation. You don't need to shift the camera yourself.

**UE5 Source**: `VolumetricCloud.usf` lines 490-493, 231-253

---

### 3. What coordinate system does UE5 use for the cloud raymarch? Is the camera at planet surface or planet center?

**Answer**: The camera is at its absolute world position, which is typically **above** the planet surface (at some altitude). The coordinate system is **world space**.

Example setup:
- Planet center: `(0, 0, -6360) km` = 6360 km below world origin
- Planet surface: At distance 6360 km from planet center = roughly at z ≈ 0 km
- Camera: `(0, 0, 50) km` = 50 km above world origin ≈ 50 km altitude above planet surface
- Shell bottom: 6365 km from planet center = 5 km above surface
- Shell top: 6375 km from planet center = 15 km above surface

**There is NO shifting of the camera.** The camera stays at (0, 0, 50). The shells are defined as radii from the planet center at (0, 0, -6360).

**UE5 Source**: `VolumetricCloud.usf` lines 531-560 (shell intersection logic uses world-space coordinates directly)

---

### 4. How does UE5 convert world position to planet-centered coordinates?

**Answer**: UE5 does NOT convert world position to planet-centered coordinates at the ray-sphere intersection step. Instead:

1. **Ray-sphere intersection happens in world space** — camera at world position, planet center at world position
2. **After intersection**, if you need planet-relative coordinates for material sampling, you compute them:

```hlsl
// After ray intersection at distance t
float3 samplePosWorld = cameraWorldPosKm + rayDir * t;
float3 samplePosPlanetRelative = samplePosWorld - planetCenterKm;
float distFromCenterKm = length(samplePosPlanetRelative);
float altitudeKm = distFromCenterKm - planetRadiusKm;

// Use altitudeKm for density lookups, etc.
```

**But this is NOT used for the intersection itself.** The intersection is purely in world space.

**UE5 Source**: `VolumetricCloud.usf` lines 531-546 (uses `RayWorldOriginKm` directly, no conversion)

---

## Your Specific Bug

You're using:
```cpp
rayOriginPlanet = cameraPos(km) + float3(0,0,planetRadius)
```

This **shifts the camera by `+planetRadius` (6360 km)**. So:
- If camera is at (0, 0, 50) km world position
- You shift it to (0, 0, 6410) km in "planet-relative" space
- Your shells are at 6365-6375 km from implicit origin
- Camera is now 6410 km from planet center, OUTSIDE the 6365-6375 km shells

**Result**: Ray-sphere intersection fails because the camera is outside the shell range.

---

## The Fix

**Remove the offset.** Use world space directly:

```hlsl
// WRONG (current)
float3 rayOriginPlanet = rayOriginKm + float3(0, 0, planetRadius);
RayIntersectSphereSolution(rayOriginPlanet, rayDir,
    float4(0, 0, 0, topRadius), ...);  // Implicit planet at origin

// CORRECT (UE5 approach)
float3 cameraWorldPosKm = rayOriginKm;  // No offset
float3 planetCenterKm = ...;  // Pass from CPU as constant
RayIntersectSphereSolution(cameraWorldPosKm, rayDir,
    float4(planetCenterKm, topRadius), ...);  // Explicit planet center
```

---

## Complete Example

### C++ Setup (CPU)

```cpp
// Planet center is 6360 km below origin
float3 planetCenterCm = FVector3f(0, 0, -6360.0f * 100000.0f);  // -6.36e8 cm
float planetRadiusKm = 6360.0f;
float layerBottomAltitudeKm = 5.0f;  // Cloud layer starts 5 km above surface
float layerHeightKm = 10.0f;         // Cloud layer is 10 km thick

// Pass to shader:
// - cameraPosWorldCm (camera's absolute world position in cm, e.g., 5,000,000 cm altitude)
// - planetCenterCm (absolute world position of planet center in cm)
// - planetRadiusKm, layerBottomAltitudeKm, layerHeightKm
```

### HLSL Shader (GPU)

```hlsl
// Convert to kilometers
float3 cameraWorldPosKm = cameraPosWorldCm * CENTIMETER_TO_KILOMETER;
float3 planetCenterKm = planetCenterCm * CENTIMETER_TO_KILOMETER;

// Shell radii (distances from planet center)
float bottomRadiusKm = planetRadiusKm + layerBottomAltitudeKm;  // 6365 km
float topRadiusKm = bottomRadiusKm + layerHeightKm;              // 6375 km

// Intersection (world space — world space, no offset)
float2 tTop, tBottom;
RayIntersectSphereSolution(cameraWorldPosKm, rayDir,
    float4(planetCenterKm, topRadiusKm), tTop);
RayIntersectSphereSolution(cameraWorldPosKm, rayDir,
    float4(planetCenterKm, bottomRadiusKm), tBottom);

// ... process intersections ...

// At a sample point, compute altitude
float3 samplePosWorld = cameraWorldPosKm + rayDir * t;
float distFromCenterKm = length(samplePosWorld - planetCenterKm);
float altitudeKm = distFromCenterKm - planetRadiusKm;
// altitudeKm will be in range [5, 15] for samples inside the cloud layer
```

---

## Reference Materials

Three new analysis documents have been created:

1. **`volumetric-clouds-camera-positioning.md`** — Detailed analysis of UE5's camera and planet center positioning, with full C++ and HLSL code excerpts and line numbers.

2. **`volumetric-clouds-coordinate-systems.md`** — Visual diagrams showing:
   - UE5's correct world-space approach
   - Your current (incorrect) planet-relative approach
   - Why the approaches differ
   - How ray-sphere intersection works mathematically
   - Altitude calculation differences

3. **`volumetric-clouds-fix-plan.md`** — Step-by-step fix instructions with:
   - Before/after code comparisons
   - Validation checklist
   - Expected results
   - Code references to UE5 source

---

## Why This Matters

**Coordinate system consistency** is fundamental to 3D graphics. When you:
- Express camera in one space (world)
- Express shells in another space (implicit origin-relative)
- Try to intersect them

...the math breaks down. The ray-sphere intersection equation assumes both inputs are in the same space.

UE5's approach is simple:
- One global world space
- Camera at position P in world space
- Planet center at position Q in world space
- Intersection computed using `P - Q` to get relative position

Your approach tries to use two spaces simultaneously:
- Camera shifted to planet-relative space
- Shells still in implicit origin-relative space
- Result: inconsistent and wrong

**The fix is straightforward**: use one space (world space) for everything.

---

## Files Modified / Created

- `docs/analysis/volumetric-clouds-camera-positioning.md` — Complete source analysis
- `docs/analysis/volumetric-clouds-coordinate-systems.md` — Visual reference and diagrams
- `docs/analysis/volumetric-clouds-fix-plan.md` — Actionable fix steps
- `docs/analysis/VOLUMETRIC-CLOUDS-SUMMARY.md` — This file

---

## Next Steps

1. **Update your shader code** to remove the `+ float3(0, 0, planetRadius)` offset
2. **Ensure planet center is passed** from CPU to shader as an explicit constant
3. **Verify altitude calculations** use `distFromCenter - planetRadius` consistently
4. **Test** with camera at various altitudes (0 km, 10 km, 500 km, etc.)
5. **Validate** that rays toward/away from planet produce correct intersections

After implementing these changes, your cloud layer will intersect correctly.

