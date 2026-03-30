## Session 1: FLIP Compute Shader

- [ ] 1.1 Create `runtime/shaders/runtime/postprocess/flip_cs.hlsl` — inputs: `Texture2D<float4> tReference`, `Texture2D<float4> tTest`, outputs: `RWTexture2D<float4> uErrorMap` (heatmap), `RWByteAddressBuffer uStats` (stores mean, max as floats). Entry point `main` with `[numthreads(8,8,1)]`
- [ ] 1.2 Implement Hunt-adjusted color conversion — convert sRGB input to Hunt-adjusted LMS color space (HTM matrix from FLIP paper). @ref Andersson et al. 2020 "FLIP: A Difference Evaluator for Alternating Images" — Table 1 constants
- [ ] 1.3 Implement HyAB color difference — `ColorDifference(ref_lms, test_lms)` in HyAB color space: split into achromatic (L) and chromatic (AB) components, combine as `|ΔL| + sqrt(Δa² + Δb²)`. @ref Andersson 2020 Eq. 4
- [ ] 1.4 Implement feature (edge) difference — Sobel gradient of luminance for both images, combine gradient magnitude difference with spatial filter. @ref Andersson 2020 Section 3.2
- [ ] 1.5 Combine color + feature difference into final per-pixel FLIP score with spatial pooling kernel (csf filter). Write heatmap via magma colormap LUT (7 control points). Atomic accumulate mean/max into `uStats` buffer
- [ ] 1.6 Build + verify shader compiles with DXC, no errors

## Session 2: FlipPass C++ + CLI Integration + Verification

- [ ] 2.1 Create `flip_pass.h/.cpp` — `FlipPass` class: `Init(device)`, `Evaluate(cmd, refSRV, testSRV) → FlipResult` where `FlipResult { RhiTextureHandle heatmap; float meanFlip; float maxFlip; bool pass; }`. Readback via staging buffer with fence wait.
- [ ] 2.2 CLI argument parsing in `application_args.cpp` — `--flip <ref>`, `--flip-ref <path>`, `--flip-test <path>`, `--flip-output <path>`, `--flip-threshold <float>` (default 0.05)
- [ ] 2.3 Headless CLI mode: when `--flip` is provided, load both images as textures, run `FlipPass::Evaluate`, save heatmap PNG to `--flip-output`, print `FLIP_JSON:{...}` to stdout, exit with code 0 (pass) or 1 (fail)
- [ ] 2.4 JSON output format: `{"mean_flip": 0.02, "max_flip": 0.15, "threshold": 0.05, "pass": true, "ref": "a.png", "test": "b.png"}`
- [ ] 2.5 Verification test: capture two screenshots from a demo at identical frame — run `--flip` on them → mean FLIP should be < 0.001 (identical). Then compare a shadowed vs unshadowed screenshot → mean FLIP should be > 0.05 (different). Build + verify both cases produce correct pass/fail
