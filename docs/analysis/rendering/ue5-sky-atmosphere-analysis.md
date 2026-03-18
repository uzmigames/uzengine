# UE5 Sky Atmosphere Single-Scatter Implementation Analysis

## Executive Summary

Your black sky output is due to **missing analytical integration of the scatter integral**. UE5 doesn't compute totalRayleigh as a simple accumulation; instead it uses the **Frostbite analytical formula** to integrate scattered light with transmittance over each ray march step. This prevents the ~1e-6 collapse you're seeing.

Key issue: You're accumulating raw `totalRayleigh * betaR` directly, but UE5 analytically integrates scattered luminance to avoid numerical underflow.

---

## UE5 Default Atmosphere Parameters

From `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\SkyAtmosphereComponent.cpp` lines 93-125:

```cpp
// Default Earth-like atmosphere
const float EarthBottomRadius = 6360.0f;      // km
const float EarthTopRadius = 6420.0f;         // km
const float EarthRayleighScaleHeight = 8.0f;  // km
const float EarthMieScaleHeight = 1.2f;       // km

// Raw Rayleigh scattering coefficients (1/km)
const FLinearColor RayleightScatteringRaw = FLinearColor(0.005802f, 0.013558f, 0.033100f);

// Stored as normalized RGB + scale
RayleighScattering = RayleightScatteringRaw * (1.0f / RayleightScatteringRaw.B);  // Normalize by B channel
RayleighScatteringScale = RayleightScatteringRaw.B;  // = 0.033100

// Mie coefficients
MieScattering = FColor::White;       // (1, 1, 1) then scaled
MieScatteringScale = 0.003996f;      // 1/km
MieAbsorption = FColor::White;
MieAbsorptionScale = 0.000444f;      // 1/km

// Ozone absorption (tent distribution peak at 25km)
const FLinearColor OtherAbsorptionRaw = FLinearColor(0.000650f, 0.001881f, 0.000085f);
OtherAbsorptionScale = OtherAbsorptionRaw.G;  // = 0.001881
```

**Critical observation**: Rayleigh scattering is stored with the SMALLEST component (B=0.033100) as the scale factor, meaning normalized RGB values are (0.1753, 0.4098, 1.0).

---

## Single-Scatter Ray March Algorithm (Frostbite Analytical Integration)

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf` lines 399-764

### 1. Loop Setup (lines 543-567)

```hlsl
float3 L = 0.0f;  // Accumulated luminance output
float3 Throughput = 1.0f;  // Transmittance from camera to current sample
float3 OpticalDepth = 0.0f;
float t = 0.0f;  // Ray march distance in km

// Pre-expose illuminance ONCE (line 554)
float3 ExposedLight0Illuminance = Light0Illuminance * OutputPreExposure;

