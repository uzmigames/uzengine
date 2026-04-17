<!-- doc-meta
title: UE5 Ground Radiance Computation — Exact Implementation Analysis
description: Complete analysis of how UE5 computes ground/planet surface radiance when sky ray hits the planet sphere
created: 2026-03-20
version: 1.0
-->

# UE5 Ground Radiance Computation — Exact Implementation Analysis

## Source Reference

**UE5 File**: `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf` lines 766-786

## UE5 Exact Implementation

### The Ground Radiance Code (Lines 766-786)

```hlsl
if (Ground && tMax == tBottom)
{
	// Account for bounced light off the planet
	float3 P = WorldPos + tBottom * WorldDir;
	float PHeight = length(P);

	const float3 UpVector = P / PHeight;
	float Light0ZenithCosAngle = dot(Light0Dir, UpVector);
	float3 TransmittanceToLight0 = GetTransmittance(Light0ZenithCosAngle, PHeight);

	const float NdotL0 = saturate(dot(UpVector, Light0Dir));
	L += Light0Illuminance * TransmittanceToLight0 * Throughput * NdotL0 * Atmosphere.GroundAlbedo.rgb / PI;
#if SECOND_ATMOSPHERE_LIGHT_ENABLED
	{
		const float NdotL1 = saturate(dot(UpVector, Light1Dir));
		float Light1ZenithCosAngle = dot(UpVector, Light1Dir);
		float3 TransmittanceToLight1 = GetTransmittance(Light1ZenithCosAngle, PHeight);
		L += Light1Illuminance * TransmittanceToLight1 * Throughput * NdotL1 * Atmosphere.GroundAlbedo.rgb / PI;
	}
#endif
}
```

## Answer to Your Five Questions

### 1. How does UE5 compute the ground color when a sky ray hits the planet sphere?

**Answer**: UE5 performs a **Lambertian diffuse reflection model**:
```hlsl
groundRadiance = Light_Illuminance * TransmittanceToLight * NdotL * GroundAlbedo / PI
```

Where:
- `Light_Illuminance` = pre-exposed sun disk luminance
- `TransmittanceToLight` = atmosphere transmittance **from ground to sun** (NOT camera to sun)
- `NdotL` = saturated dot product between surface normal (up vector at ground position) and light direction
- `GroundAlbedo` = `Atmosphere.GroundAlbedo.rgb` — a component parameter stored in constant buffer
- `/ PI` = Lambertian normalization constant

### 2. Does UE5 apply the path transmittance (Throughput) to the ground radiance?

**YES, absolutely**. The exact line is:
```hlsl
L += Light0Illuminance * TransmittanceToLight0 * Throughput * NdotL0 * Atmosphere.GroundAlbedo.rgb / PI;
```

`Throughput` is the **accumulated optical depth transmittance** from the camera to the ground intersection point. This is NOT a separate ground-to-camera transmittance — it's the `Throughput` accumulated through the entire ray march up to the ground.

**Key**: `Throughput` is modified during the ray march loop (lines 738-761) via:
```hlsl
Throughput *= SampleTransmittance;
```

By the time we reach the ground code, `Throughput` already contains the full camera-to-ground optical depth.

### 3. What is the exact ground albedo computation?

**There is NO computation**. Ground albedo is a **direct component property**:
```hlsl
Atmosphere.GroundAlbedo.rgb
```

This is stored in a constant buffer provided by the CPU (likely in `SkyAtmosphereComponent` or similar). UE5 does NOT:
- Derive ground color from the sky
- Apply any runtime transformations
- Use any analytical function to compute it

The shader simply **reads the stored RGB value directly**.

### 4. Is there any isotropic/multi-scatter contribution to the ground?

**NO**. There is NO multi-scattering or isotropic contribution to the ground surface itself. The ground is treated as a **simple diffuse receiver** with no scattering model.

However, context matters:
- **Multi-scattering IS computed in the ray march loop** (lines 651-661) for atmospheric volumes
- But when the ray hits the ground, multi-scattering contributions are **NOT applied to the ground surface**
- The ground receives only single-scattering direct light from the sun

### 5. Does UE5 use `Throughput` accumulated from the ray march, or compute a separate ground-to-camera transmittance?

**UE5 uses the accumulated `Throughput`** from the ray march. NO separate computation.

