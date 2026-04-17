<!-- doc-meta -->
<!-- title: UE5 Rendering Pipeline Patterns and Technical Details -->
<!-- description: Complete technical analysis of UE5 rendering for visual quality parity -->
<!-- -->

# UE5 Rendering Pipeline Patterns & Technical Details

**Analysis Date**: 2026-03-22
**UE5 Version**: 5.6 Source
**Purpose**: Extract rendering architecture and technical specifications for UzEngine parity implementation

---

## 1. Lighting Model — Photorealistic Direct & Indirect Lighting

### 1.1 Direct Lighting (GGX BRDF + Energy Conservation)

UE5 uses physically-based **Generalized-Microfacet Cook-Torrance BRDF** with strict energy conservation.

#### Properties (LightComponent.h)

| Property | Type | Default | Range | Purpose |
|----------|------|---------|-------|---------|
| `Intensity` | float | 1.0 | [0, ∞) | Luminous flux (varies by ELightUnits) |
| `LightColor` | FLinearColor | white | [0,1]^4 | Color in linear RGB |
| `Temperature` | float | 6500.0 | [1700, 12000] | Kelvin (blackbody illuminant, D65=6500K) |
| `bUseTemperature` | bool | false | — | If true, derive color from Temperature instead of LightColor |
| `SpecularScale` | float | 1.0 | [0, 1] | Specular highlight multiplier (unphysical, only for artistry) |
| `DiffuseScale` | float | 1.0 | [0, 1] | Diffuse lighting multiplier (unphysical) |
| `ShadowBias` | float | 0.5 | [0, 1] | Shadow acne mitigation (larger = further from surface) |
| `ShadowSlopeBias` | float | 0.5 | [0, 1] | Slope-dependent bias (scales by surface slope) |
| `ShadowSharpen` | float | 0.0 | [0, 1] | PCF sharpening (higher = harder shadows) |
| `ContactShadowLength` | float | 0.0 | [0, 0.1] | Screen-space ray trace distance for micro-shadows |
| `ContactShadowLengthInWS` | bool | true | — | If true, interpret ContactShadowLength in world units |
| `ContactShadowCastingIntensity` | float | 1.0 | [0, 1] | Shadow intensity for primitives with "cast contact shadow" enabled |
| `ContactShadowNonCastingIntensity` | float | 0.0 | [0, 1] | Shadow intensity for primitives without flag |
| `CastTranslucentShadows` | bool | false | — | Allow dynamic shadows through translucent geometry |
| `bCastShadowsFromCinematicObjectsOnly` | bool | false | — | Only shadow cinematic objects (performance) |

#### Directional Light Specifics (DirectionalLightComponent.h)

| Property | Type | Default | Range | Purpose |
|----------|------|---------|-------|---------|
| `LightSourceAngle` | float | 0.5357° | [0, 5]° | Angular diameter of sun disk (0.5357° = actual sun) |
| `LightSourceSoftAngle` | float | 0.0° | [0, 5]° | Soft edge angle for penumbra |
| `ShadowSourceAngleFactor` | float | 1.0 | [0, 5] | Ray-traced shadow angle relative to LightSourceAngle |
| `ForwardShadingPriority` | int32 | 0 | [0, ∞) | Priority for forward shading (higher wins) |

#### GGX BRDF Formula

UE5 implements **GGX Trowbridge-Reitz microfacet distribution** with:

```
F(h) = Schlick Fresnel approximation
D(h) = GGX distribution (alpha = roughness²)
G(l,v,h) = Smith joint masking-shadowing function
BRDF = (F * D * G) / (4 * dot(n,l) * dot(n,v))
```

Key facts:
- **Roughness parameter**: maps [0,1] → microfacet slope distribution
- **Metallic parameter**: F0 = lerp(0.04, Albedo, Metallic)
- **F0 (Fresnel at 0°)**: 0.04 (4% reflectance) for dielectrics, material color for metals
- **Energy conservation**: F + kd <= 1.0 (no energy creation)
- **Multiple scattering**: optional, computed via ambient BRDF LUT

#### Material Parameters (Substrate/Strata)

Modern UE5 uses **Substrate** (5.4+) for layered materials:

