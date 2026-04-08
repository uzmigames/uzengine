<!-- doc-meta
title: "UE5 Volumetric Fog Implementation Guide for UzEngine"
description: "Quick reference for implementing volumetric fog energy balance in UzEngine"
tags: ["implementation", "fog", "volumetric", "energy-conservation"]
status: "complete"
author: "claude-code"
-->

# UE5 Volumetric Fog Implementation Guide for UzEngine

## Quick Reference: Parameter Mapping

### Height Fog Component Parameters

```cpp
struct HeightFogComponent {
    // Density parameters (exponential falloff with height)
    float fogDensity = 0.02f;              // Global multiplier
    float fogHeightFalloff = 0.2f;         // Falloff rate (controls transition zone size)
    float fogHeightOffset = 0.0f;          // Relative to component position Z
    
    // Maximum opacity (prevents "always opaque" effect)
    float fogMaxOpacity = 1.0f;            // [0, 1] range
    
    // Appearance parameters
    float3 fogInscatteringColor = {0.5, 0.55, 0.7};  // Fog color
    float fogCutoffDistance = VERY_LARGE;  // Beyond this, fog is not applied
    
    // Directional light contribution (sunlight scattering)
    float3 directionalInscatteringColor = {1.0, 0.9, 0.8};
    float directionalInscatteringExponent = 8.0f;
    float directionalInscatteringStartDistance = 1000.0f;
    
    // Sky atmosphere integration
    float3 skyAtmosphereAmbientContributionScale = {1.0, 1.0, 1.0};
};
```

### Volumetric Fog Parameters (THE CRITICAL ANTI-DARKENING CONTROLS)

```cpp
struct VolumetricFogParameters {
    // *** ENERGY BALANCE PARAMETERS ***
    
    // Single-scattering albedo: fraction of light that scatters vs. absorbs
    // DEFAULT = (1.0, 1.0, 1.0) = NO ABSORPTION, ONLY SCATTERING
    // < 1.0 = some light is absorbed (darker fog)
    float3 albedo = {1.0, 1.0, 1.0};
    
    // Light emitted by fog density
    // DEFAULT = (0, 0, 0) = NO EMISSION (fog doesn't glow)
    // When PROJECT_EXPFOG_MATCHES_VFOG is true, this is MULTIPLIED BY DENSITY
    // This is the key anti-darkening compensation
    float3 emissive = {0.0, 0.0, 0.0};
    
    // Scale factor for extinction (light absorption)
    // Higher = more light absorbed = darker, more opaque fog
    // But paired with density, not albedo
    float extinctionScale = 1.0f;           // [0.1, 10.0]
    
    // *** VISUAL PARAMETERS ***
    
    // Henyey-Greenstein phase function parameter
    // Controls scattering direction, NOT brightness
    // 0.0 = isotropic (scatters equally in all directions)
    // > 0 = forward scattering (like fog illuminated by sun)
    // < 0 = backward scattering
    float scatteringDistribution = 0.0f;    // [-0.9, 0.9]
    
    // *** DISTANCE CONTROLS ***
    
    // Volumetric fog rendering distance
    float viewDistance = 5000.0f;           // [1000, 10000]
    float startDistance = 0.0f;             // Where to start fog
    float nearFadeInDistance = 0.0f;        // Smooth fade-in near camera
};
```

---

## Energy Balance: The Math

### The Four-Step Pipeline

```
Step 1: Compute Density
    density = FogDensity × exp2(-FogHeightFalloff × (worldZ - heightOffset))

Step 2: Compute Extinction and Scattering
    extinction = density × ExtinctionScale              // Light absorbed per unit distance
    scattering = albedo × extinction                    // Light scattered back
    
    KEY: scattering ≤ extinction (because albedo ≤ 1.0)
    When albedo = 1.0: scattering = extinction (no net absorption)
    When albedo < 1.0: scattering < extinction (some light lost)

Step 3: Accumulate Light from Sources
    For each light:
        light_contribution = lightColor × phase(direction) × shadowFactor
        totalScattering += light_contribution × scattering
    
    Add emissive:
        if (PROJECT_EXPFOG_MATCHES_VFOG):
            totalEmissive += emissive × density          // Scale by density!
        else:
            totalEmissive += emissive                    // Constant

Step 4: Integrate Over Ray
    transmittance = exp2(-extinction × distance)        // [0, 1]
    inScatteredLight = integrate(scattering + emissive + lightContributions)
    
    FinalColor = inScatteredLight + sceneColor × transmittance
    FinalAlpha = transmittance
```

### The Anti-Darkening Magic Line

