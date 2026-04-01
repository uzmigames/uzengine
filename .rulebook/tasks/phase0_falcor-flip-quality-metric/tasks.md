## Session 1: FLIP Compute Shader

- [x] 1.1 Created `flip_cs.hlsl` — sRGB→linear→YCxCz→CSF→CIELab→Hunt→HyAB + Gaussian feature detection. Inputs: ref+test SRV, outputs: RGBA16F heatmap UAV + RWByteAddressBuffer stats. [numthreads(8,8,1)] entry `main`.
- [x] 1.2 Hunt-adjusted color: sRGB→linear→XYZ→CIELab→Hunt(L*0.01 scaling on a*,b*). All matrices from BT.709/D65 standard.
- [x] 1.3 HyAB color difference: `|ΔL| + sqrt(Δa² + Δb²)` in Hunt-adjusted CIELab space.
- [x] 1.4 Feature detection: Gaussian-weighted point (LoG) + edge (1st derivative) kernels, normalized, per Andersson 2020 Section 3.2.
- [x] 1.5 CSF spatial filter with per-channel Gaussian weights (abA, abRG, abBY from Table 1). Error redistribution with qc=0.7, pc=0.4, pt=0.95. Magma colormap (16-sample LUT). Atomic max via uint compare.
- [x] 1.6 Build passes — shader compiles, pipeline created, no DXC errors.

## Session 2: FlipPass C++ + CLI Integration + Verification

- [x] 2.1 Created `flip_pass.h/.cpp` — `FlipPass::Init(device, shaderMgr)`, `Evaluate(cmd, refSrv, testSrv, w, h, threshold) → FlipResult`. Lazy pipeline creation, reusable heatmap texture, staging buffer readback with WaitIdle.
- [ ] 2.2 CLI argument parsing in `application_args.cpp` — `--flip <ref>`, `--flip-ref <path>`, `--flip-test <path>`, `--flip-output <path>`, `--flip-threshold <float>` (default 0.05)
- [ ] 2.3 Headless CLI mode: when `--flip` is provided, load both images as textures, run `FlipPass::Evaluate`, save heatmap PNG to `--flip-output`, print `FLIP_JSON:{...}` to stdout, exit with code 0 (pass) or 1 (fail)
- [ ] 2.4 JSON output format: `{"mean_flip": 0.02, "max_flip": 0.15, "threshold": 0.05, "pass": true, "ref": "a.png", "test": "b.png"}`
- [ ] 2.5 Verification test: capture two screenshots from a demo at identical frame — run `--flip` on them → mean FLIP should be < 0.001 (identical). Then compare a shadowed vs unshadowed screenshot → mean FLIP should be > 0.05 (different). Build + verify both cases produce correct pass/fail