| Parameter | Type | Default | Physical Meaning |
|-----------|------|---------|------------------|
| `BaseColor` | RGB | (0.5,0.5,0.5) | Albedo / diffuse color |
| `Metallic` | [0,1] | 0.0 | Blend between dielectric and conductor |
| `Specular` | [0,1] | 0.5 | Dielectric F0 scale (overrides 0.04) |
| `Roughness` | [0,1] | 0.5 | Microfacet slope (lower = sharper reflections) |
| `Normal` | RGB | (0.5,0.5,1.0) | Tangent-space perturbed normal |
| `Tangent` | RGB | computed | Tangent for anisotropy |
| `Anisotropy` | [0,1] | 0.0 | Directional roughness (for brushed metals) |
| `ClearCoat` | [0,1] | 0.0 | Extra specular layer (car paint, lacquer) |
| `ClearCoatRoughness` | [0,1] | 0.1 | Clearcoat surface roughness |
| `Subsurface` | [0,1] | 0.0 | SSS intensity (wax, marble, skin) |
| `SubsurfaceColor` | RGB | white | SSS scatter color |
| `ThinFilm` | [0,1] | 0.0 | Interference coloring (oil slick, soap) |
| `ThinFilmThickness` | nm | 300 | Film thickness (350-750 nm visible range) |

---

### 1.2 Indirect Lighting (Lumen + IBL)

#### Lumen Ray Tracing

UE5 5.0+ introduced **Lumen** for fully dynamic GI, replacing baked lightmaps for movable objects.

**Lumen Components**:
1. **Surface Cache** — per-frame radiosity cache of visible surfaces
2. **Radiance Cache** — 3D probe grid sampling cached surface radiance
3. **Direct Lighting GI** — traces rays to lights (one-bounce approximation)
4. **Reflections** — ray-traced specular (see section 3)

**Ray Lighting Override** (Scene.h: ELumenRayLightingModeOverride):

| Mode | GPU Cost | Quality | Use Case |
|------|----------|---------|----------|
| `SurfaceCache` | Low | Medium | Default; uses pre-cached surface radiance |
| `HitLightingForReflections` | Medium | High | Full material eval at hit; GI still cached |
| `HitLighting` | High | Highest | Material eval at all hits (full simulation) |

#### IBL (Image-Based Lighting)

UE5 uses pre-baked **diffuse + specular environment maps**:

- **Diffuse Irradiance Map**: 32x32 cubemap (low frequency)
- **Specular Prefiltered Map**: 128x128 cubemap, 5 mips (progressively rougher with mip level)
- **BRDF Integration LUT**: 256x256, 2 channels (Fresnel scale + bias)
- **Lookup**: `sample(diffuseMap, n)` + `sample(specularMap, n, roughness_mip)`

**Sky Light Component** captures the sky into these maps each frame (or on-demand).

---

## 2. Shadow System — VSM, CSM, Contact Shadows

### 2.1 Virtual Shadow Maps (VSM)

UE5 5.1+ introduced **Virtual Shadow Maps** as an alternative to traditional cascaded shadow maps.

**VSM Advantages**:
- Unlimited cascade count (virtual indirection)
- Automatic LOD based on distance
- Memory-efficient (on-demand page-in)

**Setup**:
- 16K x 16K virtual page table
- 128x128 physical cache pages
- Feedback to reproject previous frame's shadow coverage
- GPU-driven page streaming

### 2.2 Cascaded Shadow Maps (CSM)

Traditional approach, still primary method in UE5.

#### Configuration (DirectionalLightComponent.h)

| Property | Default | Range | Purpose |
|----------|---------|-------|---------|
| `DynamicShadowDistanceMovableLight` | 5000 cm | [0, 20000] | Max shadow distance for movable lights |
| `DynamicShadowDistanceStationaryLight` | 20000 cm | [0, 20000] | Max shadow distance for stationary lights |
| `DynamicShadowCascades` | 4 | [0, 4] | Number of cascade splits |
| `CascadeDistributionExponent` | 2.0 | [1.0, 4.0] | Distribution: >2 = closer to camera, <2 = farther |
| `CascadeTransitionFraction` | 0.1 | [0, 0.3] | Fade zone between cascades (10% default) |
| `ShadowDistanceFadeoutFraction` | 1.0 | [0, 1.0] | Fade-out at far extent (as fraction of distance) |
| `ShadowCascadeBiasDistribution` | 1.0 | [0, 1] | Bias scaling: 1=per-cascade, 0=uniform |
| `bUseInsetShadowsForMovableObjects` | true | — | Allow per-object shadows outside CSM range |
| `FarShadowCascadeCount` | 0 | [0, 4] | Extra cascades beyond DynamicShadowDistance |
| `FarShadowDistance` | 100000 cm | — | End distance for far cascades |
| `DistanceFieldShadowDistance` | 50000 cm | — | Ray-traced distance field shadow range |

#### CSM Resolution & Quality

```
Base Resolution = 2048 x 2048 per cascade
Adjusted by: r.Shadow.MaxResolution (max clamping)
            ShadowResolutionScale per-light multiplier
            Screen-size adaptive scaling
```

