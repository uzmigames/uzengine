# Architecture Risk Fixes

## Why

The 2026-03-14 gap analysis identified 6 architectural risks that will block Phase 3 features if not addressed. These are structural fixes to existing systems that prevent scaling. Fixing them now avoids expensive refactors later. P1 — must complete before Wave 2 features.

## What Changes

### Risk 1: Root Signature Saturation
- Graphics root signature is FULL (64/64 DWORDs). Adding GBufferD, probes, post-process UBOs will exceed limits.
- Fix: Consolidate per-material parameters into bindless dynamic table, reclaim root slots.

### Risk 2: CPU Light Culling Bottleneck
- Deferred lighting iterates all lights on CPU. Does not scale beyond 32 visible lights.
- Fix: Prepare light data as GPU StructuredBuffer. Prerequisite for 3.32.0 clustered.

### Risk 3: Fixed 8-Light Shadow Limit
- ShadowManager uses fixed array. Silent failure when exceeded.
- Fix: Dynamic shadow atlas with priority-sorted assignment.

### Risk 4: Scattered SceneEnvironment State
- SkyLight, Fog, PostProcess, Tonemapper are separate with no unified versioning.
- Fix: Consolidate into single SceneEnvironmentState struct with version field.

### Risk 5: MSAA Incompatibility Documentation
- Deferred pipeline incompatible with MSAA but not documented.
- Fix: Document explicitly that MSAA is unsupported. TAA is the only AA path.

## Impact
- Affected specs: `docs/architecture/render-pipeline.md`, `docs/engine/12-rendering/13-register-binding-convention.md`
- Affected code: `runtime/engine/renderer/core/root_slots.h`, `runtime/engine/renderer/core/scene_lighting.*`, `runtime/engine/renderer/shadows/`
- Breaking change: YES — root signature layout change requires PSO rebuild
- User benefit: Unblocks all Wave 2+ features without future refactoring
- Enables: Reflection probes, clustered lighting, PostProcess volumes, dynamic shadow count
