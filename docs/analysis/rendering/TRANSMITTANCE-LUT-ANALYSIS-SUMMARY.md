<!-- doc-meta
title: Transmittance LUT Analysis Summary — UE5 to UzEngine
author: Architecture Team
date: 2026-03-15
tags: [transmittance-lut, architecture, analysis, sky-atmosphere]
status: complete
version: 1.0
-->

# Transmittance LUT Analysis Summary

This document summarizes the complete analysis of UE5's transmittance LUT system and provides guidance for implementing a similar system in UzEngine.

## Documents Generated

Three analysis documents have been created:

### 1. **ue5-sky-atmosphere-transmittance-lut.md** (Main Analysis)
- **Purpose**: Complete technical analysis of UE5's transmittance LUT system
- **Content**:
  - Problem statement and why LUT precomputation is needed
  - Detailed LUT parameterization (UV mapping math)
  - Texture specifications (dimensions, formats)
  - Complete shader code (compute + lookup)
  - Dispatch configuration
  - Shader parameters and constant buffers
  - Runtime usage patterns
  - Architecture pattern explanation
  - Performance characteristics
  - UzEngine adaptation guidance

### 2. **ue5-transmittance-lut-hlsl-snippets.md** (Code Reference)
- **Purpose**: Direct copy-paste ready HLSL code from UE5 source
- **Content**:
  - Complete UV mapping functions (decode + encode)
  - Full compute shader kernel
  - Runtime lookup functions
  - Helper structures and constant buffer layouts
  - Optional sub-texel adjustment code
  - C++ dispatcher example
  - Porting checklist

### 3. **ue5-transmittance-lut-porting.md** (Implementation Guide)
- **Purpose**: Step-by-step guide for porting to UzEngine
- **Content**:
  - How to adapt HLSL code for DX12-native architecture
  - Simplified C++ texture management (no render graph)
  - Integration into UzEngine's sky renderer
  - Configuration via CVars
  - Performance optimization tips
  - Complete implementation checklist

## Key Findings

### What is Transmittance?

**Transmittance** = the fraction of light that reaches an observer through the atmosphere without being absorbed or scattered.

Example: The sun appears orange/red at sunset because shorter wavelengths (blue) are scattered more, leaving only longer wavelengths (red) to reach the observer. The transmittance along the sun ray is lower at sunset.

### Why Use a LUT?

Computing transmittance on-demand per pixel is expensive:
- Requires ray-marching through atmosphere layers (10-20 samples per ray)
- Must be evaluated for every pixel + every view
- Can become a bottleneck in complex scenes

**Solution**: Precompute transmittance for a 2D grid of (height, zenith_angle) pairs and store in a texture. Lookup is O(1) at runtime.

### LUT Dimensions

- **Default size**: 256 (width) × 64 (height) = 16,384 texels
- **Format**: R32G32B32_FLOAT (12 bytes per pixel = 192 KB total)
- **Optional small format**: R8G8B8A8 (4 bytes per pixel = 64 KB) for memory-constrained platforms

### UV Mapping (Non-Linear)

The LUT uses the **Bruneton 2017** parameterization:
- **UV.x** (horizontal): Normalized distance to atmosphere boundary
  - Maps to view angle relative to horizon
  - More texels near horizon where transmittance changes rapidly
- **UV.y** (vertical): Normalized height above planet
  - Maps linearly from BottomRadius to TopRadius

This non-linear mapping ensures maximum fidelity in regions where transmittance changes most (near the horizon).

### Computation

- **Method**: Compute shader with 8×8 thread groups
- **Dispatch**: 32×8 groups = 16,384 threads (one per texel)
- **Per-texel cost**: Single ray-march with 10 samples
- **Precomputation time**: ~10-50 ms on modern GPU
- **Frequency**: Once per scene (stored in texture pool)

### Runtime Cost

Each transmittance lookup:
1. Encode (height, zenith_angle) → UV coordinates
2. Sample texture with linear interpolation
- **Total**: ~2 GPU cycles per lookup

### Key Technical Decisions in UE5

1. **Why no alpha in format?** Mobile ES3.1 cannot write to 3-component UAVs; need RGB**A**.
2. **Why linear sampling?** Smooth interpolation of precomputed values with sub-texel precision.
3. **Why precompute once?** Atmosphere parameters change rarely; transmittance is scene-static.
4. **Why this parameterization?** Bruneton's scheme minimizes visual artifacts by concentrating detail near horizon.

## Porting to UzEngine

### What to Copy Directly

1. **UV mapping functions** — Pure math, no dependencies:
   - `getTransmittanceLutUvs()` (encode parameters → UV)
   - `fromTransmittanceLutUVs()` (decode UV → parameters)

2. **Compute shader kernel structure** — Same algorithm:
   - Decode UV to parameters
   - Build ray
   - Ray-march to compute optical depth
   - Convert to transmittance via `exp(-optical_depth)`
   - Write to UAV

3. **Texture specifications** — Same hardware requirements:
   - 256×64 dimensions
   - R32G32B32_FLOAT format
   - UAV + SRV descriptors

### What to Adapt

1. **Ray marching implementation** — UE5's `IntegrateSingleScatteredLuminance()` is complex:
   - You'll need to adapt to UzEngine's atmospheric integration function
   - Should compute optical depth (sum of extinction coefficients along ray)

2. **DX12 integration** — UzEngine has no render graph (RDG):
   - Replace FRDGTextureRef with ID3D12Resource*
   - Manual UAV/SRV descriptor allocation
   - Manual resource state transitions
   - Manual PSO/root signature management

