<!-- doc-meta: volumetric-clouds-camera-positioning; date: 2026-03-20; author: unreal-architect -->

# UE5 Volumetric Cloud Ray-Sphere Intersection Analysis

**Status**: Research complete — ray-sphere setup, camera positioning, and coordinate transformation documented.

## Key Finding: The Camera is NOT at Planet Center

Your current approach has **one critical error**:

```cpp
// WRONG — you are shifting the camera by planetRadius
rayOriginPlanet = cameraPos(km) + float3(0, 0, planetRadius);
```

**UE5 DOES NOT DO THIS.** Instead, UE5 uses `RayWorldOriginKm` which is either:
1. **Perspective view**: Directly `View.WorldCameraOrigin` (in kilometers)
2. **Orthographic view**: `View.TranslatedWorldCameraOrigin - View.PreViewTranslation` (in kilometers)

The camera stays at its world position. The planet center is transformed separately and passes to the shader as `CloudLayerCenterKm`.

---

## 1. How UE5 Defines the Cloud Layer Shell

### Shell Definition (VolumetricCloudRendering.cpp, line ~250)

```cpp
float PlanetRadiusKm = CloudProxy.PlanetRadiusKm;
if (SkyInfo)
{
    const FAtmosphereSetup& AtmosphereSetup = SkyInfo->GetSkyAtmosphereSceneProxy().GetAtmosphereSetup();
    PlanetRadiusKm = AtmosphereSetup.BottomRadiusKm;
    CloudGlobalShaderParams.CloudLayerCenterKm = (FVector3f)AtmosphereSetup.PlanetCenterKm;
}
else
{
    // Default: planet center at (0, 0, -PlanetRadiusKm)
    CloudGlobalShaderParams.CloudLayerCenterKm = FVector3f(0.0f, 0.0f, -PlanetRadiusKm);
}

// These are the RADII from the planet center, NOT altitudes
CloudGlobalShaderParams.BottomRadiusKm = PlanetRadiusKm + CloudProxy.LayerBottomAltitudeKm;
CloudGlobalShaderParams.TopRadiusKm    = CloudGlobalShaderParams.BottomRadiusKm + CloudProxy.LayerHeightKm;
```

**Key insight**: These are **RADII** (distances from planet center), not altitude values. They are computed as:
- `BottomRadiusKm = PlanetRadiusKm + LayerBottomAltitudeKm`
- `TopRadiusKm = BottomRadiusKm + LayerHeightKm`

### Example Values
If:
- `PlanetRadiusKm = 6360.0` (Earth standard)
- `LayerBottomAltitudeKm = 5.0`
- `LayerHeightKm = 10.0`

Then:
- `BottomRadiusKm = 6360.0 + 5.0 = 6365.0`
- `TopRadiusKm = 6365.0 + 10.0 = 6375.0`

This matches your setup exactly.

---

## 2. Camera Position Relative to Planet Center

### The Coordinate System (VolumetricCloud.usf, lines 490-493)

```hlsl
float3 RayTranslatedWorldOrigin = GetTranslatedWorldCameraPosFromView(SvPosition.xy);
float3 RayWorldOriginKm = select(IsOrthoProjection(),
    DFHackToFloat(DFSubtract(RayTranslatedWorldOrigin, PrimaryView.PreViewTranslation)) * CENTIMETER_TO_KILOMETER,
    DFHackToFloat(ResolvedView.WorldCameraOrigin) * CENTIMETER_TO_KILOMETER
);
```

**This is the absolute world position of the camera in kilometers.** It is NOT relative to the planet center.

Then, the ray-sphere intersection happens:

```hlsl
// Ray origin is in world space (km), sphere center is CloudLayerCenterKm
if (RayIntersectSphereSolution(RayWorldOriginKm, Raydir,
    float4(RenderVolumetricCloudParameters.CloudLayerCenterKm, RenderVolumetricCloudParameters.TopRadiusKm),
    tTop2))
```