**Default Poisson-disk PCF**: 16-tap, radius scales with distance (distance-based penumbra).

#### Shadow Bias (Per-Cascade Calculation)

```cpp
bias = ShadowBias + (ShadowSlopeBias * max_slope)
// Cascade-specific scaling (if ShadowCascadeBiasDistribution == 1.0)
cascade_bias = bias * cascade_size_scale
```

### 2.3 Contact Shadows (Screen-Space Ray Tracing)

Fine detail shadows from directional light, computed in deferred pass.

#### Algorithm

```
For each fragment:
  IF (ContactShadowLength > 0):
    ray_length = ContactShadowLength [WS]
    ray_dir = normalize(light_direction)
    max_steps = 16 (typical)

    FOR step in [1..max_steps]:
      sample_uv = screen_uv + (step / max_steps) * ray_length * project_to_screen(ray_dir)
      sample_depth = read_depth(sample_uv)
      surface_depth = fragment_depth

      IF (sample_depth > surface_depth):  // Hit occluder
        shadow_factor = lerp(0, 1, step / max_steps)  // Fade based on distance
        BREAK

    contact_shadow = shadow_factor
```

**Parameters**:
- `ContactShadowLength`: 0.01 - 0.1 (screen space) or 1.0 - 100.0 (world space)
- `ContactShadowCastingIntensity`: 0.0 - 1.0 (intensity for cast shadow primitives)
- `ContactShadowNonCastingIntensity`: 0.0 - 1.0 (intensity for non-casting primitives, usually 0)

**Visual use**: Enhances fine details in height maps, small occluders, surface wrinkles.

---

## 3. Reflection System — Lumen, SSR, Probes

### 3.1 Lumen Reflections

Fully dynamic ray-traced reflections using **Lumen surface cache** (same as GI).

**EReflectionsType** (Scene.h):
- `ScreenSpace`: SSR + fallback to reflection probes
- `RayTracing`: Lumen ray-traced reflections

**Lumen Reflection Modes** (ELumenRayLightingModeOverride):
- **SurfaceCache**: Uses precomputed surface radiance (fastest, lowest quality)
- **HitLightingForReflections**: Evaluates material at hit point with lights
- **HitLighting**: Full material + lighting at all bounces (slowest, highest quality)

### 3.2 Screen-Space Reflections (SSR)

Fallback when Lumen unavailable or performance-critical.

**Algorithm**:
```
For each fragment:
  view_ray = normalize(world_pos - camera_pos)
  reflect_ray = reflect(view_ray, normal)

  max_steps = 64
  step_size = 0.1 (screen space)

  FOR step in [0..max_steps]:
    sample_pos = world_pos + (step * step_size * reflect_ray)
    sample_uv = project(sample_pos)
    sample_depth = texture(depth, sample_uv)

    IF sample_depth < length(sample_pos - camera):
      hit_color = texture(color, sample_uv)
      confidence = calculate_confidence(step, angle, roughness)
      RETURN lerp(fallback, hit_color, confidence)

  RETURN fallback_color
```

**Quality factors**:
- Roughness-based ray jitter (rough = broader search)
- Confidence fade at screen edges (edge fade)
- Thickness bias to prevent self-intersections

### 3.3 Reflection Capture Probes

Static/baked environment maps at discrete locations.

**Types**:
- `SphereReflectionCapture`: Spherical influence, interpolates with distance
- `BoxReflectionCapture`: Box influence, parallax-corrected for interior scenes
- `PlaneReflectionCapture`: Planar mirror reflection (water, glass)

**Priority**: Lumen > Closest probe > Screen-space fallback

### 3.4 Sky Light Component

Real-time or baked sky light using captured environment.

| Property | Default | Purpose |
|----------|---------|---------|
| `Intensity` | 1.0 | Multiplier on captured sky |
| `IndirectLightingIntensity` | 1.0 | GI contribution scale |
| `VolumetricScatteringIntensity` | 1.0 | Volumetric fog color scale |
| `CubemapResolution` | 128 | Capture cubemap mip 0 size |
| `bRealTimeCaptureEnabled` | false | Recapture every frame (expensive) |
| `CaptureInterval` | 0.1 | Min time between captures (seconds) |
| `LowerHemisphereIsBlack` | false | Force bottom hemisphere to black (underground) |
| `CloudAmbientOcclusion` | 1.0 | Scale AO from clouds onto sky light |

---

## 4. Post-Processing Pipeline — Bloom, Tone Mapping, Color Grading, TAA

### 4.1 Bloom (HDR Light Bloom)

UE5 supports two bloom methods:

#### 4.1.1 Gaussian Sum of Gaussians (BM_SOG) — Default