```glsl
// When extinction is high (fog is dense):
transmittance = exp2(-highExtinction × distance) ≈ 0  // Scene becomes invisible

// But we add back light:
inScatteredLight = highScattering × lights + highEmissive × density
                 ≈ highAlbedo × highExtinction × lights + highEmissive × density

// If albedo = 1.0 AND emissive ∝ density:
inScatteredLight ≈ highExtinction × lights + (highEmissive × highDensity)

// Net result: Scene color is mostly replaced by fog light
// But fog light is bright (from illuminated scattering + compensating emissive)
// So: OPAQUE BUT NOT DARK
```

---

## Implementation Checklist

### Phase 1: Height Fog Component

- [ ] Create `HeightFogComponent` structure with 15 parameters
- [ ] Add to scene environment gathering
- [ ] CPU-side: compute `CollapsedFogParameter` (pre-computed density at camera height)
- [ ] Store in constant buffer: `FogStruct` with ExponentialFogParameters[0-3]

**Files to create/modify**:
- `engine/renderer/components/height_fog_component.h`
- `engine/renderer/fog_constants.cpp` (equivalent to `InitFogConstants()`)
- Constant buffer definition (add to existing fog CB)

### Phase 2: Analytical Height Fog Shader

- [ ] Implement `GetExponentialHeightFog()` function in shader
- [ ] Compute `ExponentialHeightLineIntegral` using Crank-Nicolson integration
- [ ] Apply directional inscattering (sunlight cone)
- [ ] Apply sky atmosphere ambient contribution
- [ ] Blend height fog color with scene

**Files to create/modify**:
- `runtime/shaders/fog/height_fog_common.hlsli`
- Integrate into basepass pixel shader

### Phase 3: Volumetric Fog Grid Setup

- [ ] Create 3D texture for scattering accumulation (e.g., 128×128×64)
- [ ] Create 3D texture for emissive accumulation
- [ ] Compute shader: material setup (density + albedo + extinction)
- [ ] Store in `RWVBufferA` (scattering, extinction)
- [ ] Store in `RWVBufferB` (emissive)

**Key: Line 146 of VolumetricFog.usf**
```hlsl
float3 scattering = albedo * extinction;
```

- [ ] Add emissive compensation (line 244-248 of VolumetricFog.usf)

### Phase 4: Light Injection

- [ ] For each light: compute contribution to fog volume
- [ ] Use Henyey-Greenstein phase function
- [ ] Apply shadows (if available; otherwise default to 1.0)
- [ ] Accumulate per-light scattering

### Phase 5: Integration

- [ ] Implement `FinalIntegrationCS` to integrate accumulated scattering along ray
- [ ] Apply temporal reprojection (blend with previous frame at 0.9 weight)
- [ ] Compute transmittance per voxel

### Phase 6: Composition

- [ ] In pixel shader, look up volumetric fog at scene depth
- [ ] Apply formula (line 469 of HeightFogCommon.ush):
  ```hlsl
  return float4(
      VolumetricFogLookup.rgb + GlobalFog.rgb * VolumetricFogLookup.a,
      VolumetricFogLookup.a * GlobalFog.a
  );
  ```

---

## Critical Default Values (Do Not Change)

| Parameter | Default | Why |
|-----------|---------|-----|
| `FogDensity` | 0.02f | Prevents "always overcast" appearance |
| `FogHeightFalloff` | 0.2f | Controls visible transition zone |
| `VolumetricFogAlbedo` | (1, 1, 1) | Full scattering; no light absorption |
| `VolumetricFogEmissive` | (0, 0, 0) | Disabled by default (artist-tuned) |
| `DirectionalInscatteringStartDistance` | 1000 | Limits sun contribution distance |
| `MatchHeightFogFactor` | 0.5f | Voxel material setup (line 142) |

---

## Tuning Parameters for Artists

### Scene Too Dark?

1. **Reduce extinction**:
   ```cpp
   extincitionScale = 0.5f;  // was 1.0f
   ```
   Effect: Fog is more transparent, scene visible longer

2. **Increase emissive**:
   ```cpp
   emissive = {0.1, 0.1, 0.1};  // was {0, 0, 0}
   ```
   Effect: Fog glows, compensates for darkness

3. **Reduce fog density**:
   ```cpp
   fogDensity = 0.01f;  // was 0.02f
   ```
   Effect: Fog is thinner overall

### Fog Not Visible?

1. **Increase extinction**:
   ```cpp
   extinctionScale = 2.0f;  // was 1.0f
   ```
   Effect: Fog becomes more opaque

2. **Increase fog density**:
   ```cpp
   fogDensity = 0.05f;  // was 0.02f
   ```
   Effect: Fog is denser, more visible

3. **Ensure lights are present and bright**

