<!-- doc-meta
title: Metallic Appearance Changes with Camera Movement — Diagnosis
project: UzEngine
created: 2026-03-23
keywords: metallic, IBL, camera movement, specular highlight, environment
-->

# Why Metallic Objects Look Different When You Move the Camera

## The Symptom

> Metallic spheres (gold, chrome) change appearance dramatically when moving the camera because only the directional light specular highlight provides light. Without IBL, (1-metallic) kills all diffuse, leaving metals with almost no light except specular.

## Root Cause Analysis

### The Math

When `metallic = 1.0`:

```
DiffuseColor = BaseColor - BaseColor * 1.0 = 0
SpecularColor = BaseColor (the metal's F0)
```

With a **single directional light** and **no IBL**:

```
TotalLighting = DirectDiffuse + DirectSpecular + IndirectDiffuse + IndirectSpecular
              = 0            + GGX(...)          + 0               + 0
              = GGX_highlight_only
```

### Why Camera Movement Matters

A **specular highlight is view-dependent**:

```hlsl
// GGX depends on:
float3 H = normalize(L + V);  // L = light direction, V = view direction
float VoH = dot(V, H);        // How much V aligns with half-vector
```

As the camera moves:
- **V changes** → H changes → VoH changes
- **Highlight position moves** with the camera
- When highlight is visible → bright
- When highlight leaves the surface → near-black
- **Result**: Dramatic appearance changes as you rotate view

### Comparison: With IBL

If you ADD a sky light:

```
TotalLighting = 0 + GGX(...) + IndirectDiffuse + IndirectSpecular
```

Now the surface is **lit from ALL directions** (the sky):
- Backlit side receives blue sky color
- Highlight moves but surface is never black
- Appearance changes less dramatically (still changes, but smoothly)

## Visual Timeline

### Scenario: Gold sphere, single directional light, NO IBL

**Frame 1: Camera in front**
```
View:     [Camera] ← Sphere ← Light
Highlight: ✓ (on front)
Result:   BRIGHT golden sphere with sharp highlight
```

**Frame 2: Camera moves to side**
```
View:     Sphere → [Camera]
          ↑ Light
Highlight: ✗ (on back, away from camera)
Result:   DARK (nearly black) sphere
```

**Frame 3: Camera moves to back**
```
View:     Light → Sphere ← [Camera]
Highlight: ✗ (on front, away from camera)
Result:   DARK (nearly black) sphere with backlit silhouette
```

### With IBL (sky light added)

**Frame 1: Camera in front**
```
Light:      ↓
Sky:        (hemispherical blue)
Highlight:  ✓ (front)
Result:     BRIGHT with golden highlight + blue tint on backlit
```

**Frame 2: Camera moves to side**
```
Light:      ↓
Sky:        (hemispherical blue)
Highlight:  ✗ (back)
Result:     MEDIUM BRIGHT (sky reflects off side)
```

**Frame 3: Camera moves to back**
```
Light:      ↓
Sky:        (hemispherical blue)
Highlight:  ✗ (front)
Result:     DARK but not black (blue sky visible, no sunlit highlight)
```

## Why This Is Correct Behavior

UE5 is doing this **correctly** according to physics:

1. **Metals are pure mirrors** — they only reflect light they can "see"
2. **Without environment**, a mirror reflects nothing
3. **Specular highlight** = reflection of the light source itself
4. **As camera moves**, the mirror angle changes → highlight moves
5. **When highlight is off-surface**, nothing is reflected → black

This is **physically accurate**. A real chrome sphere with no environment would behave exactly this way.

## How to Fix It in Your Scene

### Option 1: Add Sky Light (Recommended)

```cpp
// In your scene setup
if (skyLight == nullptr) {
    skyLight = CreateSkyLight();
    skyLight->SetCubemap(GenerateProceduralSky());
    skyLight->SetIntensity(1.0f);
}
```

**Effect**: Metallic objects now reflect the sky → visible on all camera angles

### Option 2: Add Reflection Captures

```cpp
// Place reflection probe near metallic objects
reflectionCapture = CreateReflectionCapture();
reflectionCapture->SetRadius(1000.cm);
reflectionCapture->CaptureScene();
```

**Effect**: Local reflection probes provide local environment reflection

### Option 3: Reduce Metallic for Debug

```cpp
// Temporary: set metallic to 0.7 instead of 1.0
float metallic = 0.7f;  // Instead of 1.0
```

**Effect**: 30% diffuse + 70% specular → visible even without IBL
- Not physically correct for metals, but useful for testing
- Will look like tarnished/oxidized metal

### Option 4: Add More Direct Lights

```cpp
// Add fill lights from different angles
AddLight(position: front,   color: white,  intensity: 1.0);
AddLight(position: left,    color: white,  intensity: 0.3);
AddLight(position: back,    color: white,  intensity: 0.2);
AddLight(position: overhead, color: white, intensity: 0.2);
```

**Effect**: Multiple highlights from different angles
- Not realistic (real world has one light), but practical
- Metallic now has light from multiple directions

## Verification: Is Your Implementation Correct?

### Test 1: Single Light, No IBL (Expected: Dramatic Changes)

Setup: Gold sphere, directional light only, no sky light
- Front view: Bright with sharp highlight
- Side view: Dark/dim
- Back view: Dark/dim
- **This is CORRECT** — metallic surfaces need IBL or multiple lights

### Test 2: Single Light + Sky Light (Expected: Smooth Gradation)

Setup: Same gold sphere + sky light
- Front view: Bright highlight + blue tint on bottom
- Side view: Blue/cyan (sky reflection from side)
- Back view: Dark blue (sky only, no sun)
- **This is CORRECT** — sphere now reflects the sky environment

### Test 3: Non-Metallic (metallic=0) Same Setup (Expected: Bright Everywhere)

Setup: Matte gray sphere, directional light, no sky light
- Front view: Bright (full diffuse + small specular)
- Side view: Medium (light coming from side, diffuse still works)
- Back view: Dim/gray (still receives diffuse light from shallow angle)
- **This is CORRECT** — diffuse materials are visible from all angles

### Test 4: Metallic vs Non-Metallic Side-by-Side

Setup: Two spheres (one metallic gold, one matte gold), one directional light
- **Metallic**: Bright only when highlight is on surface
- **Matte**: Consistently bright from all angles
- **Difference should be DRAMATIC**

## Summary: Camera Movement Behavior

| Condition | Metallic=1.0 | Metallic=0.0 | Why |
|-----------|--------------|--------------|-----|
| Single light, no IBL, front | Bright | Bright | |
| Single light, no IBL, side | Black | Medium | Metallic has zero diffuse |
| Single light, no IBL, back | Black | Dim | Metallic needs specular angle |
| Single light + sky light, front | Bright | Bright | |
| Single light + sky light, side | Medium-blue | Medium | Sky now provides light |
| Single light + sky light, back | Dark-blue | Dim-gray | Both see sky |

## Recommended Approach for UzEngine

Ensure your implementation:

1. ✓ Correctly sets `DiffuseColor = BaseColor * (1 - Metallic)`
2. ✓ Applies direct lighting to BOTH diffuse and specular
3. ✓ Applies IBL (sky/reflections) in a SEPARATE pass
4. ✓ When IBL is missing, metallic gets ZERO indirect light
5. ✓ When camera moves, specular highlight moves (expected)

If the behavior matches the tests above, your implementation is **correct and matches UE5**.