Multi-tap Gaussian blur at progressively lower resolutions.

**FGaussianSumBloomSettings** (Scene.h):

| Property | Default | Range | Purpose |
|----------|---------|-------|---------|
| `Intensity` | 0.675 | [0, 8] | Overall bloom brightness |
| `Threshold` | -1.0 | [-1, 8] | Bloom start brightness (-1 = all pixels) |
| `SizeScale` | 4.0 | [0, 64] | Master scale for all bloom taps |
| `Filter1Size` | 0.3 | [0, 4] | 1/2 resolution blur diameter (%) |
| `Filter2Size` | 1.0 | [0, 8] | 1/4 resolution blur diameter (%) |
| `Filter3Size` | 2.0 | [0, 16] | 1/8 resolution blur diameter (%) |
| `Filter4Size` | 10.0 | [0, 32] | 1/16 resolution blur diameter (%) |
| `Filter5Size` | 30.0 | [0, 64] | 1/32 resolution blur diameter (%) |
| `Filter6Size` | 64.0 | [0, 128] | 1/64 resolution blur diameter (%) |
| `FilterN_Tint` | varied | [0,1]^4 | Per-tap color (default: grayscale) |

**Default Filter Tints**:
```
Filter1Tint = (0.3465, 0.3465, 0.3465)  // ~34.65% contribution
Filter2Tint = (0.138, 0.138, 0.138)     // ~13.8% contribution
Filter3Tint = (0.1176, 0.1176, 0.1176)  // ~11.76% contribution
Filter4Tint = (0.066, 0.066, 0.066)     // ~6.6% contribution
Filter5Tint = (0.066, 0.066, 0.066)     // ~6.6% contribution
Filter6Tint = (0.061, 0.061, 0.061)     // ~6.1% contribution
```
**Sum ≈ 1.0** (energy-conserving)

#### 4.1.2 Convolution Bloom (BM_FFT) — Cinematic

FFT-based convolution with custom kernel texture (very expensive, for cinematics).

**FConvolutionBloomSettings**:

| Property | Default | Purpose |
|----------|---------|---------|
| `Texture` | nullptr | Custom kernel texture |
| `ScatterDispersion` | 1.0 | Kernel energy scale |
| `Size` | 1.0 | Kernel size relative to viewport |
| `CenterUV` | (0.5, 0.5) | Kernel center offset |
| `PreFilterMin` | 7.0 | Pre-bloom threshold (min) |
| `PreFilterMax` | 15000.0 | Pre-bloom threshold (max) |
| `PreFilterMult` | 15.0 | Pre-bloom intensity multiplier |
| `BufferScale` | 0.133 | Extra padding to prevent wrap |

### 4.2 Tone Mapping (ACES)

UE5 uses **ACES fitted curve** (Academy Color Encoding System) for industry-standard tone mapping.

**Algorithm**:
```
// Input: HDR linear color
hdr_color = render_target_value

// Apply exposure adjustment (from auto-exposure or manual)
exposed_color = hdr_color * exposure

// ACES fitted tone curve
// Reference: Hill "Filmic Tonemapping for Real-time Rendering" (SIGGRAPH 2010)
a = 2.51
b = 0.03
c = 2.43
d = 0.59
e = 0.14

tone_mapped = (exposed_color * (a * exposed_color + b)) /
              (exposed_color * (c * exposed_color + d) + e)

// Gamma correction (sRGB)
output = pow(tone_mapped, 1.0 / 2.2)
```

**Parameters** (FPostProcessSettings):
- `bOverride_ToneCurveAmount`: Enable/disable tone curve
- `ToneCurveAmount`: [0, 1] (blend with identity)

### 4.3 Color Grading (LUT-based + Manual Controls)

UE5 uses **LUT (Look-Up Table)** approach plus per-range controls.

#### 4.3.1 Per-Range Color Grade (FColorGradingSettings)

Applied to Shadows, Midtones, Highlights separately.

**FColorGradePerRangeSettings**:

| Property | Default | Range | Purpose |
|----------|---------|-------|---------|
| `Saturation` | (1,1,1,1) | [0, 2] | Per-channel saturation |
| `Contrast` | (1,1,1,1) | [0, 2] | Per-channel contrast |
| `Gamma` | (1,1,1,1) | [0.01, 2] | Per-channel gamma (lift) |
| `Gain` | (1,1,1,1) | [0, 2] | Per-channel gain (exposure) |
| `Offset` | (0,0,0,0) | [-1, 1] | Per-channel offset (shadows/lift) |