---

## Algorithm: Material Setup (Density Computation)

**Input**: World position of voxel
**Output**: Scattering and Extinction per voxel

```hlsl
// Equivalent to VolumetricFog.usf:MaterialSetupCS (lines 123-252)

float ComputeFogDensityAtPosition(float3 worldPos, HeightFogData fog) {
    // Exponential height fog: density decreases as altitude increases
    float heightAboveSeaLevel = worldPos.z - fog.heightOffset;
    
    // First fog layer
    float density1 = fog.fogDensity 
                   * exp2(-fog.fogHeightFalloff * heightAboveSeaLevel);
    
    // Second fog layer (optional secondary fog)
    float density2 = fog.secondFogDensity 
                   * exp2(-fog.secondFogHeightFalloff 
                           * (heightAboveSeaLevel - fog.secondFogHeightOffset));
    
    // Combined density
    float globalDensity = density1 + density2;
    
    // Match height fog factor (empirical scaling for voxel representation)
    globalDensity *= 0.5f;  // UE5 magic constant from line 142
    
    return globalDensity;
}

void MaterialSetupComputeShader(uint3 gridCoord, out float3 scattering, out float extinction) {
    float3 worldPos = ComputeWorldPositionFromGridCoord(gridCoord);
    
    float density = ComputeFogDensityAtPosition(worldPos, fogData);
    
    // *** THE CRITICAL ANTI-DARKENING SPLIT ***
    float extinction = max(density * extinctionScale, 0.0f);
    float3 scattering = albedo * extinction;
    
    // Emissive compensation (optional)
    float3 emissiveToAdd = emissive;
    #if PROJECT_EXPFOG_MATCHES_VFOG
        emissiveToAdd *= density;  // Scale by density for energy compensation
    #endif
    
    // Output: Store scattering and extinction
    // Note: actual UE5 stores as float4(scattering.rgb, extinction)
    RWVBufferA[gridCoord] = float4(scattering, extinction);
    RWVBufferB[gridCoord] = float4(emissiveToAdd, 0.0f);
}
```

---

## Algorithm: Light Injection

**For each light in scene**:

```glsl
// Equivalent to VolumetricFog.usf:InjectShadowedLocalLightCommon (lines 365-493)

for (uint lightIndex = 0; lightIndex < numLights; ++lightIndex) {
    LightData light = GetLight(lightIndex);
    
    for (uint3 gridCoord : allVoxels) {
        float3 worldPos = ComputeWorldPositionFromGridCoord(gridCoord);
        float3 toLight = light.position - worldPos;
        float distToLight = length(toLight);
        float3 lightDir = toLight / distToLight;
        
        // Light attenuation (inverse square law or custom falloff)
        float attenuation = ComputeLightAttenuation(distToLight, light);
        
        // Directional distance from camera
        float3 cameraDir = normalize(worldPos - cameraPos);
        
        // Phase function: Henyey-Greenstein
        // Controls forward/backward/isotropic scattering
        float phase = HenyeyGreenstein(phaseG, dot(lightDir, -cameraDir));
        
        // Shadows (if available; default to 1.0 if no shadow system)
        float shadowFactor = ComputeVolumeShadow(worldPos, lightDir);
        
        // Light contribution to scattering
        float3 lightContribution = light.color 
                                * attenuation 
                                * phase 
                                * shadowFactor;
        
        // Add to accumulated scattering
        RWLightScattering[gridCoord].rgb += lightContribution;
    }
}
```

---

## Algorithm: Composition (Final Blending)

**In pixel shader**:

```glsl
// Equivalent to HeightFogCommon.ush:CombineVolumetricFog (lines 420-470)

float4 ApplyFog(float3 sceneColor, float3 worldPos) {
    // Get analytical height fog
    float4 heightFog = ComputeExponentialHeightFog(worldPos);
    // Returns: float4(inscatteringColor, transmittance)
    
    // Get volumetric fog at pixel
    float3 volumeUV = ComputeVolumeUV(worldPos, view.WorldToClip);
    float4 volumetricFog = Sample3D(integratedLightScattering, volumeUV);
    // Returns: float4(scattering + emissive, transmittance)
    
    // *** CRITICAL BLENDING FORMULA ***
    // Volumetric fog is in front (closer to camera)
    // Height fog is behind (farther away)
    // Transmittance controls how much of behind is visible through front
    
    float3 finalColor = volumetricFog.rgb 
                      + heightFog.rgb * volumetricFog.a;
                      //                 ^^^^^^^^^^
                      //                 Transmittance of vfog
                      //                 controls height fog visibility
    
    float finalAlpha = volumetricFog.a * heightFog.a;
    //                 ^^^^^^^^^^         ^^^^^^^^^^^
    //                 VFog transmittance × Height fog transmittance
    //                 More opaque if either is opaque
    
    // Blend with scene
    float3 outColor = lerp(finalColor, sceneColor, finalAlpha);
    
    return float4(outColor, 1.0);
}
```

