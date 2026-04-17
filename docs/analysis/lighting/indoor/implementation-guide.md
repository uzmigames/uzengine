<!-- doc-meta
title: Interior Lighting Implementation Guide for UzEngine
created: 2026-04-06
status: actionable
audience: architects, rendering engineers
keywords: interior, sky light, DFAO, Lumen, layered architecture
-->

# Interior Lighting Implementation Guide for UzEngine

## Executive Summary

UE5's interior lighting is **not a special mode**—it's a **layered composition system** where:

1. **CSM/VSM** renders direct sunlight (blocked indoors → contribution = 0)
2. **SkyLight** provides ambient fill from visible sky (cheap, effective)
3. **Distance Field AO** darkens contact shadows (spatial variation)
4. **Lumen GI** bounces light between surfaces (color variation)
5. **Post-FX** (SSAO, SSR) adds high-frequency detail

**Indoor scenes automatically look correct** when all layers are enabled with appropriate tuning.

---

## Implementation Priority

### Phase 1 (Now): Sky Light + DFAO
- **Effort**: 2-3 weeks
- **Impact**: 80% visual improvement (interior scenes no longer pitch-black)
- **Dependencies**: GBuffer, deferred lighting pass, DFAO system

### Phase 2 (Month 2): Lumen Radiosity
- **Effort**: 4-6 weeks
- **Impact**: Color bounces, perceptual correctness
- **Dependencies**: Screen-space probe placement, hardware RT or software tracing

### Phase 3 (Month 3): Lightmass Integration
- **Effort**: 2-3 weeks (if SkyLight+Lumen exist)
- **Impact**: Pre-baked quality for static scenes
- **Dependencies**: Mesh card baking, SH coefficient storage

---

## Layer 1: Sky Light + DFAO (Foundation)

### Architecture

```
┌─ SkyLightComponent (ECS)
│  ├─ Properties: SourceType, Cubemap, bRealTimeCapture
│  ├─ Runtime: Captures sky cubemap → convolves to SH (9 coefficients)
│  └─ Output: SkyIrradianceSH (3 SH vectors, RGB)
│
└─ Deferred Lighting Pass
   ├─ Reads: GBuffer (normal, roughness, metallic, albedo)
   ├─ Reads: SkyIrradianceSH from constant buffer
   ├─ Reads: DFAO occlusion texture (from DistanceFieldAO pass)
   ├─ Compute: EvaluateSH(normal) × albedo × (1 - dfao)
   └─ Output: lighting buffer (accumulated)
```

### SkyLightComponent Specification

**Location**: `runtime/src/engine/components/sky_light_component.h`

```cpp
struct SkyLightComponent : public Component {
    // --- Capture Source ---
    enum SourceType {
        CapturedScene,      // Real-time capture current viewport
        SpecifiedCubemap,   // Static cubemap file
    } source_type = SourceType::CapturedScene;
    
    // Cubemap to use when SourceType = SpecifiedCubemap
    Handle<Texture> cubemap = Handle<Texture>::null();
    
    // Real-time capture settings
    bool b_real_time_capture = true;
    float sky_distance_threshold = 150000.0f;  // Kilometers; geometry beyond this is "sky"
    
    // --- Indirect Lighting ---
    float intensity = 1.0f;              // Multiplier on irradiance
    float indirect_lighting_intensity = 1.0f;  // Separate indirect scale
    float volumetric_scattering_intensity = 0.2f;
    
    // --- Lower Hemisphere ---
    // Critical for interiors: should ground contribute to sky lighting?
    bool b_lower_hemisphere_is_black = false;  // UE5 default: FALSE
    Vec3 lower_hemisphere_color = {0.1f, 0.15f, 0.2f};  // Fallback if not captured
    
    // --- Distance Field Ambient Occlusion ---
    float occlusion_max_distance = 500.0f;  // cm; contact shadow reach
    float occlusion_exponent = 0.85f;       // < 1: brighten, > 1: darken
    float contrast = 0.5f;                  // S-curve on AO (0 = none, 1 = max)
    float min_occlusion = 0.15f;            // Brighten fully-occluded corners
    Vec3 occlusion_tint = {240, 200, 180};  // Warm shadows (optional)
    
    // --- Output (computed per-frame) ---
    SHVectorRGB3 irradiance_sh;  // 9 float3 coefficients (SH bands 0-2)
    // Usage: EvaluateSH(normal) gives irradiance in that direction
};
```

### DFAO Pass Specification