**Range Boundaries**:
```cpp
FColorGradingSettings {
  FColorGradePerRangeSettings Global;      // Applied to all pixels
  FColorGradePerRangeSettings Shadows;     // Pixels < ShadowsMax
  FColorGradePerRangeSettings Midtones;    // Between Shadows & Highlights
  FColorGradePerRangeSettings Highlights;  // Pixels > HighlightsMin

  float ShadowsMax = 0.09f;                // Max luminance for shadows
  float HighlightsMin = 0.5f;              // Min luminance for highlights
  float HighlightsMax = 1.0f;              // Max luminance clamping
}
```

#### 4.3.2 LUT Volume Texture

Custom 3D color LUT (256x16x16 typical for 16x16x16 grid in 256x256 texture).

**Texture Format**: sRGB → Linear conversion, RGBA8 or higher

### 4.4 Auto-Exposure (Histogram)

UE5 supports three auto-exposure methods.

#### 4.4.1 Histogram Method (AEM_Histogram)

**Algorithm**:
```
1. Downsample render target → luminance histogram (64 bins)
2. Compute weighted average of histogram
3. Target exposure to bring average to middle gray (0.18)
4. Smooth exposure blend with previous frame
```

**Parameters** (FPostProcessSettings):
- `bOverride_AutoExposure`: Enable histogram method
- `AutoExposureMinBrightness`: [0.02, 1.0] (clamp low end)
- `AutoExposureMaxBrightness`: [1.0, 8.0] (clamp high end)
- `AutoExposureBias`: [-2, 2] (offset from middle gray)
- `AutoExposureSpeedUp`: [0.02, 4] (convergence speed going brighter)
- `AutoExposureSpeedDown`: [0.02, 4] (convergence speed going darker)

#### 4.4.2 Basic Method (AEM_Basic)

Simpler compute: single weighted average, no histogram.

#### 4.4.3 Manual Mode (AEM_Manual)

Camera settings drive exposure (no automatic adjustment).

### 4.5 Temporal Anti-Aliasing (TAA) & Temporal Super-Resolution (TSR)

#### TAA Properties (FPostProcessSettings)

| Property | Default | Purpose |
|----------|---------|---------|
| `bOverride_TemporalAA` | false | Enable TAA |
| `TemporalAAQuality` | 1.0 | [0, 3] sample count (lower = faster) |
| `TemporalAACurrentFrameWeight` | 0.1 | [0, 1] current frame blend (lower = more history) |
| `TemporalAAJitter` | Halton sequence | Subpixel jitter (decorrelation) |

**Algorithm**:
```
current_frame = render()
reprojected_history = reproject(history, motion_vectors)
taa_output = lerp(reprojected_history, current_frame, weight)
```

**Key detail**: Uses **Halton sequence** (low-discrepancy) for jitter pattern across frames.

#### TSR (Temporal Super-Resolution)

More advanced: upsamples lower-res render + TAA for performance.

| Parameter | Range | Purpose |
|-----------|-------|---------|
| `ScreenPercentage` | [25, 100]% | Render resolution as % of display |
| `TSR_Enable` | bool | Use TSR instead of standard post-process upscale |
| `TSR_HistoryScreenPercentage` | [50, 100]% | History buffer resolution |

### 4.6 Motion Blur

Post-process effect using velocity buffers.

| Property | Default | Range | Purpose |
|----------|---------|-------|---------|
| `bOverride_MotionBlur` | false | — | Enable effect |
| `MotionBlurAmount` | 0.5 | [0, 1] | Blur intensity |
| `MotionBlurMax` | 5.0 | [0, 32] | Max blur radius (pixels) |
| `MotionBlurPerObjectSize` | 0.0 | [0, 1] | Per-object motion scale |

### 4.7 Depth of Field (DOF)

Three methods available.

#### Method: BokehDOF (DOFM_BokehDOF)

Physically-inspired bokeh simulation.

| Property | Default | Purpose |
|----------|---------|---------|
| `DepthOfFieldFocalDistance` | 0.0 | World units from camera to focal plane |
| `DepthOfFieldFocalRegion` | 0.0 | Width of transition zone |
| `DepthOfFieldScale` | 0.0 | Overall blur magnitude |
| `DepthOfFieldBokehShape` | texture | Custom bokeh kernel |
| `DepthOfFieldMaxBokehSize` | 32.0 | Max blur radius (pixels) |

#### Method: GaussianDOF (DOFM_Gaussian)

Simple Gaussian blur by depth.

#### Method: CircleDOF (DOFM_CircleDOF)

Circle-shaped bokeh (simpler, faster).

### 4.8 Chromatic Aberration

Simulates lens color fringing.

