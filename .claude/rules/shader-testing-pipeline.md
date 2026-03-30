# Shader Testing Pipeline — 3-Tier Validation (MANDATORY)

## Rule (applies to ALL agents touching shaders or render passes)

Every shader modification MUST be validated through this 3-tier pipeline before being marked complete.

## Tier 1: CPU Math Parity Tests (ALWAYS required)

Every HLSL function in `shaders/runtime/include/` with non-trivial math MUST have:
1. A C++ CPU-side equivalent in `engine/renderer/lighting/*_math.h`
2. A Google Test in `tests/engine/renderer/test_*_parity.cpp`
3. At minimum: one known-value test, one boundary test, one roundtrip/property test

### Mapping Table

| Shader Header | C++ Header | Test File |
|---------------|------------|-----------|
| `pbr_common.hlsli` | `brdf_math.h` | `test_brdf_parity.cpp`, `test_ggx_parity.cpp`, `test_fresnel_parity.cpp` |
| `sampling.hlsli` | `sampling_math.h` | `test_sampling_parity.cpp` |
| `octahedral.hlsli` | `encoding_math.h` | `test_encoding_parity.cpp` |
| `packing.hlsli` | `encoding_math.h` | `test_encoding_parity.cpp` |
| `math_util.hlsli` | `color_math.h` | `test_color_math_parity.cpp` |
| `sheen.hlsli` | `brdf_math.h` | `test_brdf_parity.cpp` |
| `film_tonemap.hlsli` | `brdf_math.h` | `test_brdf_parity.cpp` |
| `ibl.hlsli` | `sampling_math.h` | `test_sampling_parity.cpp` |

### When Adding a New Shader Function

1. Implement in HLSL (`shaders/runtime/include/*.hlsli`)
2. Add C++ equivalent (literal translation, same variable names, same operations)
3. Write test with known-value from the published paper
4. Run `zig build test` — MUST pass before committing

### Test Methodology

- **Known-value**: Hand-compute from the paper's equations. Tolerance: 1e-4 (stable formulas), 1e-3 (chained transcendentals)
- **Boundary**: Test at 0, 1, PI/2, extreme inputs
- **Property**: Non-negativity, symmetry, energy conservation where applicable
- **Roundtrip**: For encode/decode pairs (OctEncode/Decode, Pack/Unpack), verify recovery within quantization error

## Tier 2: Visual Regression (required for render pass changes)

After any shader or render pass change, run visual comparison:

```bash
# Capture screenshot
uzengine.exe --screenshot-stdout --screenshot-frame 5 --frames 8 \
  --project ./sample/projects/pbr_showcase.uzproject --editor

# Compare against golden reference
uzengine.exe --diff runtime/tests/golden/pbr_showcase/default.png <captured.png>
```

### Golden Reference Storage

```
runtime/tests/golden/
  pbr_showcase/
    default.png           # Default camera position
    manifest.json         # Camera params, resolution, PSNR threshold
  cornell_box/
    default.png
    manifest.json
```

### PSNR Thresholds

| PSNR (dB) | Verdict | Action |
|-----------|---------|--------|
| >= 40 | PASS | Visually identical |
| 30-40 | WARNING | Investigate — may be intentional |
| < 30 | FAIL | Visible regression — must fix |

### When to Update Golden Images

- After intentional visual changes (energy conservation fix, new tonemap operator, etc.)
- Run `scripts/visual_test.ps1 --update-golden`
- Commit new golden images WITH the shader change
- Document the visual difference in the commit message

## Tier 3: FLIP Perceptual Comparison (for A/B testing)

For comparing two rendering approaches (e.g., old vs new BRDF):

```bash
uzengine.exe --flip <reference.png> <test.png> --flip-output diff.png
```

| FLIP Mean | Verdict |
|-----------|---------|
| < 0.02 | Imperceptible |
| 0.02-0.05 | Minor — acceptable for quality improvements |
| > 0.10 | Visible — investigate |

## Agent Workflow — MANDATORY Steps

### Before Modifying Any Shader Function

1. Run `zig build test` — note current pass count
2. If modifying a function in `include/*.hlsli`, identify its C++ mirror
3. Read the existing tests for that function