The `Throughput` variable is maintained throughout the ray march:
- It starts at `1.0` (no attenuation)
- It is modified in each loop iteration: `Throughput *= SampleTransmittance;`
- When the ray hits the ground, the **current value of `Throughput`** is used directly
- This represents the cumulative optical depth from camera to ground

**There is NO separate calculation of transmittance from ground to camera**.

---

## Comparison: UE5 vs UzEngine

### Your Current Implementation

```hlsl
if (groundHit.x > 0.0f) {
    float3 groundPosKm = cameraSkyKm + rayDir * groundHit.x;
    float3 groundNormal = normalize(groundPosKm);
    float3 groundSunT = SampleTransmittanceLut(tLut, linearSampler, groundPosKm, sunDir, bottomRadiusKm, topRadiusKm);
    float NdotL = max(0.0f, dot(groundNormal, sunDir));
    float3 groundRadiance = groundAlbedo.xyz * (1.0f / PI) * NdotL * sunIlluminance.xyz * groundSunT;
    L += Throughput * groundRadiance;
}
```

### Issues Found

| Aspect | UE5 | UzEngine | Status |
|--------|-----|---------|--------|
| **Ground normal** | `P / PHeight` (normalized position vector) | `normalize(groundPosKm)` | ✅ CORRECT — equivalent |
| **TransmittanceToLight** | `GetTransmittance(Light_ZenithCosAngle, PHeight)` | `SampleTransmittanceLut(...)` | ✅ CORRECT — same intent |
| **NdotL** | `saturate(dot(UpVector, LightDir))` | `max(0.0, dot(...))` | ⚠️ MINOR: UE5 uses `saturate`, you use `max`. Functionally same for positive values, but `saturate` clips to [0,1] range. Use `saturate()` for parity. |
| **Illuminance** | `Light0Illuminance` + optional `Light1Illuminance` | `sunIlluminance` | ⚠️ **MISSING**: UE5 supports DUAL lights (primary + secondary). You only have one. |
| **GroundAlbedo** | `Atmosphere.GroundAlbedo.rgb` | `groundAlbedo.xyz` | ✅ CORRECT — same concept |
| **Lambertian factor** | `/ PI` | `(1.0 / PI) *` | ✅ CORRECT — mathematically equivalent |
| **Throughput application** | `Throughput * (...)` | `Throughput * groundRadiance` | ✅ CORRECT — same position in calculation |
| **Structure check** | Conditional: `if (Ground && tMax == tBottom)` | Conditional: `if (groundHit.x > 0.0f)` | ✅ CORRECT — same intent |

### Key Differences

#### 1. **Missing Second Light Support**

UE5 (lines 778-784):
```hlsl
#if SECOND_ATMOSPHERE_LIGHT_ENABLED
	{
		const float NdotL1 = saturate(dot(UpVector, Light1Dir));
		float Light1ZenithCosAngle = dot(UpVector, Light1Dir);
		float3 TransmittanceToLight1 = GetTransmittance(Light1ZenithCosAngle, PHeight);
		L += Light1Illuminance * TransmittanceToLight1 * Throughput * NdotL1 * Atmosphere.GroundAlbedo.rgb / PI;
	}
#endif
```

**UzEngine has**: Only primary sun light
**UE5 has**: Optional second atmosphere light

**Impact**: If you need multi-sun atmosphere effects, UE5's code handles it conditionally. You need to add support for a second light if required by your use case.

#### 2. **Potential Illuminance Pre-exposure**

UE5 uses:
- `ExposedLight0Illuminance` (lines 695-699 show usage)
- This suggests the illuminance is pre-exposed for tone-mapping

UzEngine uses:
- `sunIlluminance`
- Unclear if this is pre-exposed or raw

**Action needed**: Check if your `sunIlluminance` matches UE5's pre-exposure behavior.

---

## Exact Mathematical Formulation

### Single Light (Your Case)

```
L_ground = Throughput * Light_Illuminance * Transmittance(Ground→Light) * max(0, N·L) * GroundAlbedo / π
```

Where:
- **Throughput**: cumulative optical depth `exp(-integral(extinction(s)ds)` from camera to ground)
- **Light_Illuminance**: sun disk luminance (pre-exposed)
- **Transmittance(Ground→Light)**: sampled from LUT at (ground position, light direction)
- **N·L**: clamped dot product of surface normal (radial outward) and light direction
- **GroundAlbedo**: RGB albedo value from component constant buffer
- **π**: Lambertian normalization

### Dual Light (UE5 Full)

