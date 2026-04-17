<!-- doc-meta
title: IBL Reflection Quality Review
author: shader-engineer agent
date: 2026-03-24
status: analysis
-->

# IBL Reflection Quality Review

Analysis of why HDRI environment reflections look flat/wrong on metallic spheres.

---

## Issues Found

### BUG 1 (HIGH): `skyLightColor` is never applied to IBL ambient

**File**: `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`
**Lines**: 50-52, 408

The cbuffer declares `skyLightColorR`, `skyLightColorG`, `skyLightColorB` but the deferred lighting shader **never reads them**. The ambient term at line 408 only multiplies by `skyLightIntensity`:

```hlsl
// Line 408 — current (wrong):
ambient = (diffuseAmbient * ao + specularAmbient) * skyInt;
```

Should be:

```hlsl
float3 skyColor = float3(skyLightColorR, skyLightColorG, skyLightColorB);
ambient = (diffuseAmbient * ao + specularAmbient) * skyInt * skyColor;
```

**Impact**: If the CPU sets a non-white sky light color (e.g., warm sunset tint), it is completely ignored in the IBL path. This makes reflections look wrong under colored lighting conditions.

**Reference**: Karis 2013 "Real Shading in Unreal Engine 4" -- the sky light contribution is modulated by the light's color/tint.

---

### BUG 2 (HIGH): `WorldToCubemap` mapping is incorrect -- produces mirrored/rotated reflections

**File**: `runtime/shaders/runtime/include/ibl.hlsli`, line 54-56

The current mapping:
```hlsl
float3 WorldToCubemap(float3 dir) {
    return float3(dir.x, -dir.z, dir.y);
}
```

UzEngine coordinate system (Z-up, left-handed):
- X = right
- Y = forward
- Z = up

DX12 cubemap coordinate system (Y-up, left-handed):
- X = right
- Y = up
- Z = forward (into screen, +Z)

The correct mapping from Z-up LH to Y-up LH is:
- cubemap.X = world.X (right stays right)
- cubemap.Y = world.Z (world up -> cubemap up)
- cubemap.Z = world.Y (world forward -> cubemap forward)

So the correct transform is `(X, Y, Z) -> (X, Z, Y)`.

The current code uses `(X, -Z, Y)` which **negates the up-to-forward component**. This flips the Z axis of the cubemap lookup, producing a **left-right mirror** on the forward/back axis. For environment reflections this means:

- Reflections from the "front" of the scene appear as if from the "back"
- The sky gradient appears inverted on the horizontal plane
- On a perfect mirror sphere, the HDRI image will be mirrored along one axis

**Correct code**:
```hlsl
float3 WorldToCubemap(float3 dir) {
    return float3(dir.x, dir.z, dir.y);
}
```

**However**, there is a subtlety: the `CubeFaceDirection` function in the same file produces directions in Y-up DX cubemap space, and the irradiance/prefilter shaders sample the environment cubemap using those directions directly (without `WorldToCubemap`). The `WorldToCubemap` is only used at **runtime sampling** in the deferred lighting shader. So the cubemap content itself is baked in Y-up space correctly, and only the runtime lookup direction is affected.

**Wait -- critical nuance**: If the equirect-to-cubemap conversion bakes the cubemap in standard DX Y-up space (it does -- `CubeFaceDirection` produces Y-up directions), then the cubemap content is in Y-up. When sampling at runtime, we need to convert the world-space reflection vector from Z-up to the same Y-up. The negate on Z (`-dir.z`) would only be correct if the engine used a right-handed Y-up convention where Z points backward. Since UzEngine is **left-handed** Z-up, and DX cubemaps are **left-handed** Y-up, the mapping should be `(X, Z, Y)` without negation.

**To verify**: check if the current `-Z` was introduced to fix a visual issue where reflections appeared upside-down. If so, the real fix was probably needed elsewhere (e.g., in the equirect mapping or the normal encoding). The `-Z` here would mask one bug while introducing a mirror.

**Impact**: Reflections are mirrored. On a metallic sphere, the HDRI environment will look "wrong" because left and right (or front and back) are swapped in the reflection.

---

### BUG 3 (MEDIUM): Prefilter shader samples source cubemap at mip 0 always -- no PDF-based mip selection

**File**: `runtime/shaders/runtime/ibl/prefilter_cs.hlsl`, line 88

```hlsl
prefilteredColor += envCube.SampleLevel(linearSampler, L, 0.0f).rgb * NdotL;
```