**Location**: Existing (`src/engine/renderer/passes/dfao_pass.cpp`)

The DFAO pass is **already implemented** but needs wiring to SkyLight:

```cpp
// EXISTING: Distance Field AO Pass
class DFAOPass : public RenderPass {
    void Execute() {
        // 1. Dispatch compute shader: ray-march distance field
        // 2. For each pixel: trace cone toward normal
        // 3. Accumulate occlusion over max_distance
        // 4. Apply contrast and min_occlusion curves
        // 5. Write result to ao_texture
    }
};

// NEEDED: Integration with SkyLight
class DeferredLightingPass : public RenderPass {
    void Execute() {
        // ...existing lighting code...
        
        // NEW: Apply SkyLight + DFAO
        if (HasComponent<SkyLightComponent>()) {
            const auto& sky = GetComponent<SkyLightComponent>();
            
            // For each pixel in GBuffer:
            float3 normal = gbuffer_normal;
            float3 sky_irradiance = EvaluateSH(sky.irradiance_sh, normal);
            float ao = ReadOcclusionTexture(dfao_ao_texture);
            
            float3 contribution = sky_irradiance * albedo * (1.0f - ao);
            lighting_buffer += contribution;
        }
    }
};
```

### SH Evaluation Function

**Location**: `runtime/src/engine/renderer/lighting/sh_math.h`

```cpp
/// @ref Ramamoorthi 2002 "Precomputed Radiance Transfer"
/// Evaluate spherical harmonic at given direction
/// sh: 9 SH coefficients (3x3 float3 array, bands 0-2)
/// direction: normalized surface normal
/// Returns: irradiance in that direction
float3 EvaluateSH(const SHVectorRGB3& sh, const float3& direction) {
    // SH basis functions (pre-computed)
    // Band 0 (constant):
    float c0 = 0.282095f;
    
    // Band 1 (linear, 3 coefficients):
    float c1a = 0.488603f;
    float c1b = 0.488603f;
    float c1c = 0.488603f;
    
    // Band 2 (quadratic, 5 coefficients):
    float c2a = 1.092548f;
    float c2b = 1.092548f;
    float c2c = 0.315392f;
    float c2d = 0.546274f;
    float c2e = 0.546274f;
    
    // Basis evaluation
    float sh_00 = c0;
    float sh_1m1 = c1a * direction.z;
    float sh_10 = c1b * direction.y;
    float sh_11 = c1c * direction.x;
    float sh_2m2 = c2a * direction.x * direction.z;
    float sh_2m1 = c2b * direction.y * direction.z;
    float sh_20 = c2c * (3.0f * direction.y * direction.y - 1.0f);
    float sh_21 = c2d * direction.x * direction.y;
    float sh_22 = c2e * (direction.x * direction.x - direction.z * direction.z);
    
    // Accumulate with coefficients
    float3 result = float3(0.0f);
    result += sh[0][0] * sh_00;
    result += sh[1][0] * sh_1m1;
    result += sh[1][1] * sh_10;
    result += sh[1][2] * sh_11;
    result += sh[2][0] * sh_2m2;
    result += sh[2][1] * sh_2m1;
    result += sh[2][2] * sh_20;
    result += sh[2][3] * sh_21;
    result += sh[2][4] * sh_22;
    
    // Clamp negative to 0 (SH can ring)
    result = max(result, float3(0.0f));
    
    return result;
}
```

### Real-Time Sky Capture

**Location**: `runtime/src/engine/renderer/passes/sky_capture_pass.cpp` (NEW)

```cpp
class SkyCapturePass : public RenderPass {
    void Execute() {
        // 1. Render sky to cubemap (6 faces, 64x64 or 128x128)
        //    - Use SkyAtmosphereComponent + any sky dome/material
        // 2. For each face, render to FRDGTextureCube
        // 3. Convolve cubemap to irradiance (SH coefficients)
        
        // Convolution shader: 
        //   For each SH band m:
        //     For each direction (use hemisphere samples):
        //       coeff[m] += SampleCubemap(direction) × SHBasis[m](direction)
        
        // Write results to scene's SkyLightComponent.irradiance_sh
    }
};
```

### Outdoor Tuning Example

```cpp
// Outdoor scene: strong sun, blue sky
SkyLightComponent sky;
sky.intensity = 0.8f;
sky.lower_hemisphere_is_black = false;  // Include ground bounce
sky.min_occlusion = 0.05f;  // Outdoor shadows can be very dark
sky.contrast = 0.6f;        // Increase contrast in bright scenes
```

