# Proposal: phase0_falcor-flip-quality-metric

## Why
UzEngine has zero automated visual regression testing. The only quality signal is "does it look wrong to a human?" This is insufficient for CI and multi-backend parity work — DX12 vs Vulkan differences, shadow bug regressions, and post-process quality regressions all go undetected. Falcor ships a full FLIP perceptual quality pass. FLIP (Andersson et al. 2020) combines Hunt-adjusted color difference in HyAB color space with Butteraugli-style edge detection and spatial filtering to produce a single perceptual error map that matches human visual sensitivity. Mean FLIP scores below 0.02 are imperceptible; above 0.1 are clearly visible. This enables automated CI gating on visual quality.

## What Changes
- New compute shader `runtime/shaders/runtime/postprocess/flip_cs.hlsl` — FLIP algorithm: input two textures (reference + test), output per-pixel error heatmap (hot = high error) and side-channel UAV storing mean FLIP score. @ref Andersson et al. 2020 "FLIP: A Difference Evaluator for Alternating Images"
- New C++ `FlipPass` class — render graph pass wrapping the compute shader; inputs: reference texture SRV, test texture SRV; outputs: error heatmap UAV, `float meanFlip` readback
- CLI integration: `--flip <ref.png> <test.png>` computes FLIP between two files, writes `--flip-output <heatmap.png>` and prints JSON `{"mean_flip": 0.02, "max_flip": 0.15, "pass": true}` to stdout
- Configurable threshold: `--flip-threshold 0.05` — exit code 1 if mean FLIP exceeds threshold (enables CI pass/fail)
- The same `FlipPass` can be invoked in-engine to compare DX12 vs Vulkan output during backend parity testing

## Impact
- Affected specs: `docs/specs/screen-space-effects.md`, `docs/specs/debug-profiling.md`
- Affected code: new `flip_pass.h/.cpp`, new `flip_cs.hlsl`, `application_args.cpp` (CLI parsing), demo build runner
- Breaking change: NO — purely additive; no existing pass is modified
- User benefit: CI can gate on visual quality; multi-backend parity regressions are detected automatically; before/after comparisons for any rendering change produce a quantitative score instead of eyeball comparison
