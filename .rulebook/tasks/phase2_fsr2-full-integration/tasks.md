## 1. Implementation

### 1.1 SDK Integration
- [ ] 1.1.1 Add AMD FidelityFX FSR 2 SDK to `scripts/setup.ps1` as a thirdparty dependency.
- [ ] 1.1.2 Add FSR 2 include paths and library linking to `build.zig`.
- [ ] 1.1.3 Create `FfxFsr2Context` wrapper in `fsr2_upscaler.cpp` that manages the FSR 2 context lifecycle.

### 1.2 Upscaler Wiring
- [ ] 1.2.1 Implement `FSR2Upscaler::Init()` — create FSR 2 context with display resolution, render resolution, and flags.
- [ ] 1.2.2 Implement `FSR2Upscaler::Execute()` — dispatch FSR 2 with: color input (render res), depth (render res), motion vectors (render res), reactive mask, exposure value. Output to display resolution target.
- [ ] 1.2.3 Wire Halton jitter from the existing TAA jitter system to FSR 2 camera jitter input.
- [ ] 1.2.4 Generate reactive mask: translucent/particle pixels marked as reactive to reduce ghosting.
- [ ] 1.2.5 Pass auto-exposure value to FSR 2 (or let FSR 2 compute its own — configurable).

### 1.3 Upscaler Selection
- [ ] 1.3.1 Add `FSR2` to `TemporalUpscaler` enum and wire it in the upscaler factory dispatch.
- [ ] 1.3.2 Add CVar `r.Upscaler` (0=None, 1=TAA, 2=TSR, 3=FSR2) and quality presets (Quality, Balanced, Performance, Ultra Performance).
- [ ] 1.3.3 Handle resolution changes: destroy/recreate FSR 2 context on display resolution change.

## 2. Testing

### 2.1 Visual Regression (Tier 2)
- [ ] 2.1.1 Capture native 4K reference, then FSR 2 Quality upscaled from 1080p. PSNR must be >= 30 dB.
- [ ] 2.1.2 Verify no ghosting on fast-moving objects (camera rotation, particle effects).
- [ ] 2.1.3 Verify sharpness is maintained on fine detail (text, thin geometry edges).
- [ ] 2.1.4 A/B comparison: FSR2 vs TAA vs TSR at same output resolution.

### 2.2 Performance
- [ ] 2.2.1 Benchmark FSR 2 overhead: target < 1.0ms GPU at 4K output.
- [ ] 2.2.2 Verify total frame time is lower with FSR 2 at 1080p→4K than native 4K rendering.

### 2.3 Coverage
- [ ] 2.3.1 Verify 95%+ coverage on new C++ code.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/renderer.md` with FSR 2 upscaler description and quality presets.
- [ ] 3.2 Update `docs/guides/getting-started.md` with FSR 2 setup instructions.
