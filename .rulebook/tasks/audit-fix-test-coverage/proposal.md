# Proposal: Test Coverage Expansion

## Why
Internal audit (2026-03-13) found overall test coverage at ~30%, far below the 95% target.
Math tests are BROKEN (reference pre-split API), renderer passes have ZERO tests, and there
are no shader compilation tests. A regression in any of these systems would go undetected.

## What Changes
- Rewrite all math tests for the new split API (Vec3, Vec4, Quat, Mat4)
- Add renderer pass tests (GBuffer, Lighting, Shadow, PBR) via mock RHI
- Add shader compilation tests for all engine shaders
- Add missing core tests (paths, config, profiling)
- Build test infrastructure: DX12 test fixtures, mock RHI backend
- Fill RHI test gaps (swapchain, command list, PSO cache, destruction)

## Impact
- Affected specs: test infrastructure
- Affected code: runtime/tests/**
- Breaking change: NO
- User benefit: Confidence in code changes, regression detection, 95% coverage target
