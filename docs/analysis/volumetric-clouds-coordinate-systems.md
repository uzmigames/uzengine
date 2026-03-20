<!-- doc-meta: volumetric-clouds-coordinate-systems; date: 2026-03-20; author: unreal-architect -->

# UE5 Volumetric Cloud Coordinate Systems — Visual Guide

## Diagram 1: UE5's Correct World-Space Approach

```
World Space (Kilometers)

                      +Y (up in world space)
                      |
          Camera at altitude ~500 km
          Pos: (0.1, 0.2, 500.0) km
                o
                |\ Ray direction
                | \
                |  \
      ========  |   \  ======= Top shell (radius 6375 km)
                |    \
      ========  |     \====== Bottom shell (radius 6365 km)
                |
                |
      =====================  Planet surface (radius 6360 km)
                |
                |
    +Z        Planet center at (0, 0, -6360) km
    ↓         (or relative to world origin)

    Ray: P(t) = cameraPos + t * rayDir
         where cameraPos = (0.1, 0.2, 500.0) km (WORLD SPACE)

    Sphere: center = (0, 0, -6360) km, radius = 6375 km

    Intersection: |P(t) - center| = radius
```

**Key point**: Both camera position and sphere center are in **the same world space**. No transformation.

---

## Diagram 2: Your Current (Incorrect) Planet-Relative Approach

```
Planet-Relative Space (Kilometers)

        Camera shifted by +planetRadius
        Effective pos: (0.1, 0.2, 500 + 6360) = (0.1, 0.2, 6860) km
                o
                |\ Ray direction
                | \
      ========  |   \  ======= Top shell (radius 6375 km)
                |    \
      ========  |     \====== Bottom shell (radius 6365 km)
                |
                |
      =====================  Planet surface (radius 6360 km)
         (0, 0, 0)
         Planet center AT ORIGIN (0, 0, 0)

    Ray: P(t) = (0.1, 0.2, 6860) + t * rayDir

    Sphere: center = (0, 0, 0), radius = 6375 km

    Problem: Camera is at 6860 km from planet center
             Shells are at 6365-6375 km
             Ray might intersect shells correctly, BUT...
             The problem is mixing coordinate systems inconsistently.
```

**The real issue**: If you shift the camera by `+planetRadius` but DON'T shift the shell center, you're implicitly saying:
- Camera altitude = 500 km (correct)
- Shell distance from camera = 6375 - 6860 = -485 km (NEGATIVE, which is wrong)

The shells end up BELOW the camera, not around it.

---

## Diagram 3: How Ray-Sphere Intersection Works (Math)

```
UE5 Implementation:

bool RayIntersectSphereSolution(float3 RayOrigin, float3 RayDirection,
                                float4 Sphere, inout float2 Solutions)
{
    // Step 1: Express ray origin relative to sphere center
    float3 LocalPosition = RayOrigin - Sphere.xyz;
    //       ^^^^^^^^^^^^                 ^^^^^^^^^^
    //       Camera position         Planet center position
    //       Both in WORLD space!

    // Step 2: Quadratic equation for ray-sphere intersection
    float a = dot(RayDirection, RayDirection);
    float b = 2 * dot(RayDirection, LocalPosition);
    float c = dot(LocalPosition, LocalPosition) - Sphere.w * Sphere.w;
    //                                           ^^^^^^^^
    //                                           Sphere radius squared

    // Step 3: Solve for t
    float discriminant = b*b - 4*a*c;
    if (discriminant >= 0) {
        float sqrt_disc = sqrt(discriminant);
        Solutions = (-b + float2(-1, 1) * sqrt_disc) / (2*a);
        return true;  // Ray intersects sphere
    }
    return false;  // No intersection
}

Example with your values:
  RayOrigin = (0.1, 0.2, 500.0) km [camera]
  Sphere.xyz = (0, 0, -6360.0) km [planet center]
  Sphere.w = 6375 km [top shell radius]

  LocalPosition = (0.1, 0.2, 500) - (0, 0, -6360)
               = (0.1, 0.2, 6860.0) km

  |LocalPosition| = sqrt(0.01 + 0.04 + 47041600) ≈ 6860 km

  Discriminant = 0 (marginal) — ray just grazes the sphere
                            OR negative — ray misses sphere entirely
```