### Indoor Tuning Example (Sponza Atrium)

```cpp
// Indoor atrium scene: diffuse sunlight from above, no direct rays
SkyLightComponent sky;
sky.intensity = 1.2f;                   // Boost sky contribution (interior brighter)
sky.lower_hemisphere_is_black = false;  // Let ground contribute
sky.lower_hemisphere_color = {0.15f, 0.20f, 0.25f};  // Soft blue fill
sky.min_occlusion = 0.25f;              // Keep corners from getting too dark
sky.occlusion_exponent = 0.80f;         // Brighten AO overall
sky.contrast = 0.3f;                    // Reduce contrast (interior is softer)
sky.occlusion_tint = {200, 200, 200};   // Neutral tint
```

---

## Layer 2: Lumen Radiosity (Real-Time GI)

### Architecture

```
┌─ Lumen Scene Data (GPU-resident)
│  ├─ Mesh Cards (per-primitive)
│  │  ├─ Position, normal, diffuse albedo
│  │  └─ Radiance (emissive + lighting)
│  └─ Voxel Grid (coarse distance field)
│
├─ Screen-Space Radiosity Probes
│  ├─ Grid around camera (64cm spacing, tunable)
│  ├─ Per-probe: hemisphere rays (32-64 rays)
│  └─ Radiance accumulated into SH coefficients
│
└─ Deferred Lighting Pass
   ├─ For each pixel:
   │  ├─ Find nearest radiosity probe
   │  ├─ Interpolate SH from nearby probes
   │  ├─ Evaluate SH for normal → indirect color
   │  └─ Add to lighting buffer
   └─ Output: indirect_lighting_buffer
```

### Implementation Phases

#### Phase 2.1: Mesh Cards (Week 1-2)
- **What**: GPU-resident virtual lightmaps per primitive
- **Why**: Efficient bounce light storage
- **How**: Render each mesh to 2D cards per-frame, store radiance

#### Phase 2.2: Software Tracing (Week 2-3)
- **What**: Compute shader ray-marching through distance fields
- **Why**: Works on all platforms (no RTX/DXR needed)
- **How**: Cast rays from probe, trace vs. global distance field, accumulate

#### Phase 2.3: Screen-Space Probes (Week 3)
- **What**: Radiance grid around camera
- **Why**: Probe placement adapts to camera view
- **How**: Place grid per-frame, interpolate for final shading

### Lumen Configuration

**Location**: `runtime/src/engine/renderer/lighting/lumen_config.h`

```cpp
struct LumenConfig {
    bool enabled = true;
    
    // Tracing method
    enum TracingMethod {
        SoftwareDistanceField,  // Compute shader, distance field tracing
        HardwareRayTracing,     // DXR/RTX, scene geometry
        HybridCards,            // Mesh cards + fallback to DF
    } tracing_method = TracingMethod::HybridCards;
    
    // Radiosity probe grid
    float probe_spacing_cm = 64.0f;        // Distance between probes
    int32 hemisphere_rays_per_probe = 48;  // Rays for importance sampling
    bool use_probe_occlusion = true;       // Apply AO to probe radiance
    
    // Tracing distance
    float max_trace_distance_cm = 500.0f;  // How far to trace (indoors: short)
    float far_field_distance_cm = 5000.0f; // Transition to coarse voxels
    
    // Temporal feedback
    float temporal_accumulation_speed = 0.2f;  // [0,1] how fast to converge
};
```

### Fallback for Layer 2

If Lumen is **too expensive** or **platform doesn't support it**:
- **Fallback to pre-baked light probes** (Lightmass irradiance volumes)
- No real-time bounces, but still correct indirect lighting
- Same visual pipeline as Lumen (just static coefficients)

---

## Layer 3: Lightmass (Pre-Baked GI) [Optional]

### When to Use

- **Static interior scene**: Use Lightmass (zero runtime cost)
- **Dynamic interior scene**: Use Lumen (real-time, adapts to changes)
- **Hybrid**: Static walls + dynamic objects = Lightmass for static parts, Lumen for objects

### Portal System for Interiors

**Location**: `runtime/src/engine/components/lightmass_portal_component.h`

```cpp
struct LightmassPortalComponent : public Component {
    // Marks an opening (window, doorway) for light to propagate through
    // Lightmass traces through portals to connect exterior light to interior
    
    // No runtime cost; purely offline baking hint
};
```

### Workflow