The prefilter always samples the environment cubemap at mip level 0. For low roughness this is fine (sharp reflection), but for higher roughness values, the importance samples spread out and each sample covers a large solid angle. Sampling at mip 0 causes:

1. **Aliasing/sparkle** at high roughness (undersampling the environment)
2. **Noise** because bright small features (sun, specular highlights) get randomly sampled

The standard approach (Karis 2013 course notes, Lagarde & de Rousiers 2014 "Moving Frostbite to PBR") is to compute the PDF of each sample and derive a mip level:

```hlsl
float D   = D_GGX(NdotH, clampedRoughness);
float pdf = D * NdotH / (4.0 * HdotV) + 0.0001;
float saTexel  = 4.0 * PI / (6.0 * faceSize * faceSize);
float saSample = 1.0 / (float(sampleCount) * pdf + 0.0001);
float mipLevel = (roughness == 0.0) ? 0.0 : 0.5 * log2(saSample / saTexel);
```

Then sample at that mip: `envCube.SampleLevel(linearSampler, L, mipLevel)`.

**Impact**: Higher roughness mips will show more noise/sparkle than expected. With 256 samples this may be acceptable but the quality is sub-optimal, especially for roughness > 0.5.

**Reference**: Karis 2013 "Real Shading in Unreal Engine 4" -- section on filtered importance sampling.

---

### BUG 4 (MEDIUM): `CubeFaceDirection` face 2 and face 3 have incorrect V mapping

**File**: `runtime/shaders/runtime/include/ibl.hlsli`, lines 42-43

```hlsl
case 2: return float3( u,  1,  v);   // +Y face
case 3: return float3( u, -1, -v);   // -Y face
```

For DX12 cubemap conventions (D3D12 spec, matches D3D11):
- Face 2 (+Y, looking down): direction = `(u, 1, -v)` -- the V is negated
- Face 3 (-Y, looking up): direction = `(u, -1, v)` -- the V is NOT negated

The standard DX cubemap face directions are:

| Face | Major Axis | sc (maps to u) | tc (maps to v) | Direction |
|------|-----------|-----------------|-----------------|-----------|
| 0 (+X) | +X | -Z | -Y | `(1, -v, -u)` |
| 1 (-X) | -X | +Z | -Y | `(-1, -v, u)` |
| 2 (+Y) | +Y | +X | +Z | `(u, 1, v)` |
| 3 (-Y) | -Y | +X | -Z | `(u, -1, -v)` |
| 4 (+Z) | +Z | +X | -Y | `(u, -v, 1)` |
| 5 (-Z) | -Z | -X | -Y | `(-u, -v, -1)` |

**Wait** -- looking at the OpenGL cubemap spec (which DX follows for face layout), the tc for +Y face is +Z (forward), and for -Y face is -Z. Let me re-derive:

The standard mapping from the OpenGL/DX spec table:

| Face | sc | tc | ma |
|------|----|----|-----|
| +X | -z | -y | x |
| -X | z | -y | x |
| +Y | x | z | y |
| -Y | x | -z | y |
| +Z | x | -y | z |
| -Z | -x | -y | z |

Where `u = 0.5 * sc/|ma| + 0.5`, `v = 0.5 * tc/|ma| + 0.5`.

Inverting to get direction from (u, v):
- `sc = (2u - 1) * |ma|`, `tc = (2v - 1) * |ma|`, and we set `|ma| = 1` for direction (normalize later).

For face 2 (+Y): `x = sc = (2u-1)`, `y = ma = 1`, `z = tc = (2v-1)` -> `(u_remapped, 1, v_remapped)` where u_remapped = 2u-1. This matches the code's `float3(u, 1, v)` since u and v are already remapped to [-1,1].

For face 3 (-Y): `x = sc = (2u-1)`, `y = ma = -1`, `z = -tc = -(2v-1)` -> `(u_remapped, -1, -v_remapped)`. This matches `float3(u, -1, -v)`.

**Correction**: After re-derivation, the current code for faces 2 and 3 is actually **correct** per the OpenGL/DX cubemap specification. The +Y face has `(u, 1, v)` and -Y has `(u, -1, -v)`. This is NOT a bug.

---

### BUG 5 (LOW): Irradiance shader uses only 64 samples -- may produce banding on smooth gradients

**File**: `runtime/shaders/runtime/ibl/irradiance_cs.hlsl`, line 71