| Property | Default | Range | Purpose |
|----------|---------|-------|---------|
| `bOverride_ChromaticAberration` | false | — | Enable effect |
| `ChromaticAberration` | 0.0 | [0, 1] | Aberration magnitude |

---

## 5. Material System — Substrate/Strata, Subsurface Scattering, Clearcoat, Thin Film

### 5.1 Substrate (Unified Material Framework)

UE5 5.4+ introduces **Substrate** — a unified material system replacing the old BRDF selector.

**Material Layers**:
1. **Base Layer** — Primary PBR material
2. **Clear Coat** — Specular topcoat (car paint, polish)
3. **Subsurface** — SSS/translucency layer
4. **Thin Film** — Interference coloring (oil slick, soap)

### 5.2 Subsurface Scattering (SSS)

Simulates light scattering through translucent materials (skin, wax, marble).

#### Parameters

| Property | Type | Default | Physical Meaning |
|----------|------|---------|------------------|
| `SubsurfaceColor` | RGB | white | Scatter color in linear space |
| `SubsurfaceProfile` | Texture | nullptr | Pre-computed SSS kernel (1D) |
| `SubsurfaceRadius` | float | 1.0 cm | Scatter distance (mean path length) |

#### Profile LUT

UE5 uses **Separable SSS** (Jimenez et al. 2012):
- 1D kernel texture (256 pixels) storing blur radius per pixel
- Computed offline from physical scattering simulation
- Applied as separable convolution (X + Y passes)

**Standard profiles**:
- Skin (warm yellow scatter)
- Wax (cold blue scatter)
- Marble (white scatter)

### 5.3 Clear Coat

Extra specular layer for coated surfaces (car paint, lacquer, shellac).

| Property | Default | Physical Meaning |
|----------|---------|------------------|
| `ClearCoat` | 0.0 | Layer intensity [0, 1] |
| `ClearCoatRoughness` | 0.1 | Surface roughness of topcoat |
| `ClearCoatBottomNormal` | computed | Normal from base layer (parallax) |

**Rendering**:
```
specular_contribution = lerp(base_specular, clearcoat_specular, clearcoat)
```

### 5.4 Thin Film Interference

Simulates oil slicks, soap bubbles, feathers (color shifts with thickness).

| Property | Default | Range | Physical Meaning |
|----------|---------|-------|------------------|
| `ThinFilm` | 0.0 | [0, 1] | Layer presence |
| `ThinFilmThickness` | 300 nm | [0, 1000] nm | Film thickness (affects color) |

**Fresnel Shift**: Thickness causes wavelength-dependent phase shift → color iridescence.

---

## 6. Atmosphere & Fog — Sky Atmosphere, Volumetric Fog, Height Fog

### 6.1 Sky Atmosphere (Bruneton Model)

UE5 uses **Bruneton atmospheric scattering** (Bruneton & Neyret 2008).

#### USkyAtmosphereComponent Properties

**Planet Setup**:

| Property | Default | Range | Purpose |
|----------|---------|-------|---------|
| `BottomRadius` | 6371.0 km | [0.1, 10000] | Planet radius (Earth ≈ 6371 km) |
| `AtmosphereHeight` | 80 km | [0.1, 200] | Atmosphere thickness above ground |
| `GroundAlbedo` | (0.3, 0.3, 0.3) | [0, 1]^3 | Ground reflectance for multiscatter |
| `TransformMode` | PlanetTopAtComponentTransform | — | Where planet center is relative to component |

**Rayleigh Scattering (air molecules)**:

| Property | Default | Physical Meaning |
|----------|---------|------------------|
| `RayleighScatteringScale` | 1.0 | Overall scale factor |
| `RayleighScattering` | (0.00588, 0.01386, 0.03351) | Scattering coefficients at sea level |
| `RayleighExponentialDistribution` | 8.0 km | Altitude where density drops to 40% |

**Typical values** (Earth atmosphere):
```
λ_blue (450nm):   0.00588 km^-1
λ_green (550nm):  0.01386 km^-1  (actually less than blue due to relative units)
λ_red (650nm):    0.03351 km^-1  (actually less, but shows expected trend)
```
Note: Values depend on normalization; relative ratios matter for color.

**Mie Scattering (aerosols/particles)**:

| Property | Default | Physical Meaning |
|----------|---------|------------------|
| `MieScatteringScale` | 1.0 | Overall scale factor |
| `MieScattering` | (0.003996, 0.003996, 0.003996) | Scattering from particles |
| `MieAbsorptionScale` | 1.0 | Absorption scale |
| `MieAbsorption` | (0.000444, 0.000444, 0.000444) | Absorption coefficients |
| `MieAnisotropy` | 0.8 | Phase function anisotropy [0, 0.999] |
| `MieExponentialDistribution` | 1.2 km | Altitude scale for Mie density |

