<!-- doc-meta
title: UE5 Height Fog Visual Breakdown — Layer by Layer
created: 2026-03-19
category: rendering
tags: fog-rendering, visual-breakdown, sky-atmosphere
-->

# UE5 Height Fog Visual Breakdown — Layer by Layer

This document shows **visually** how UE5 composes the final fog color from multiple layers.

---

## Visual Composition Diagram

```
Final Fog Color = Inscattering Color × (1 - FogDensity) + Directional Inscattering
                  └─────────────────────────────────────┬──────────────────────────┘
                                                        └→ Output to screen


Inscattering Color = Base Fog Color + Sky Ambient Contribution + Cubemap Contribution
                     ├─────────────┬──────────────────┬──────────────────┤
                     │             │                  │                  │
                     │             └─ Sky Ambient:    └─ Cubemap:        │
                     │                = SkyAmbient...  = (only if        │
                     │                ColorScale.rgb   enabled)          │
                     │                × HeightFog                        │
                     │                Contribution                       │
                     │                × GetView                          │
                     │                DistanceSky                        │
                     │                LightColor()                       │
                     │                                                   │
                     └─ Base:                                           │
                        = ExponentialFogColorParameter.rgb              │
                           (from fog component)                         │


Directional Inscattering = DirectionalInscatteringColor × (1 - FogTransmittance)
                           ├────────────────────────────┤
                           │
                           └─ DirectionalInscatteringColor
                              = FogData.DirectionalInscatteringColor.xyz
                              + HeightFogContribution × AtmosphereLightIlluminanceOnGroundPostTransmittance[0].rgb
                                                         └─ This is where the blue/orange comes from!
```

---

## Layer 1: Base Fog Color

### What It Is
The base fog color from the ExponentialHeightFog component.

```hlsl
Inscattering = FogData.ExponentialFogColorParameter.xyz  // Line 142, HeightFogCommon.ush
```

### Example Values
- Clear day: (0.5, 0.6, 0.8) — light blue-gray
- Sunny: (0.7, 0.75, 0.85) — brighter blue
- Overcast: (0.6, 0.65, 0.7) — muted grayish

### Visual Effect
Provides the **base tone** of the fog. Without this, the fog would be pure black.

---

## Layer 2: Sky Ambient Contribution

### What It Is
The sky light's ambient color, modulated by fog component settings.

```hlsl
// HeightFogCommon.ush:175-179
#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG
    float3 SkyAmbient = FogData.SkyAtmosphereAmbientContributionColorScale.rgb
                      * View.SkyAtmosphereHeightFogContribution.xxx
                      * GetViewDistanceSkyLightColor();
    #if PROJECT_EXPFOG_MATCHES_VFOG
    SkyAmbient *= FogData.VolumetricFogAlbedo;
    #endif
    Inscattering += SkyAmbient;
#endif
```

### Three Multipliers

1. **`SkyAtmosphereAmbientContributionColorScale`** (from fog component)
   - Typical value: (1, 1, 1) — white, full contribution
   - Can be tinted, e.g., (1, 0.8, 0.6) for warm bias
   - Default: white (no tint)

2. **`View.SkyAtmosphereHeightFogContribution`** (from sky component)
   - Range: 0.0 to 1.0
   - 0.0 = no sky atmosphere effect (disabled)
   - 1.0 = full effect (default)
   - Used as master blend factor

3. **`GetViewDistanceSkyLightColor()`** (from sky light component)
   - The actual ambient color being emitted by the sky
   - Typical at zenith: (0.2, 0.3, 0.5) — blue
   - Typical at horizon: (1.0, 0.8, 0.4) — orange

### Computation Example
```
SkyAmbient = (1, 1, 1)           [SkyAtmosphereAmbientContributionColorScale]
           × 1.0                 [SkyAtmosphereHeightFogContribution]
           × (0.3, 0.4, 0.6)     [Sky light color at current view direction]
           = (0.3, 0.4, 0.6)
```

Then added to base:
```
Inscattering = (0.5, 0.6, 0.8)   [Base fog color]
             + (0.3, 0.4, 0.6)   [Sky ambient]
             = (0.8, 1.0, 1.4)   [Clamped to valid range]
```