---

## Diagram 4: Why Your Offset Fails

```
Your code:
  rayOriginPlanet = cameraPos(km) + float3(0, 0, planetRadius)

  cameraPos = (0, 0, 500) km [at altitude 500 km]
  + float3(0, 0, 6360)
  = rayOriginPlanet = (0, 0, 6860) km

But then you also use:
  bottomRadius = planetRadius + layerBottomAltitude = 6365 km
  topRadius = 6375 km

Implicitly, you're assuming planet center is at (0, 0, 0).

So:
  distance(rayOriginPlanet, center) = |(0, 0, 6860) - (0, 0, 0)| = 6860 km

But the shells are at:
  6365 km to 6375 km from center

This means shells are at:
  6365 km ≤ distance ≤ 6375 km

And camera is at 6860 km, which is OUTSIDE the shell range!

The ray might intersect if it angles toward the planet, but the math is wrong
because you're inconsistently using two different coordinate systems:
  - Planet-relative for the camera (offset)
  - World-relative (implicit) for the shells
```

---

## Diagram 5: Correct Fix — World Space (Match UE5)

```
C++ (CPU setup):
  FVector cameraPosWorldCm = ...;  // e.g., (100, 200, 5000000) cm
  FVector planetCenterCm = FVector(0, 0, -636000000);  // 6360 km below origin
  float planetRadiusKm = 6360.0f;

HLSL (Shader):
  float3 cameraWorldPosKm = cameraPosWorldCm * CENTIMETER_TO_KILOMETER;
                          = (0.001, 0.002, 50) km (converted from cm)
  float3 planetCenterKm = planetCenterCm * CENTIMETER_TO_KILOMETER;
                        = (0, 0, -6360) km
  float topRadiusKm = 6375 km;
  float bottomRadiusKm = 6365 km;

  // Direct world-space intersection — no offsets!
  RayIntersectSphereSolution(cameraWorldPosKm, rayDir,
      float4(planetCenterKm, topRadiusKm), tTop);

  // This computes:
  // LocalPosition = (0.001, 0.002, 50) - (0, 0, -6360)
  //               = (0.001, 0.002, 6410) km
  // distance = 6410 km (camera at ~50 km altitude, correct!)
  // Shells at 6365-6375 km from center
  // Ray will intersect if pointing toward planet
```

---

## Diagram 6: Altitude Calculation — Correct vs. Wrong

```
After ray intersection, at a point along the ray:

CORRECT (UE5 approach):
  samplePosWorld = rayOriginWorld + t * rayDir
                 = (0.001, 0.002, 50) + t * rayDir [km]
  distFromCenter = |samplePosWorld - planetCenter|
                 = |samplePosWorld - (0, 0, -6360)|
  altitude = distFromCenter - planetRadius

  If samplePos = (0.001, 0.002, 6370), then:
    distFromCenter ≈ 6370 km
    altitude ≈ 6370 - 6360 = 10 km ✓

WRONG (your offset approach):
  samplePosShifted = rayOriginShifted + t * rayDir
                   = (0, 0, 6860) + t * rayDir [km]
  distFromCenter = |samplePosShifted - (0, 0, 0)|
  altitude = distFromCenter - planetRadius

  If samplePos = (0, 0, 6370), then:
    distFromCenter = 6370 km
    altitude = 6370 - 6360 = 10 km

  But you've implicitly assumed planet is at origin and camera is AT
  (0, 0, 6860), which is inconsistent with actual world positions.
  All your density lookups will be off by a constant shift.
```

---

## Key Takeaway

| Aspect | UE5 | Your Code | Problem |
|--------|-----|-----------|---------|
| Camera position space | World space | World space + offset (planet-relative) | Inconsistent with shell center |
| Planet center space | World space | Implicit at origin | Not in same space as camera |
| Ray-sphere intersection | World space - world space | Shifted world space - origin | Camera is outside shells |
| Altitude computation | distFromCenter - radius | distFromCenter - radius | distFromCenter is wrong |
| Coordinate consistency | ✓ All world space | ✗ Mixed spaces | Intersection fails |

**Solution**: Use UE5's approach — keep everything in the same world space, no offsets.