### After Modifying a Shader Function

1. Update the C++ mirror to match the new HLSL exactly
2. Update/add tests for the changed behavior
3. Run `zig build test` — verify pass count same or higher
4. If render pass was changed, capture screenshot and compare PSNR
5. Report test results in the task checklist

### When Adding a New Shader Function

1. Implement in HLSL
2. Create C++ mirror (literal translation)
3. Write at least 3 tests (known-value, boundary, property)
4. Run `zig build test`
5. If the function affects visual output, capture before/after screenshots

## Tier 4: Falcor Reference Rendering (for ground truth validation)

NVIDIA Falcor (BSD licensed) serves as an external ground truth renderer. It runs via
Python (Mogwai) and can render the same glTF scenes with physically-based algorithms
validated by NVIDIA researchers.

### Setup (one-time)

```bash
# Clone Falcor (requires NVIDIA GPU + Windows SDK + Visual Studio)
git clone https://github.com/NVIDIAGameWorks/Falcor.git E:/Falcor
# Build (CMake + VS2022)
cd E:/Falcor && cmake --preset vs2022 && cmake --build build --config Release
```

### Test Script Location

`scripts/falcor_reference_test.py` — Python script that:
1. Launches Mogwai with a render graph script
2. Loads the same glTF scene used by UzEngine
3. Sets identical camera/light parameters
4. Captures reference frames at deterministic time steps
5. Compares UzEngine output vs Falcor output via PSNR/FLIP

### Render Graph for Reference Comparison

```python
# scripts/falcor/reference_pbr.py — Falcor render graph for PBR comparison
import falcor

def render_graph():
    g = RenderGraph("ReferencePBR")
    # Rasterized GBuffer + deferred lighting (matches UzEngine pipeline)
    g.addPass(createPass("GBufferRaster"), "GBuffer")
    g.addPass(createPass("ToneMapper"), "ToneMapper")
    g.markOutput("ToneMapper.dst")
    return g

m.loadScene("../../sample/scenes/pbr_showcase.gltf")
graph = render_graph()
m.addGraph(graph)
m.clock.pause()

# Capture at multiple frames for temporal stability
for frame in [1, 8, 16]:
    m.clock.frame = frame
    m.renderFrame()
    m.frameCapture.outputDir = "../../runtime/tests/golden/falcor_reference/"
    m.frameCapture.capture()
```

### What to Compare

| Area | UzEngine Source | Falcor Source | Comparison |
|------|----------------|---------------|------------|
| PBR BRDF | `pbr_common.hlsli` | `StandardBSDF.slang` | D_GGX, Fresnel, Smith at same angles |
| Tone mapping | `film_tonemap.hlsli` | `ToneMapper.slang` | ACES curve at same HDR input |
| IBL | `ibl.hlsli` + `prefilter_cs` | `EnvMapSampler.slang` | Split-sum at same roughness |
| AO | `gtao_cs.hlsl` | N/A (Falcor uses RT AO) | Skip — different technique |
| Shadows | `shadow_depth_vs + pcss` | N/A (Falcor uses RT shadows) | Skip — different technique |

### When to Run Falcor Reference Tests

- After major BRDF changes (new Fresnel, energy conservation, etc.)
- After tone mapping operator changes
- After IBL pipeline changes
- NOT needed for: post-FX tweaks, shadow changes, VFX, atmosphere (Falcor lacks these)

### Tolerance

Falcor and UzEngine use different pipelines (Slang vs HLSL, different render graphs).
Exact pixel match is NOT expected. The comparison validates **math correctness**, not pixel identity.

| PSNR vs Falcor | Verdict |
|----------------|---------|
| >= 35 dB | PASS — both implement the same published algorithm correctly |
| 25-35 dB | WARNING — may be artistic differences or minor implementation variants |
| < 25 dB | FAIL — likely a math bug in one of the renderers |

## FORBIDDEN

- Marking a shader task complete without running `zig build test`
- Adding a shader function without a C++ parity test
- Claiming "visual looks correct" without a screenshot comparison
- Modifying HLSL math without updating the C++ mirror
- Running Falcor tests on every minor change (expensive — reserve for BRDF/tonemap/IBL changes)