**Mie anisotropy**: 0 = isotropic scatter, 0.8 = forward-scattering (halo around sun).

**Absorption Layer (Ozone)**:

| Property | Default | Purpose |
|----------|---------|---------|
| `OtherAbsorptionScale` | 0.0 | Overall scale |
| `OtherAbsorption` | (0.00006, 0.0001, 0.00001) | Absorption in extra layer |
| `OtherTentDistribution` | altitude 10-25km | Where this layer exists |

**Visual Controls**:

| Property | Default | Purpose |
|----------|---------|---------|
| `SkyLuminanceFactor` | (1.0, 1.0, 1.0, 1.0) | Sky color scale |
| `SkyAndAerialPerspectiveLuminanceFactor` | (1.0, 1.0, 1.0, 1.0) | AP color scale |
| `AerialPespectiveViewDistanceScale` | 1.0 | [0, 3] Thickness of AP |
| `AerialPerspectiveStartDepth` | 0.001 km | Where AP begins |
| `TransmittanceMinLightElevationAngle` | -90.0° | Sun visibility angle |
| `MultiScatteringFactor` | 1.0 | [0, 100] Multiscatter intensity |
| `TraceSampleCountScale` | 1.0 | [0.25, 8] Sample count quality scale |
| `HeightFogContribution` | 0.0 | [0, 1] Affect height fog color |

#### Bruneton Algorithm

```
1. Build lookup tables (per-atmosphere per-frame or cached)
   - Transmittance LUT: 256x64 (altitude vs sun angle)
   - MultiScatter LUT: 32x32
   - Aerial Perspective LUT: 32x32xSlices (depth + direction)

2. For each pixel:
   camera_to_surface_transmittance = sample(TransmittanceLUT, camera_altitude, zenith_angle)

   FOR each scatter direction:
     sun_to_surface_transmittance = sample(TransmittanceLUT, surface_altitude, sun_angle)
     inscatter = rayleigh_phase(sun_dir, view_dir) + mie_phase(sun_dir, view_dir) * phase
     radiance += (sun_transmit * view_transmit * inscatter)

   RETURN radiance + surface_color * transmittance
```

### 6.2 Volumetric Fog (Froxel-based)

Dynamic per-frame fog simulation in 3D grid.

**UExponentialHeightFogComponent with Volumetric Fog enabled**:

| Property | Default | Range | Purpose |
|----------|---------|-------|---------|
| `bEnableVolumetricFog` | false | — | Enable volumetric fog |
| `VolumetricFogScatteringDistribution` | 0.2 | [-0.9, 0.9] | Phase function (0=isotropic, >0=forward) |
| `VolumetricFogAlbedo` | white | [0,1]^3 | Particle reflectance |
| `VolumetricFogEmissive` | black | [0,∞)^3 | Self-emitted light (glow) |
| `VolumetricFogExtinctionScale` | 1.0 | [0.1, 10] | Absorption amount |
| `VolumetricFogDistance` | 9000 cm | [1000, 10000] | Integration distance |
| `VolumetricFogStartDistance` | 0 cm | [0, 5000] | Start distance from camera |
| `VolumetricFogNearFadeInDistance` | 0 cm | [0, 1000] | Fade-in zone width |
| `VolumetricFogStaticLightingScatteringIntensity` | 2.0 | [0, 10] | Static light contribution |

**Froxel Grid**:
- Depth slices: 64 slices from start to start+distance
- XY resolution: scalability setting (256x256 → 64x64 at low end)
- Per-voxel storage: radiance + transmittance

**Scattering Integration**:
```cpp
FOR each froxel:
  scattered_light += (sun_light * phase_function(sun_dir) * extinction)
  scattered_light += (sky_light * extinction * albedo)
  transmitted_light *= exp(-extinction * distance)
```

### 6.3 Exponential Height Fog (Legacy, still used)

Screen-space height fog without per-voxel simulation.

**UExponentialHeightFogComponent** (without volumetric):

| Property | Default | Range | Purpose |
|----------|---------|-------|---------|
| `FogDensity` | 0.02 | [0, 0.05] | Global density factor |
| `FogHeightFalloff` | 0.2 | [0.001, 2.0] | Density drop per height unit |
| `FogInscatteringLuminance` | computed | — | Fog color (from sky or manual) |
| `FogMaxOpacity` | 1.0 | [0, 1] | Maximum fog opacity |
| `StartDistance` | 0 | [0, 5000] | Camera distance to start fog |
| `FogCutoffDistance` | 20000000 | [100k, 20M] | Distance to stop fogging |
| `DirectionalInscatteringExponent` | 8.0 | [2, 64] | Sun inscatter cone angle |
| `DirectionalInscatteringStartDistance` | 1000 | [0, 20000] | Sun inscatter start distance |
| `DirectionalInscatteringLuminance` | computed | — | Sun inscatter color |
| `bOverrideLightColorsWithFogInscatteringColors` | false | — | Use fog color for volumetric scattering |

