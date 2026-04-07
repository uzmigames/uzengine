## 1. Implementation

### 1.1 HLSL Anisotropic BRDF
- [x] 1.1.1 Added `D_GGXAniso(ax, ay, NoH, XoH, YoH)` to brdf.hlsli. Cited Burley 2012 + Walter 2007.
- [x] 1.1.2 Added `V_SmithGGXCorrelated_Aniso(ax, ay, NoV, NoL, XoV, XoL, YoV, YoL)` to brdf.hlsli. Cited Heitz 2014 + Filament.
- [x] 1.1.3 Added `RemapAnisotropy(roughness, anisotropy) -> float2(ax, ay)` to brdf.hlsli. Cited Burley 2012 + KHR_materials_anisotropy.
- [ ] 1.1.4 Add `SHADING_MODEL_ANISOTROPIC` case to `standard_material.hlsli` dispatch that calls aniso D and V functions.

### 1.2 GBuffer Anisotropy Packing
- [ ] 1.2.1 Pack anisotropy direction (tangent projected to screen space) + strength into GBuffer. Use existing spare channels or add a new GBuffer target if needed.
- [ ] 1.2.2 Unpack anisotropy direction in `deferred_lighting_ps.hlsl` and reconstruct tangent/bitangent for the aniso BRDF evaluation.

### 1.3 Material System
- [ ] 1.3.1 Add `float anisotropy = 0.0f` and `float anisotropyRotation = 0.0f` to `PBRMaterial` struct.
- [ ] 1.3.2 Add anisotropy texture slot (optional) for per-pixel anisotropy direction map.
- [ ] 1.3.3 Upload anisotropy params to material StructuredBuffer.

### 1.4 C++ Math Mirror
- [x] 1.4.1 Added `D_GGXAniso()`, `V_SmithGGXCorrelated_Aniso()`, `RemapAnisotropy()` to brdf_math.h. Literal translation from HLSL.

## 2. Testing

### 2.1 CPU Math Parity (Tier 1)
- [ ] 2.1.1 Known-value test: `D_GGXAniso(0.3, 0.1, 0.8, 0.5, 0.3)` matches hand-computed result within 1e-4.
- [ ] 2.1.2 Property test: D_GGXAniso reduces to D_GGX when ax == ay (isotropic case).
- [ ] 2.1.3 Boundary test: ax=0 or ay=0 edge cases (degenerate anisotropy).
- [ ] 2.1.4 Property test: V_SmithGGXCorrelated_Aniso always positive for valid inputs.

### 2.2 Visual Regression (Tier 2)
- [ ] 2.2.1 Create test material with high anisotropy (0.8) on a sphere — verify elongated highlight along tangent direction.
- [ ] 2.2.2 Capture screenshot and compare against Falcor aniso sphere reference.
- [ ] 2.2.3 Verify isotropic materials (anisotropy=0) produce identical output to current renderer (PSNR >= 40 dB).

### 2.3 Coverage
- [ ] 2.3.1 Verify 95%+ coverage on all new C++ code.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/shader-library.md` with anisotropic BRDF function signatures.
- [ ] 3.2 Update `docs/specs/rendering/shader-variants.md` with `SHADING_MODEL_ANISOTROPIC` variant.
- [ ] 3.3 Add `@ref` citations: Burley 2012, Heitz 2014, KHR_materials_anisotropy spec.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