### Visual Effect
- **Increases fog brightness** toward the horizon
- **Adds the sky's hue** to the fog (blue overhead, warm at horizon)
- **Strongest effect** when looking toward the sky

---

## Layer 3: Directional Light (Sun) Contribution

### What It Is
The sun's color **after being filtered by the atmosphere**.

```hlsl
// HeightFogCommon.ush:319
DirectionalInscatteringColor = FogData.DirectionalInscatteringColor.xyz
                             + InView.SkyAtmosphereHeightFogContribution
                             * InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[0].rgb;
```

### Where It Comes From

```cpp
// DirectionalLightComponent.cpp:605
GetSunIlluminanceOnGroundPostTransmittance()
    = GetOuterSpaceIlluminance()              // Sun color in space (white)
    × GetAtmosphereTransmittanceTowardSun()   // Atmospheric filtering
```

### Transmittance Examples
**At zenith (sun overhead)**:
```
Sun color: (1.0, 1.0, 1.0)           [white, from DirectionalLight]
Transmittance: (0.95, 0.93, 0.92)    [atmosphere has minimal effect]
Result: (0.95, 0.93, 0.92)           [nearly white, sun looks bright]
```

**At 45° elevation**:
```
Sun color: (1.0, 1.0, 1.0)
Transmittance: (0.85, 0.75, 0.55)    [more blue scattered, red depleted]
Result: (0.85, 0.75, 0.55)           [warm peachy color]
```

**At horizon (sun near horizon)**:
```
Sun color: (1.0, 1.0, 1.0)
Transmittance: (0.6, 0.4, 0.1)       [heavy blue scattering, deep red]
Result: (0.6, 0.4, 0.1)              [deep orange/red]
```

### Applied to Directional Inscattering

```hlsl
// After computing DirectionalInscatteringColor with atmosphere:
DirectionalLightInscattering = DirectionalInscatteringColor
                             × pow(saturate(dot(ViewDir, SunDir)), Exponent)
                             × PhaseFunction
                             × (1 - FogTransmittance)
```

This creates a **bright glow around the sun**, colored by atmospheric scattering:
- **Zenith sun**: bright white glow
- **Horizon sun**: bright orange/red glow

### Visual Effect
- **Creates the sun's visible glow** in the fog
- **Adds color shift** based on atmospheric filtering
- **Strongest effect** when looking toward the sun (but not directly at it)

---

## Layer 4: Fog Density (Transmittance Factor)

### What It Is
How much existing light is blocked by fog between camera and object.

```hlsl
// HeightFogCommon.ush:362
half ExpFogFactor = max(saturate(exp2(-ExponentialHeightLineIntegral)), MinFogOpacity);
```

### Computation
```
ExponentialHeightLineIntegral = ∫ density(z) dz along ray
ExpFogFactor = exp2(-ExponentialHeightLineIntegral)
             = how much light makes it through (0 = blocked, 1 = transparent)
```

### Used in Final Blend

```hlsl
FogColor = Inscattering × (1 - ExpFogFactor) + DirectionalInscattering
           └─ Additive blending: fog color opaqueness based on density
```

**Example**:
- `ExpFogFactor = 0.3` (70% opaque fog)
- `FogColor = Inscattering × 0.7 + DirectionalInscattering`
- Object behind fog is 70% obscured, 30% visible through

---

## Complete Example: Sunrise Scenario

### Setup
- Sun at 15° elevation (low morning sun)
- Sky light emitting warm colors
- Fog density moderate (50% opaque)

### Calculation

**1. Base fog color** (component property):
```
Base = (0.6, 0.65, 0.7)
```

**2. Sky ambient contribution**:
```
SkyAmbient = (1, 1, 1) × 1.0 × (0.8, 0.6, 0.3)
           = (0.8, 0.6, 0.3)  [warm sky color]
Inscattering = (0.6, 0.65, 0.7) + (0.8, 0.6, 0.3)
             = (1.4, 1.25, 1.0)  [bounded to valid range]
             ≈ (1.0, 1.0, 1.0) [actually bright white]
```