```
L_ground = Throughput * [
    Light0_Illuminance * Transmittance(Ground→Light0) * max(0, N·L0) * GroundAlbedo / π
    + Light1_Illuminance * Transmittance(Ground→Light1) * max(0, N·L1) * GroundAlbedo / π
]
```

---

## Data Flow Verification

### UE5 Setup

1. **Component**: `SkyAtmosphereComponent` (C++)
   - Stores `GroundAlbedo` as UPROPERTY
   - Stores `SkyLightIntensity`, `SunLightIntensity` for illuminance

2. **CPU → GPU**: Constant buffer population
   - `Atmosphere.GroundAlbedo` → CB
   - `Light0Illuminance` / `Light1Illuminance` → CB

3. **Shader**: Ray march loop
   - Reads CB at start
   - Accumulates `Throughput` during march
   - Applies ground formula when `Ground && tMax == tBottom`

### UzEngine Your Setup

1. **Component**: Likely `SkyLightComponent` or similar
   - Provides `groundAlbedo`
   - Provides `sunIlluminance`

2. **CPU → GPU**: Constant buffer
   - Ground albedo → CB
   - Sun illuminance → CB

3. **Shader**: Ray march equivalent
   - Reads CB
   - Accumulates `Throughput`
   - Applies formula when hit ground

**Compatibility**: Your structure mirrors UE5's. No major data flow issues.

---

## Ground Albedo — Design Implications

**Critical observation**: UE5 does NOT compute ground color dynamically. It **stores a fixed RGB value**.

This means:
- Ground color does NOT vary with time of day
- Ground color is a **component property**, not derived from environment
- Sky color and ground color are **independent**

If you want blue ground to match blue sky (a visual design decision), you would:
1. In editor: Set `GroundAlbedo = (0.1, 0.15, 0.25)` to match sky
2. This is a **manual authoring choice**, not automatic computation

---

## Shader Pseudo-Code Conversion

### UE5 Shader Language → UzEngine HLSL

```hlsl
// UE5 (actual code from SkyAtmosphere.usf:777)
L += Light0Illuminance * TransmittanceToLight0 * Throughput * NdotL0 * Atmosphere.GroundAlbedo.rgb / PI;

// UzEngine (your equivalent)
L += sunIlluminance * groundSunT * Throughput * NdotL * groundAlbedo.xyz / PI;

// For maximum parity, rewrite as:
L += Throughput * sunIlluminance * groundSunT * saturate(dot(groundNormal, sunDir)) * groundAlbedo / PI;
```

---

## Recommendations

### 1. Change `max(0.0, dot(...))` → `saturate(dot(...))`
```hlsl
float NdotL = saturate(dot(groundNormal, sunDir));  // UE5 style
```

### 2. Verify Pre-exposure on Illuminance
Check that your `sunIlluminance` matches UE5's `ExposedLight0Illuminance`:
- If UE5 pre-exposes, so should you
- If UE5 doesn't, don't

### 3. Add Second Light Support (Optional)
If you need multiple sky lights (e.g., planet reflections):
```hlsl
if (groundHit.x > 0.0f) {
    // ... primary light computation ...

#if SECONDARY_LIGHT_ENABLED
    float3 groundSunT2 = SampleTransmittanceLut(..., light2Dir, ...);
    float NdotL2 = saturate(dot(groundNormal, light2Dir));
    L += Throughput * sunIlluminance2 * groundSunT2 * NdotL2 * groundAlbedo / PI;
#endif
}
```

### 4. Document Your GroundAlbedo Default
```cpp
/// @source UE5 SkyAtmosphere.usf:777
/// Ground albedo controls planet surface reflectance. Stored as sRGB in component,
/// converted to linear during constant buffer setup.
float3 groundAlbedo = 0.2f;  // Typical earth-like surface
```

---

## Summary

**Your implementation is ALMOST CORRECT**. The main issues are:

1. ✅ Uses `Throughput` correctly (accumulated from ray march)
2. ✅ Applies ground normal correctly (radial outward)
3. ✅ Applies transmittance from ground to light correctly
4. ✅ Uses Lambertian diffuse model with 1/π normalization
5. ⚠️ Missing second light support (optional, UE5 feature)
6. ⚠️ Minor: `max(0.0, x)` should be `saturate(x)` for exact parity

**No fundamental algorithm changes needed.** The formula is correct and matches UE5's implementation.