---

## Henyey-Greenstein Phase Function

```glsl
// Controls how light scatters in fog
// g = phase distribution parameter

float HenyeyGreenstein(float g, float cosTheta) {
    // g ∈ [-0.9, 0.9]
    // g = 0.0: isotropic (scatter equally in all directions)
    // g > 0.0: forward scattering (like sunlit fog)
    // g < 0.0: backward scattering
    
    float g2 = g * g;
    float numerator = 1.0 - g2;
    float denominator = 4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    
    return numerator / denominator;
}
```

Default: `g = 0.0` → isotropic phase function

---

## Common Mistakes to Avoid

### ❌ WRONG: Albedo < Scattering

```cpp
// DON'T DO THIS
extinction = 1.0f;
albedo = 0.5f;
scattering = 2.0f * extinction;  // WRONG! Scattering > Extinction violates energy conservation
```

### ✓ CORRECT: Scattering ≤ Extinction

```cpp
extinction = 1.0f;
albedo = 0.5f;
scattering = albedo * extinction;  // CORRECT: 0.5 * 1.0 = 0.5 ≤ 1.0
```

### ❌ WRONG: Emissive Without Density Scaling

```cpp
// DON'T DO THIS (unless desired)
emissive = {0.5, 0.5, 0.5};  // Constant, doesn't scale with density
// Result: Fog is equally bright in dense and thin areas (weird)
```

### ✓ CORRECT: Emissive Scales with Density

```cpp
// DO THIS (matches Hillaire/UE5)
emissive = {0.1, 0.1, 0.1};
// In shader: emissiveToAdd = emissive * density;
// Result: Fog brightness compensates for extinction
```

### ❌ WRONG: Composition Order

```glsl
// DON'T DO THIS
finalColor = heightFog.rgb + volumetricFog.rgb;  // Both fully visible?
```

### ✓ CORRECT: Transmittance-Based Composition

```glsl
// DO THIS
finalColor = volumetricFog.rgb 
           + heightFog.rgb * volumetricFog.a;  // Height fog only visible through transparent vfog
```

---

## Testing Checklist

- [ ] Scene with fog: Not dark at default settings (FogDensity = 0.02)
- [ ] Scene with fog: Still dark when artist increases density to 0.05
- [ ] Volumetric fog enabled: Adds visual depth, doesn't obscure scene
- [ ] Volumetric fog + bright light: Fog is illuminated (scattering visible)
- [ ] Volumetric fog + dim light: Fog is dark but not *black* (still visible)
- [ ] Emissive compensation: Increasing emissive brightens fog without making it invisible
- [ ] Phase function: Changing distribution doesn't change brightness (only direction)

---

## Performance Notes

- **Grid resolution**: 128×128×64 is reasonable for 1440p. Halve for lower quality.
- **Temporal reprojection**: Blend with previous frame at 0.9 weight (reduces jitter, hides under-sampling)
- **Light injection**: Only inject lights with significant impact (culling helps)
- **Integration**: Integrate along view ray (can be accumulated in 2D pass or 3D pass)

---

## Reference Implementation Locations in UE5

| Component | UE5 Location | Lines | Purpose |
|-----------|--------------|-------|---------|
| Density computation | VolumetricFog.usf | 135-137 | Exponential falloff |
| Albedo/Extinction split | VolumetricFog.usf | 146 | `scattering = albedo * extinction` |
| Emissive compensation | VolumetricFog.usf | 244-248 | Scale by density |
| Light injection | VolumetricFog.usf | 365-493 | Per-light scattering |
| Composition | HeightFogCommon.ush | 469 | `VFog + HeightFog * VFog.alpha` |
| Blending formula | HeightFogCommon.ush | 375 | `(InscatteringColor) * (1 - ExpFogFactor) + DirectionalInscattering` |
| Directional inscattering | HeightFogCommon.ush | 304-359 | Sunlight in fog |
| Sky ambient | HeightFogCommon.ush | 174-180 | Sky light contribution |

---

## Summary: The Three Laws of Fog Energy Balance

1. **Albedo ≤ 1.0**: Never let scattering exceed extinction
2. **Emissive ∝ Density**: Compensate for extinction with density-scaled light
3. **Transmittance Composition**: Use alpha to blend, never add colors blindly

Violating any of these causes either over-brightening (unrealistic) or over-darkening (the original problem).