The `RayIntersectSphereSolution` function **internally** computes the ray origin relative to the sphere center:

```hlsl
bool RayIntersectSphereSolution(float3 RayOrigin, float3 RayDirection, float4 Sphere, inout float2 Solutions)
{
    float3 LocalPosition = RayOrigin - Sphere.xyz;  // ← This is the magic line
    float LocalPositionSqr = dot(LocalPosition, LocalPosition);

    float3 QuadraticCoef;
    QuadraticCoef.x = dot(RayDirection, RayDirection);
    QuadraticCoef.y = 2 * dot(RayDirection, LocalPosition);
    QuadraticCoef.z = LocalPositionSqr - Sphere.w * Sphere.w;

    float Discriminant = QuadraticCoef.y * QuadraticCoef.y - 4 * QuadraticCoef.x * QuadraticCoef.z;

    if (Discriminant >= 0)
    {
        float SqrtDiscriminant = sqrt(Discriminant);
        Solutions = (-QuadraticCoef.y + float2(-1, 1) * SqrtDiscriminant) / (2 * QuadraticCoef.x);
        return true;
    }
    return false;
}
```

**Key**: The ray-sphere intersection works in world space. UE5 NEVER shifts the camera to be at the planet center. Instead:
- `RayOrigin = absolute world position (km)`
- `Sphere.xyz = CloudLayerCenterKm (absolute world position of planet center in km)`
- `LocalPosition = RayOrigin - Sphere.xyz` (relative to planet)

---

## 3. Coordinate System for Ray Marching

**UE5 uses WORLD SPACE for everything:**

1. **Camera position**: `RayWorldOriginKm` = absolute world position in km
2. **Planet center**: `CloudLayerCenterKm` = absolute world position of planet center in km
3. **Ray direction**: Standard camera ray in world space
4. **Ray-sphere intersection**: Works entirely in world space — the function internally computes relative position

**No transformation to planet-centered coordinates happens at any point.** The planet center is just another position in world space.

---

## 4. Converting World Position to Planet-Centered Coordinates

**UE5 doesn't do this explicitly in the cloud shader.** Instead:

1. **Keep everything in world space** for the ray-sphere intersection
2. **Only use planet-relative coordinates** inside material evaluation loops if needed

But the intersection itself is entirely in world space. If you need planet-relative coordinates for perlin noise sampling or other material work, you can compute it after the intersection:

```hlsl
// After ray-sphere intersection, at distance t along the ray:
float3 SamplePositionWorld = RayWorldOriginKm + Raydir * t;
float3 SamplePositionPlanetRelative = SamplePositionWorld - CloudLayerCenterKm;
float DistanceFromPlanetCenter = length(SamplePositionPlanetRelative);
float Altitude = DistanceFromPlanetCenter - PlanetRadiusKm;
```

---

## Your Bug: The Missing `float3(0,0,planetRadius)` Offset

Your current code:

```cpp
rayOriginPlanet = cameraPos(km) + float3(0,0,planetRadius)
bottomRadius = planetRadius + layerBottomAltitude
topRadius = bottomRadius + layerHeight
```

**The problem:**

You're shifting the camera by `+planetRadius` in Z, which means:
- If `cameraPos = (0, 0, 0)` (at origin), you shift it to `(0, 0, 6360)`
- The shell is centered at `(0, 0, 0)` in your system
- So the camera is 6360 km ABOVE the center, but your shells are still relative to center at origin

This is the **opposite** of the standard setup where the planet center is below the camera.

### The Standard Setup

In UE5 (and typically in atmospheric rendering):

1. **Camera location in world space**: Some arbitrary position, e.g., `(100, 200, 50000)` cm
2. **Planet center in world space**: Typically `(0, 0, -PlanetRadiusKm * 100000)` = `(0, 0, -6.36e8)` cm
   - OR stored separately as `(0, 0, -6360)` km

