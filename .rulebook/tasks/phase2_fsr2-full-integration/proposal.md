# Proposal: FSR 2.0 Full Integration (Phase 2)

## Why
FSR 1 (EASU + RCAS spatial upscaling) is implemented but FSR 2 (temporal upscaling with motion vectors) exists only as a partial stub. FSR 2 enables rendering at 1080p and upscaling to 4K with quality approaching native resolution — critical for GPU-limited scenarios. Godot 4 ships with FSR 2 fully integrated; UzEngine's FSR2Upscaler class exists but is not wired to the AMD FidelityFX SDK.

## What Changes

### New Systems
1. AMD FidelityFX FSR 2 SDK integration via thirdparty dependency
2. FSR2Upscaler full implementation: jitter, motion vectors, reactive mask, exposure input

### Refactoring
- fsr2_upscaler.cpp: wire to AMD SDK instead of stub implementation
- Temporal upscaler selection: add FSR2 to the TemporalUpscaler enum dispatch
- scripts/setup.ps1: add FSR 2 SDK download

## Impact
- Affected specs: docs/specs/rendering/renderer.md
- Affected code: runtime/engine/renderer/passes/postprocess/fsr2_upscaler.cpp/.h, scripts/setup.ps1
- Breaking change: NO — additive upscaler option
- User benefit: 1080p→4K upscaling with near-native quality on all GPUs (no NVIDIA requirement)
- Enables: Performance mode for lower-end hardware