**Algorithm**:
```cpp
fog_density = FogDensity * exp(-height_above_fog_zero * FogHeightFalloff)
fog_color = FogInscatteringLuminance +
            sun_light * DirectionalInscatteringLuminance *
            pow(dot(view_dir, light_dir), DirectionalInscatteringExponent)
fog_transmittance = exp(-fog_density * distance)
output = lerp(fog_color, surface_color, fog_transmittance)
```

**Integration with Sky Atmosphere**:
- When `r.SupportExpFogMatchesVolumetricFog = 1`: uses atmospheric scattering color instead of fixed color
- Allows height fog to match sky color dynamically

---

## 7. Summary: Recommended Implementation Priority for UzEngine

### P0 (Must-Have for Photorealistic Look)
1. **PBR Lighting** — GGX BRDF with energy conservation (see 1.1)
2. **Cascaded Shadow Maps** — 4 cascades, per-cascade bias, distance-based penumbra (see 2.2)
3. **IBL** — Diffuse + specular prefiltered maps with BRDF LUT (see 1.2)
4. **ACES Tone Mapping** — Industry-standard tone curve (see 4.2)
5. **Sky Atmosphere** — Bruneton model (see 6.1)

### P1 (Quality Enhancements)
6. **Contact Shadows** — Screen-space ray-traced micro-shadows (see 2.3)
7. **Bloom** — Gaussian sum (6 taps, separable) (see 4.1)
8. **Color Grading** — Per-range (shadows/midtones/highlights) (see 4.3)
9. **TAA** — Temporal reprojection + Halton jitter (see 4.5)
10. **Height Fog** — Exponential with directional inscattering (see 6.3)

### P2 (Polish & Special Effects)
11. **Auto-Exposure** — Histogram-based (see 4.4)
12. **Subsurface Scattering** — Separable kernel, profile LUT (see 5.2)
13. **Clearcoat** — Second specular layer (see 5.3)
14. **Screen-Space Reflections** — Fallback reflection system (see 3.2)
15. **Volumetric Fog** — Froxel grid (see 6.2)

---

## 8. Key Technical Principles

### Energy Conservation
All lighting contributions must sum to ≤ 1.0 to prevent synthetic brightening. Typically:
```
outgoing_radiance = diffuse_component + specular_component
                  = (1 - fresnel) * diffuse_brdf + fresnel * specular_brdf
```

### Physically-Based Defaults
- **F0 (Dielectric)**: 0.04 (4% reflectance, ~40% index of refraction for glass)
- **F0 (Metal)**: Material albedo color
- **Roughness**: [0, 1] maps to microfacet slope (0=mirror, 1=diffuse)

### Perceptual Rendering
- **Linear space computation**: All math in linear RGB
- **sRGB output**: Final 2.2 gamma for display
- **Halton jitter**: Low-discrepancy TAA sequence (not random)

### Performance Scalability
UE5 uses `r.*` console variables to scale quality:
- `r.Shadow.MaxResolution` — shadow map resolution cap
- `r.SkyAtmosphere.SampleCountMax` — sky sampling count
- `r.VolumetricFog.GridPixelSize` — froxel resolution
- `r.MotionBlur.MaxSize` — motion blur radius limit
- `r.Bloom.ScreenPercentage` — bloom resolution

---

## References

- **GGX BRDF**: Walter et al. 2007 "Microfacet Models for Refraction through Rough Surfaces"
- **Schlick Fresnel**: Schlick 1994 "An Inexpensive BRDF Model for Physically-Based Rendering"
- **Smith G Function**: Smith 1967 "Geometrical shadowing of a random rough surface"
- **ACES Tone Mapping**: Hill 2010 "Filmic Tonemapping for Real-time Rendering" (SIGGRAPH)
- **Bruneton Sky**: Bruneton & Neyret 2008 "Precomputed Atmospheric Scattering"
- **Separable SSS**: Jimenez et al. 2012 "Real-Time Realistic Skin Translucency"
- **Poisson-Disk PCF**: Simonsen & Andersen 2006 "Real-time Light Space Perspective Shadow Maps"
- **Halton Sequence**: Halton 1960 "On the efficiency of certain quasi-random sequences"

---

**Document Status**: Complete technical reference for UE5 rendering parity (2026-03-22)