When you convert to km:
- **Camera in km**: `(0.001, 0.002, 500)` km
- **Planet center in km**: `(0, 0, -6360)` km
- **Distance from camera to planet center**: ~6860 km (camera is at ~500 km altitude)

### Why Your Setup Fails

If you use:

```cpp
rayOriginPlanet = cameraPos + float3(0, 0, planetRadius)
```

You're effectively saying "put the camera at planet-relative height = planetRadius", which makes the camera AT the planet surface, not above it. Then:
- `bottomRadius = 6360 + 5 = 6365` (shell bottom)
- `topRadius = 6375` (shell top)
- `rayOriginPlanet = ~6360` (camera height)

The camera is roughly at the shell boundary, which is why you get no intersection. The shells might be tangent to the camera ray, or the ray might pass through them at a glancing angle.

---

## The Correct Approach for UzEngine

### Option 1: Copy UE5's Approach (Recommended)

```hlsl
// CPU side (C++)
// Planet center at some world position, e.g., (0, 0, -PlanetRadiusKm * KmToClm)
float3 planetCenterCm = ...;
float planetRadiusKm = 6360.0f;
float layerBottomAltitudeKm = 5.0f;
float layerHeightKm = 10.0f;

// Shader side (HLSL)
float3 cameraWorldPosKm = cameraWorldPosCm * CENTIMETER_TO_KILOMETER;
float3 planetCenterKm = planetCenterCm * CENTIMETER_TO_KILOMETER;
float bottomRadiusKm = planetRadiusKm + layerBottomAltitudeKm;  // 6365 km
float topRadiusKm = bottomRadiusKm + layerHeightKm;              // 6375 km

// Ray-sphere intersection (world space)
float2 tTop, tBottom;
RayIntersectSphereSolution(cameraWorldPosKm, rayDir,
    float4(planetCenterKm, topRadiusKm), tTop);
RayIntersectSphereSolution(cameraWorldPosKm, rayDir,
    float4(planetCenterKm, bottomRadiusKm), tBottom);
```

### Option 2: Alternative - Planet-Relative Coordinates

If you prefer planet-relative:

```hlsl
// Convert to planet-relative before intersection
float3 rayOriginRelative = cameraWorldPosKm - planetCenterKm;
float3 rayDir = ...;

// Now work in planet-relative coordinates
float distFromCenter = length(rayOriginRelative);
// distFromCenter should be ~6860 km if camera is at ~500 km altitude

RayIntersectSphereSolution(rayOriginRelative, rayDir,
    float4(0.0, 0.0, 0.0, topRadiusKm), tTop);
```

**But this still doesn't involve adding `planetRadius` to the camera position.**

---

## Summary: UE5's Exact Approach

| Component | UE5 Value | Your Value (Incorrect) |
|-----------|-----------|---|
| Camera position | World space (km) | World space (km) — **but then offset by +planetRadius** |
| Planet center | World space (km) | Implicit at (0,0,0) |
| Bottom radius | `PlanetRadiusKm + LayerBottomAltitudeKm` | Same ✓ |
| Top radius | `BottomRadiusKm + LayerHeightKm` | Same ✓ |
| Ray-sphere intersection coordinate system | World space | **Planet-shifted space (WRONG)** |

The fix: **Remove the `+ float3(0,0,planetRadius)` offset.** Use world space directly.

---

## References

- `W:\UE_5.6_Source\Engine\Shaders\Private\VolumetricCloud.usf` lines 231-253 (ray-sphere intersection)
- `W:\UE_5.6_Source\Engine\Shaders\Private\VolumetricCloud.usf` lines 490-493 (camera positioning)
- `W:\UE_5.6_Source\Engine\Shaders\Private\VolumetricCloud.usf` lines 531-560 (shell intersection logic)
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\VolumetricCloudRendering.cpp` line ~250 (shader parameter setup)
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Rendering\SkyAtmosphereCommonData.h` (FAtmosphereSetup structure)