for (float SampleI = 0.0f; SampleI < SampleCount; SampleI += 1.0f)
{
    // Compute sample point along ray
    // ...
```

### 2. Sample Medium (line 600)

```hlsl
MediumSampleRGB Medium = SampleAtmosphereMediumRGB(P);
```

`MediumSampleRGB` struct contains (from `SkyAtmosphereCommon.ush` lines 286-305):

```hlsl
struct MediumSampleRGB
{
    float3 Scattering;         // Total scattering coefficient (1/km)
    float3 Extinction;         // Total extinction = scattering + absorption (1/km)

    float3 ScatteringMie;      // Mie scattering (1/km)
    float3 ScatteringRay;      // Rayleigh scattering (1/km)
    float3 ScatteringOzo;      // Ozone scattering (negligible, usually 0)

    float3 ExtinctionMie;      // Mie extinction (1/km)
    float3 ExtinctionRay;      // Rayleigh extinction (1/km)
    float3 ExtinctionOzo;      // Ozone extinction (1/km)
};
```

**Sampling** (lines 308-340):

```hlsl
MediumSampleRGB SampleAtmosphereMediumRGB(in float3 WorldPos)
{
    const float SampleHeight = max(0.0, (length(WorldPos) - Atmosphere.BottomRadiusKm));

    // Exponential density falloff
    const float DensityMie = exp(Atmosphere.MieDensityExpScale * SampleHeight);
    const float DensityRay = exp(Atmosphere.RayleighDensityExpScale * SampleHeight);

    // Multiply coefficients by density
    s.ScatteringRay = DensityRay * Atmosphere.RayleighScattering.rgb;
    s.ExtinctionRay = s.ScatteringRay;  // No absorption for Rayleigh

    s.ScatteringMie = DensityMie * Atmosphere.MieScattering.rgb;
    s.ExtinctionMie = DensityMie * Atmosphere.MieExtinction.rgb;  // Can differ if absorption > 0

    return s;
}
```

### 3. Transmittance Along Step (lines 601-602)

```hlsl
const float3 SampleOpticalDepth = Medium.Extinction * dt * AerialPespectiveViewDistanceScale;
const float3 SampleTransmittance = exp(-SampleOpticalDepth);
```

This is the **transmittance for the current segment** of thickness `dt`.

### 4. **Crucial**: Analytical Scatter Integral (lines 697-761)

This is what you're missing. Instead of:
```hlsl
L += Throughput * ScatteringCoeff * dt;
```

UE5 uses the **Frostbite analytical integration**:

```hlsl
// Phase * Scattering term computed earlier (line 697)
float3 S = ExposedLight0Illuminance * (PlanetShadow0 * TransmittanceToLight0 * PhaseTimesScattering0);

// Analytic integration of scattered luminance over segment [t0, t1]
// From slide 28: http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
float3 Sint = (S - S * SampleTransmittance) / SafeMediumExtinction;

// Accumulate with pre-computed transmittance from previous steps
L += Throughput * Sint;  // Line 757

// Step transmittance for next iteration
Throughput *= SampleTransmittance;  // Line 760
```

**Why this matters**:

The analytical formula `Sint = (S - S * T) / Extinction` integrates:
$$\int_0^{dt} e^{-\sigma \cdot x} \cdot S \, dx = \frac{S(1 - e^{-\sigma \cdot dt})}{\sigma}$$

This avoids summing tiny contributions. If dt=60km/16=3.75km and extinction=0.033:
- Naive: 16 × (0.033 × 3.75 × 20 × phase) ≈ 16 × 0.2475 = **3.96**
- Analytical: Properly integrates transmittance decay, yielding higher values

### 5. Variable Sample Count (lines 522-527)

UE5 adapts sample count based on ray distance:

```hlsl
if (Sampling.VariableSampleCount)
{
    SampleCount = lerp(Sampling.MinSampleCount, Sampling.MaxSampleCount,
                       saturate(tMax * Sampling.DistanceToSampleCountMaxInv));
    // Non-linear distribution with quadratic falloff
    t0 = t0 * t0;
    t1 = t1 * t1;
}
```

This concentrates more samples near the camera where density is higher.

### 6. Final Output (lines 788-792)

```hlsl
Result.L = L;  // Single-scattered luminance
Result.Transmittance = Throughput * PlanetOnOpaque;
return Result;
```

The pixel shader then applies pre-exposure inversion and blends with clouds/aerial perspective.

---

## Key Differences from Your Implementation

| Aspect | Your Approach | UE5 Approach |
|--------|---------------|-------------|
| **Integration** | Direct accumulation: `L += sun * beta * phase * dt` | Analytical: `L += sun * ((beta*phase - beta*phase*T) / beta)` |
| **Transmittance** | May not apply Throughput properly between steps | Explicit `Throughput *= SampleTransmittance` with pre-exposure handling |
| **Pre-exposure** | Sun illuminance units unclear | `ExposedLight0Illuminance = Light0Illuminance * OutputPreExposure` applied once before loop |
| **Density sampling** | Coefficients may not account for height falloff | `SampleAtmosphereMediumRGB()` applies exponential density: `density = exp(scale * height)` |
| **Ozone absorption** | Possibly ignored | Included in extinction, reduces scattered light at high altitudes |
| **Scale height** | May use incorrect values | RayleighScaleHeight=8km, MieScaleHeight=1.2km (UE5 defaults) |

---

## The Black Sky Root Cause

When you compute:
```hlsl
sky = sunIlluminance * (totalRayleigh * betaR * rayleighPhase)
```

With:
- sunIlluminance = {20, 20, 20}
- betaR = {5.802e-3, 13.558e-3, 33.1e-3} km⁻¹
- rayleighPhase ≈ 0.3 (for 90° angle)
- 16 steps over 60km → step size ≈ 3.75km
- Average density ≈ exp(-h/8) at mid-path ≈ 0.65

Your accumulation:
```
totalRayleigh = sum(16) of (exp(-h/8) * 3.75) ≈ 0.65 * 3.75 * 0.5 ≈ 1.22 km
L = 20 * 1.22 * 0.033 * 0.3 ≈ 0.24 (still very dim)
```

But you're **missing the transmittance decay inside the loop**. Each step should contribute less as light from that point must travel through remaining atmosphere:

```hlsl
// What UE5 does:
L += Throughput * (Sun * (1 - exp(-sigma*dt)) / sigma);
Throughput *= exp(-sigma*dt);  // Now Throughput decays!
```

This causes early steps to contribute more (high Throughput) while late steps are attenuated.

---

## Exact UE5 Formula

**Single-scattered luminance at pixel:**

$$L = \sum_{i=0}^{N-1} T_i \cdot \int_0^{dt_i} e^{-\sigma(s) \cdot x} \cdot L_{\text{sun}} \cdot \sigma_s(s+x) \cdot \rho(\theta) \, dx$$

Where:
- $T_i$ = transmittance from camera to sample point i
- $\sigma(s)$ = extinction coefficient at sample height
- $L_{\text{sun}}$ = sun illuminance (pre-exposed)
- $\sigma_s$ = scattering coefficient (Rayleigh or Mie)
- $\rho(\theta)$ = phase function (Rayleigh or Henyey-Greenstein)

**Analytical form** (inside loop, lines 754):

$$\text{Sint} = \frac{S - S \cdot e^{-\sigma \cdot dt}}{\sigma}$$

Where $S = L_{\text{sun}} \cdot \sigma_s \cdot \rho(\theta)$

---

## Required Changes for UzEngine

### 1. Apply Analytical Integration
```hlsl
// Current (WRONG):
float3 scattered = sun * scatteringCoeff * phase * dt;
L += scattered;

// Should be (CORRECT):
float3 extinction = scatteringCoeff + absorptionCoeff;
float3 transmittance = exp(-extinction * dt);
float3 scatterTerm = sun * scatteringCoeff * phase;

float3 Sint = (scatterTerm - scatterTerm * transmittance) / max(extinction, 1e-9);
L += Throughput * Sint;

Throughput *= transmittance;
```

### 2. Ensure Height-Based Density
```hlsl
float height = max(0.0, length(worldPos) - planetRadiusKm);
float densityRayleigh = exp(-height / 8.0);      // 8km scale height
float densityMie = exp(-height / 1.2);           // 1.2km scale height

float betaRayleigh = densityRayleigh * 0.033100 * {0.1753, 0.4098, 1.0};
float betaMie = densityMie * 0.003996 * {1.0, 1.0, 1.0};
```

### 3. Pre-expose Sun Illuminance Once
```hlsl
// Before loop:
float3 sunIlluminanceExposed = sunIlluminance * exposureScale;

// Inside loop:
float3 scatterTerm = sunIlluminanceExposed * betaR * rayleighPhase;
```

### 4. Variable Sample Count
Concentrate samples near camera:
```hlsl
if (tMax < 10.0)       // 10km or closer
    SampleCount = 32;
else if (tMax < 60.0)  // 60km
    SampleCount = 16;
else                    // Space/far
    SampleCount = 8;
```

---

## UE5 Source Files Reference

- **Shader implementation**: `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf` (lines 399-764)
- **Sampling function**: `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphereCommon.ush` (lines 308-340)
- **C++ setup**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\SkyAtmosphereComponent.cpp` (lines 89-142)
- **Default parameters**: Same file, lines 94-125
- **Transmittance LUT**: `SkyAtmosphere.usf` lines 1100-1200

---

## Frostbite Reference

The analytical integration formula comes from:
> Slide 28, "Physically Based Unified Volumetric Rendering in Frostbite"
> http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/

UE5 credits this explicitly at line 753 of SkyAtmosphere.usf.

The key insight: instead of `sum of (light * extinction * dt)`, you integrate `exp(-extinction * t)` analytically, giving:
$$\int_0^{dt} e^{-\sigma t} L \, dt = L \cdot \frac{1 - e^{-\sigma \cdot dt}}{\sigma}$$

This naturally accounts for transmittance decay within each step, preventing numerical collapse.

---

## Verification Checklist

- [ ] Rayleigh scale height = 8km
- [ ] Mie scale height = 1.2km
- [ ] Planet radius = 6360km, top = 6420km
- [ ] Default beta coefficients match (0.005802, 0.013558, 0.033100)
- [ ] Analytical integration formula: `Sint = (S - S*T) / Extinction`
- [ ] Transmittance applied iteratively: `Throughput *= exp(-sigma*dt)`
- [ ] Sun illuminance pre-exposed once before loop
- [ ] Variable sample count (more samples near camera)
- [ ] Phase functions computed once per sample point
- [ ] Transmittance to sun sampled from LUT or computed analytically

