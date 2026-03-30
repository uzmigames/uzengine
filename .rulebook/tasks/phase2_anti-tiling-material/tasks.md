## 1. Implementation

### 1.1 HLSL Anti-Tiling Library
- [ ] 1.1.1 Create `runtime/shaders/runtime/include/anti_tiling.hlsli` with procedural hash-based noise functions (value noise, smooth noise) operating in world space.
- [ ] 1.1.2 Implement `SampleAntiTiled()` function: dual-sample base texture at two scales with optional rotation, blend via world-space macro noise mask.
- [ ] 1.1.3 Implement dark spots / dirt breakup layer: second lower-frequency noise multiplied into albedo and optionally roughness.
- [ ] 1.1.4 Implement microdetail normal overlay that blends independently from the macro variation.
- [ ] 1.1.5 Add `@ref` citations for the technique (Iq 2019 "Texture Repetition" / Stochastic sampling literature).

### 1.2 Parameter Block
- [ ] 1.2.1 Define `AntiTilingParams` struct (HLSL + C++ mirror) with all 9 parameters: baseTiling, variationTiling, variationStrength, macroNoiseScale, macroNoiseContrast, darkSpotsScale, darkSpotsStrength, bumpStrength, rotationAngle.
- [ ] 1.2.2 Integrate parameter block into the material system — extend `PBRMaterial` or add a secondary StructuredBuffer for extended material features.
- [ ] 1.2.3 Set published/reasonable defaults for all parameters.

### 1.3 Shader Variant Integration
- [ ] 1.3.1 Add `ANTI_TILING` shader permutation flag to the variant system.
- [ ] 1.3.2 Wire `#ifdef ANTI_TILING` path in `gbuffer_ps.hlsl` to call `SampleAntiTiled()` for albedo, normal, roughness sampling.
- [ ] 1.3.3 Wire the same path in `mesh_ps.hlsl` for forward-rendered objects.

### 1.4 CPU-Side Math Mirror
- [ ] 1.4.1 Create `runtime/engine/renderer/material/anti_tiling_math.h` with C++ equivalents of the noise and blending functions (literal translation from HLSL).

## 2. Testing

### 2.1 CPU Math Parity (Tier 1)
- [ ] 2.1.1 Create `runtime/tests/engine/renderer/test_anti_tiling_parity.cpp` with known-value tests for the noise functions.
- [ ] 2.1.2 Test blending weight computation at boundary inputs (0, 1, extreme scales).
- [ ] 2.1.3 Test rotation matrix generation for the variation sample offset.
- [ ] 2.1.4 Property test: blending weights always in [0, 1], dark spots multiplier always in [darkSpotsStrength, 1].

### 2.2 Visual Regression (Tier 2)
- [ ] 2.2.1 Create or extend a demo scene with a large planar surface (10x10m+) using a single tiled PBR texture.
- [ ] 2.2.2 Capture golden reference screenshots with anti-tiling ON at default parameters.
- [ ] 2.2.3 Capture comparison: anti-tiling OFF vs ON at multiple camera distances (near, mid, far).
- [ ] 2.2.4 Verify PSNR stability across frames (temporal coherence — no shimmer or flicker).

### 2.3 Performance
- [ ] 2.3.1 Benchmark anti-tiling ON vs OFF: measure GPU ms delta on a terrain-sized surface. Target: < 0.3ms additional cost at 1080p.
- [ ] 2.3.2 Verify total texture lookups per pixel stays at 3 or fewer (base + variation + noise can be procedural).

### 2.4 Coverage
- [ ] 2.4.1 Verify 95%+ coverage on all new C++ code (`anti_tiling_math.h`, parameter integration).

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/shader-library.md` with `anti_tiling.hlsli` section and function signatures.
- [ ] 3.2 Update `docs/specs/rendering/shader-variants.md` with the `ANTI_TILING` permutation entry.
- [ ] 3.3 Add shader authoring guide entry in `docs/guides/shader-authoring.md` explaining how to enable anti-tiling on a material.
- [ ] 3.4 Add `@ref` citations to the HLSL file header referencing the published technique.