With `SAMPLE_COUNT = 64`, the cosine-weighted hemisphere convolution may not fully converge for HDRIs with strong directional lighting (e.g., sun). This can produce:
- Slight banding in smooth irradiance gradients
- Inconsistent brightness when camera rotates (different faces converge to slightly different values)

Standard practice is 256-1024 samples for irradiance. Since this is a one-time bake (not per-frame), increasing to 256 would have negligible cost.

**Impact**: Minor -- irradiance is low frequency so 64 samples may be "good enough" but could explain subtle quality issues in the diffuse IBL term.

---

### BUG 6 (LOW): `envBRDF` split-sum formula uses F90 differently from Karis 2013

**File**: `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`, lines 389-391

```hlsl
float  F90 = saturate(50.0 * F0.g);
float3 specularAmbient = prefilteredColor * (F0 * envBRDF.x + F90 * envBRDF.y);
```

This is actually the **correct** UE4/UE5 formulation. The standard Karis 2013 formula is:
```
specular = prefilteredColor * (F0 * envBRDF.x + envBRDF.y)
```

The `F90 * envBRDF.y` version is a refinement that accounts for the fact that the Fresnel at 90 degrees isn't always 1.0 -- for dielectrics with very low F0 (like water, glass), F90 should still saturate to 1.0, but for metals with very high F0, the F90 term matters less. The `saturate(50.0 * F0.g)` computes an approximate F90.

**Verdict**: This is correct and matches UE5's `EnvBRDF()` function. NOT a bug.

---

### BUG 7 (MEDIUM): `iblEnergy` fallback kills real IBL data when environment is dark

**File**: `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`, lines 411-414

```hlsl
float iblEnergy = dot(prefilteredColor, float3(0.299, 0.587, 0.114));
if (iblEnergy < 0.001) {
    ambient = lerp(baseColor * 0.05, F0 * 0.15, metallic) * skyInt;
}
```

This fallback fires when the prefiltered cubemap is very dark. While the intent is to handle "first frames" before IBL is baked, it will also fire for:
- Dark environments (indoor, night scenes)
- Directions pointing toward dark areas of the HDRI (floor, dark buildings)
- Metallic surfaces reflecting a dark part of the environment

When it fires, it **replaces** the correct (dark) IBL result with a flat `F0 * 0.15` approximation, making dark reflections brighter than they should be. This could make metallic spheres look "flat" because dark reflection regions get clamped up.

**Fix**: Either remove this fallback entirely (if IBL is always baked before first render), or key it on a flag like `iblReady` from the CPU rather than thresholding the actual reflection color.

---

## Summary of Issues by Severity

| # | Severity | File | Description |
|---|----------|------|-------------|
| 1 | HIGH | deferred_lighting_ps.hlsl:408 | `skyLightColor` RGB never applied to IBL ambient |
| 2 | HIGH | ibl.hlsli:55 | `WorldToCubemap` uses `(X, -Z, Y)` instead of `(X, Z, Y)` -- mirrors reflections |
| 3 | MEDIUM | prefilter_cs.hlsl:88 | Prefilter always samples mip 0 -- noisy at high roughness |
| 4 | N/A | ibl.hlsli:42-43 | CubeFaceDirection faces 2/3 -- CORRECT after re-derivation |
| 5 | LOW | irradiance_cs.hlsl:71 | Only 64 samples for irradiance convolution |
| 6 | N/A | deferred_lighting_ps.hlsl:389-391 | F90 split-sum -- CORRECT, matches UE5 |
| 7 | MEDIUM | deferred_lighting_ps.hlsl:411-414 | Dark-environment fallback replaces real IBL with flat approximation |

## Recommended Fix Priority

1. **BUG 2** -- Fix `WorldToCubemap` to `(X, Z, Y)`. This is likely the primary cause of "flat/wrong" reflections. Mirrored reflections on a sphere look subtly wrong to the eye -- the highlights don't match the environment, making the surface appear incorrectly lit.

2. **BUG 1** -- Apply `skyLightColor` to the ambient term. Simple one-line fix.

3. **BUG 7** -- Replace the energy threshold fallback with an explicit `iblReady` flag, or at minimum raise the threshold to only catch truly empty (all-zero) cubemaps.

4. **BUG 3** -- Add PDF-based mip selection to the prefilter shader for cleaner high-roughness reflections.

5. **BUG 5** -- Increase irradiance sample count from 64 to 256 (optional quality improvement).