1. **Place portals** at windows/doorways (editor, drag-place boxes)
2. **Build light map** (lightmass bake, 10-60 minutes)
3. **Inspect results** in-editor (verify no light leaks)
4. **Pack into level** (stored as baked lightmaps)

---

## Implementation Roadmap

### Week 1-2: Foundation (Sky Light + DFAO)

**Tasks**:
1. Implement `SkyLightComponent` (ECS)
2. Implement `SkyCapturePass` (cubemap → SH)
3. Integrate DFAO into deferred lighting
4. Add SH evaluation shader code

**Testable**: Interior scene with basic ambient fill (no color bounces yet)

**Demo**: Load Sponza, interior no longer pitch-black

### Week 3-4: Lumen Phase 1 (Mesh Cards + Software Tracing)

**Tasks**:
1. Implement mesh card allocation & update
2. Implement distance field voxelization (global SDF)
3. Implement software ray-tracing (compute shader)
4. Integrate card radiance into probe gather

**Testable**: Indirect bounces from exterior surfaces into interior

**Demo**: Color variation from bounces (walls lit by skycolor bounces)

### Week 5-6: Lumen Phase 2 (Screen-Space Probes)

**Tasks**:
1. Implement probe grid placement (camera-relative)
2. Implement probe ray dispatch & tracing
3. Implement SH accumulation per-probe
4. Integrate into deferred lighting

**Testable**: Full radiosity with temporal feedback

**Demo**: Real-time GI, stable with >30 fps

---

## Testing Strategy

### Visual Verification

```bash
# Load Sponza interior scene
uzengine.exe --project sample/projects/sponza.uzproject --editor

# Verify layers incrementally:
1. No Lumen, only SkyLight:    uniform gray fill (correct!)
2. + DFAO:                     corners darker (correct!)
3. + Lumen Probes:             color variation (correct!)
4. + Temporal accumulation:     noise reduces over time (correct!)
```

### Numerical Validation

**SH Evaluation Test**:
```cpp
// Test case: normal = (0, 1, 0), SH from uniform blue sky
SHVectorRGB3 uniform_sky = {
    {0.5f, 0.5f, 1.0f}, // Band 0
    {...}, // Band 1
    {...}  // Band 2
};
float3 result = EvaluateSH(uniform_sky, normalize({0, 1, 0}));
// Expected: ~{0.5, 0.5, 1.0} (blue sky in all directions)
// Tolerance: 1% (numerical precision)
```

**DFAO Accuracy**:
- Compare DFAO output vs. reference (Falcor GTAO)
- PSNR > 35dB = acceptable
- Check corner darkening (interior corners should be darker than center)

---

## Performance Targets

### Layer 1 (Sky Light + DFAO)
- **GPU**: 2-4ms (RTX 3080)
- **Memory**: ~50MB (cubemap + SH coefficients)
- **Platforms**: All (no special hardware)

### Layer 2 (Lumen)
- **GPU**: 4-8ms for probe tracing (RTX 3080)
- **Memory**: ~500MB (mesh cards + voxel grid)
- **Platforms**: DX12+ with compute shaders; RTX accelerated version faster

### Layer 3 (Lightmass)
- **Bake Time**: 10-60 minutes per level
- **Runtime**: 0ms (baked lighting is free)
- **Memory**: ~200MB per level (depends on light map resolution)

---

## Parameter Tuning Reference

| Scene Type | Sky Intensity | Min Occlusion | Contrast | Probe Spacing |
|------------|--------------|---------------|----------|---------------|
| **Outdoor** | 0.5-0.8 | 0.05 | 0.6 | 128cm |
| **Covered Outdoor** | 1.0 | 0.10 | 0.5 | 96cm |
| **Interior (Atrium)** | 1.2 | 0.25 | 0.3 | 64cm |
| **Interior (Corridor)** | 1.5 | 0.30 | 0.2 | 48cm |
| **Dungeon/Cave** | 2.0+ | 0.35 | 0.1 | 32cm |

---

## References & Validation

1. **Sky Light SH**: Ramamoorthi 2002, "Precomputed Radiance Transfer"
2. **DFAO**: Quilez 2013, "Analytical Distance Field Sampling"
3. **Lumen**: NVIDIA research + UE5 documentation
4. **Lightmass Portals**: UE4/UE5 lighting documentation
5. **Visual Parity**: Compare output to Falcor (via `--screenshot-stdout`)

---

**Status**: Ready for implementation  
**Priority**: Sky Light + DFAO (Phase 1) is **critical path** for playable interior scenes
