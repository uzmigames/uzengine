<!-- doc-meta: volumetric-clouds-analysis-index; date: 2026-03-20; author: unreal-architect -->

# Volumetric Cloud Analysis Documents

This folder contains a complete analysis of UE5's volumetric cloud system, specifically how it handles ray-sphere intersection for the cloud layer shell.

## Documents

### 1. **VOLUMETRIC-CLOUDS-SUMMARY.md** (START HERE)
   - **Purpose**: High-level overview and diagnosis
   - **Length**: ~300 lines
   - **Contains**:
     - TL;DR of the problem and solution
     - Answers to your 4 specific questions
     - Explanation of your bug
     - Example setup
   - **Best for**: Understanding the problem quickly

### 2. **volumetric-clouds-camera-positioning.md**
   - **Purpose**: Detailed technical analysis with source code
   - **Length**: ~200 lines
   - **Contains**:
     - How UE5 defines cloud layer shell (inner/outer radius)
     - Camera position relative to planet center
     - Coordinate system used in ray marching
     - How UE5 converts world position to planet-relative (hint: it doesn't, at intersection time)
     - Complete code excerpts from UE5 with line numbers
   - **Best for**: Understanding the "why" and reviewing UE5 source

### 3. **volumetric-clouds-coordinate-systems.md**
   - **Purpose**: Visual explanations and diagrams
   - **Length**: ~300 lines
   - **Contains**:
     - Diagram 1: UE5's correct world-space approach
     - Diagram 2: Your current (incorrect) planet-relative approach
     - Diagram 3: Ray-sphere intersection math
     - Diagram 4: Why your offset fails
     - Diagram 5: Correct fix
     - Diagram 6: Altitude calculation comparison
   - **Best for**: Visual learners and understanding the geometry

### 4. **volumetric-clouds-fix-plan.md**
   - **Purpose**: Step-by-step implementation guide
   - **Length**: ~250 lines
   - **Contains**:
     - Before/after code comparison
     - 5-step fix procedure
     - Validation checklist
     - Expected results for each test case
     - Code references to UE5 source
   - **Best for**: Implementing the fix in your codebase

### 5. **volumetric-clouds-diagnostic-values.md**
   - **Purpose**: Expected numerical values for validation
   - **Length**: ~350 lines
   - **Contains**:
     - Test case: camera at 50 km altitude
     - Step-by-step expected values for UE5 approach
     - Step-by-step expected values for your broken approach
     - Why your approach fails (explanation with math)
     - Validation test sheet
   - **Best for**: Debugging — if your values don't match, find where they diverge

---

## Quick Diagnosis

Run this mental checklist:

1. **Do you use `rayOriginPlanet = cameraPos + float3(0, 0, planetRadius)`?**
   - YES → Your code is broken. Remove the offset.
   - NO → Check if your planet center is in the same world space as camera pos.

2. **Is your planet center passed as an explicit shader constant?**
   - NO → It must be. Add it to your constant buffer.
   - YES → Verify it's in world space (km), same coordinate system as camera.

3. **Do your ray-sphere intersections use world-space-to-world-space?**
   - Pseudo-code: `RayIntersect(cameraWorldPos, rayDir, float4(planetCenterWorldPos, radius))`
   - YES → You're on the right track. Check the discriminant values in diagnostic doc.
   - NO → You're mixing coordinate systems. See fix-plan.md.

4. **Are your altitude calculations using `distFromCenter - planetRadius`?**
   - YES → Check if they're computed correctly with world-space distance.
   - NO → You'll get wrong altitudes. See diagnostic-values.md for expected numbers.

---

## UE5 Source References

All analysis is based on these files in `W:\UE_5.6_Source`:

- **Shader**: `Engine/Shaders/Private/VolumetricCloud.usf`
  - Lines 231-253: `RayIntersectSphereSolution()` function
  - Lines 490-493: Camera positioning (`RayWorldOriginKm`)
  - Lines 531-560: Shell intersection logic

- **C++ Setup**: `Engine/Source/Runtime/Renderer/Private/VolumetricCloudRendering.cpp`
  - Line ~250: Planet center and radii setup

- **Data Structure**: `Engine/Source/Runtime/Engine/Public/Rendering/SkyAtmosphereCommonData.h`
  - Lines 17-61: `FAtmosphereSetup` struct with `PlanetCenterKm`

---

## The One-Sentence Fix

**Remove `rayOriginPlanet = rayOriginKm + float3(0, 0, planetRadius)` and pass planet center explicitly to the shader in the same world space as the camera.**

---

## File Reading Order

- **For quick fix**: SUMMARY → FIX-PLAN → implement
- **For understanding**: SUMMARY → COORDINATE-SYSTEMS → CAMERA-POSITIONING → implement
- **For validation**: DIAGNOSTIC-VALUES → run tests → adjust code
- **For reference**: CAMERA-POSITIONING (has all UE5 source code)