3. **Parameter passing** — Simplified constant buffers:
   - Atmosphere parameters (Rayleigh, Mie, Ozone coefficients)
   - LUT metadata (size + inverse size)
   - Both in register(b0), register(b1)

### Implementation Path

1. **Phase 1**: Copy UV mapping functions + compute shader skeleton
2. **Phase 2**: Integrate with UzEngine's atmospheric ray-march function
3. **Phase 3**: Create DX12 texture/UAV/SRV resources
4. **Phase 4**: Dispatch compute shader once per scene
5. **Phase 5**: Add runtime lookup in sky pixel shader
6. **Phase 6**: Test and optimize

### Integration Points

**Precomputation** (once per scene):
- `SkyAtmosphere::Init()` → create texture
- `SceneRenderer::RenderScene()` → dispatch compute shader
- Store result in `SkyAtmosphere.TransmittanceLutTexture`

**Runtime** (every frame, every pixel):
- Sky pixel shader: `GetTransmittance(ViewHeight, ViewZenithCosAngle, ...)`
- Sample `TransmittanceLutTexture` with encoded UV
- Multiply sky color by transmittance value

## Critical Success Factors

### Testing Strategy

1. **White transmittance**: Disable real computation, return 1.0 (no extinction)
   - Sky should appear fully bright
   - Sun disk should appear at full brightness

2. **Single wavelength**: Test with one color channel
   - Red: R=1, G=0, B=0
   - Green: R=0, G=1, B=0
   - Blue: R=0, G=0, B=1
   - Verify transmittance LUT shows correct falloff per wavelength

3. **Horizon test**: Sky transmittance should darken significantly at horizon
   - At zenith: transmittance ≈ (1, 1, 1) or close to white
   - At horizon: transmittance ≈ (0.1, 0.5, 0.9) or reddish

4. **Sun disk test**: With atmosphere, sun should appear orange/red at sunset
   - Without transmittance: sun appears white
   - With transmittance: sun darkens and reddens as it approaches horizon

### Validation Checklist

- [ ] Precomputation completes without errors
- [ ] Transmittance LUT texture created with correct format and dimensions
- [ ] Compute shader compiles and dispatches successfully
- [ ] White transmittance test shows fully bright sky
- [ ] Single-channel test shows correct falloff per wavelength
- [ ] Horizon test shows darkening toward horizon
- [ ] Sun disk test shows orange/red sun at sunset
- [ ] Performance: precomputation < 50 ms
- [ ] Performance: runtime lookup cost < 0.5% frame time
- [ ] Memory: LUT texture footprint ≈ 200 KB
- [ ] No visual artifacts at LUT boundaries

## References

### UE5 Source Files

1. **W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf**
   - Lines 1099-1138: `RenderTransmittanceLutCS` compute shader
   - Lines 207-222: UV mapping function wrappers
   - Lines 346-356: `GetTransmittance()` lookup

2. **W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphereCommon.ush**
   - Lines 156-172: `fromTransmittanceLutUVs()` (decode)
   - Lines 175-192: `getTransmittanceLutUvs()` (encode)
   - Lines 233-254: `GetAtmosphereTransmittance()` integration

3. **W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.cpp**
   - Lines 151-174: CVar definitions
   - Lines 1148-1164: Format selection logic
   - Lines 1166-1208: Texture creation
   - Lines 1364-1379: Compute dispatch
   - Lines 791-815: Shader class definition

### Academic References

- **Bruneton et al. (2017)**: "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
  - Paper: https://hal.science/hal-01985130
  - Reference implementation: https://github.com/ebruneton/precomputed_atmospheric_scattering
  - Explains the non-linear parameterization and why it minimizes artifacts

### Related UzEngine Docs

Once transmittance LUT is implemented:
- Update `docs/specs/rendering/sky-atmosphere.md` with new capability
- Link from `docs/architecture/render-pipeline.md` (transmittance LUT as pre-pass)
- Add Lua API binding for querying transmittance values (if needed)

## Timeline Estimate

| Phase | Task | Effort | Est. Duration |
|-------|------|--------|----------------|
| 1 | Copy + adapt HLSL code | Low | 2 hours |
| 2 | Integrate ray-march function | Medium | 4 hours |
| 3 | DX12 texture/UAV/SRV setup | Medium | 3 hours |
| 4 | Compute dispatch + state transitions | Medium | 2 hours |
| 5 | Runtime lookup integration | Low | 2 hours |
| 6 | Testing + validation + performance | Medium | 4 hours |
| **Total** | | | **17 hours** |

## Risk Assessment

### Low Risk

- UV mapping functions (pure math, proven in UE5)
- Compute shader kernel structure (straightforward algorithm)
- Texture format/dimensions (standard DX12)

### Medium Risk

- Atmospheric ray-march integration (depends on UzEngine's atmosphere model)
- DX12 state management (manual vs. render graph)
- Parameter constant buffers (must match shader layout)

### Mitigation

1. Start with white transmittance (no real computation) to validate plumbing
2. Compare transmittance LUT visual output against UE5 (if possible)
3. Add debug visualization mode: display LUT texture on-screen
4. Profile each stage (dispatch time, texture access patterns)

## Next Steps

1. **Review** these three analysis documents
2. **Implement Phase 1**: Copy UV mapping functions, create skeleton compute shader
3. **Implement Phase 2**: Integrate with UzEngine's atmospheric sampling
4. **Create spec**: `docs/specs/rendering/transmittance-lut.md` (similar to ue5-sky-atmosphere-transmittance-lut.md)
5. **Write tests**: Validate precomputation output visually + numerically
6. **Document** integration in architecture docs
7. **Release**: Add to `SkyAtmosphereComponent` spec as required feature