**3. Directional light** (sun at 15° elevation):
```
Sun color = (1, 1, 1)
Transmittance at 15° = (0.75, 0.55, 0.25)  [significant blue/green scattering]
AtmosphereLightIlluminanceOnGroundPostTransmittance[0] = (0.75, 0.55, 0.25)

DirectionalInscatteringColor = (0.3, 0.3, 0.3)  [base from fog component]
                             + 1.0 × (0.75, 0.55, 0.25)  [atmosphere light]
                             = (1.05, 0.85, 0.55)
                             ≈ (1.0, 0.85, 0.55)

DirectionalInscattering = (1.0, 0.85, 0.55) × pow(0.5, 2) × 1/(4π) × (1 - 0.5)
                        = (1.0, 0.85, 0.55) × 0.25 × 0.0796 × 0.5
                        ≈ (0.01, 0.0085, 0.0055)  [subtle sun glow, warm]
```

**4. Final fog color**:
```
FogColor = (1.0, 1.0, 1.0) × (1 - 0.5) + (0.01, 0.0085, 0.0055)
         = (0.5, 0.5, 0.5) + (0.01, 0.0085, 0.0055)
         = (0.51, 0.51, 0.51)
```

**Visual result**: A bright, warm white fog with a subtle orange glow toward the sun direction.

---

## Control Parameters Summary

### From Fog Component (`AExponentialHeightFog`)
| Parameter | Controls | Default |
|-----------|----------|---------|
| `SkyAtmosphereAmbientContributionColorScale` | Tint/intensity of sky ambient | (1, 1, 1, 1) white |
| `ExponentialFogColor` | Base fog color | Component property |
| `DirectionalInscatteringColor` | Base sun glow (without atmosphere) | (0.3, 0.3, 0.3) |
| `FogDensity` | How much density at height | Component property |

### From Sky Component (`USkyAtmosphereComponent`)
| Parameter | Controls | Default |
|-----------|----------|---------|
| `HeightFogContribution` | Master blend factor (0-1) | 1.0f |

### Computed (Not User-Settable)
| Parameter | Controls | Source |
|-----------|----------|--------|
| `AtmosphereLightIlluminanceOnGroundPostTransmittance` | Sun color × atmospheric filtering | Atmosphere LUT sample |
| `GetViewDistanceSkyLightColor()` | Sky light color at current view dir | Sky light component |

---

## Visual Progression

### Zenith (Sun Overhead)
```
Inscattering = Base + Sky Ambient (blue)
DirectionalInscattering = Subtle white glow
Result: Blue fog with bright white sun
```

### Horizon (Sun at Edge of World)
```
Inscattering = Base + Sky Ambient (orange/warm)
DirectionalInscattering = Intense orange/red glow
Result: Orange fog with warm sun
```

### Behind Camera (No Sun Contribution)
```
Inscattering = Base + Sky Ambient (blue from overhead)
DirectionalInscattering = None (sun facing away)
Result: Blue-gray fog, no glow
```

---

## Important Notes for Implementation

1. **No Approximations**: All numbers are computed exactly, not hardcoded
2. **Transmittance is Sampled**: Not derived from a formula, sampled from atmosphere LUT
3. **Linear Space**: All colors are in linear RGB, not sRGB
4. **Master Control**: `SkyAtmosphereHeightFogContribution` multiplies BOTH ambient and directional effects
5. **Additive Blending**: Sky contributions are added, not multiplied (except by the master factor)
6. **No Compensation Factors**: No `× 2.0` or `× 0.5` multipliers to "match" another engine

---

## Debugging Visual Issues

**If fog looks wrong**:
1. Check `HeightFogContribution` — is it 0? (would disable all effects)
2. Check `SkyAtmosphereAmbientContributionColorScale` — is it black? (would disable ambient)
3. Check transmittance LUT sample — is it returning valid values?
4. Compare `AtmosphereLightIlluminanceOnGroundPostTransmittance` to actual sun angle
5. Verify fog density is reasonable (not fully opaque)

**If fog color is wrong**:
1. Check sky light component's emitted color
2. Check if atmosphere light color is being computed
3. Check if both ambient and directional paths are enabled (`PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG`)

