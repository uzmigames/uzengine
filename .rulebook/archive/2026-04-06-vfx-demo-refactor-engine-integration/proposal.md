# Proposal: vfx-demo-refactor-engine-integration

## Why

The 13 VFX demos merged from PR #6 contain ~6500 lines of engine logic
inlined in demo main.cpp files. Each demo duplicates a mock RHI device
(~80 lines x 10 = 800 lines), manually manages VFXSystem lifecycle,
hardcodes ModuleCompositor configurations, and includes validation logic
that belongs in unit tests. This violates the "demos are thin consumers"
rule established by the triangle demo pattern.

## What Changes

### Phase 1: Infrastructure
- Extract shared Mock RHI device to `runtime/demos/common/mock_device.h`
- Integrate VFXSystem lifecycle into SceneRenderer (Init/Tick/Shutdown)
- Create `.uzfx` asset format for declarative emitter/module configuration

### Phase 2: Demo Conversion
- Convert 12 VFX demos from headless-mock to DemoApplication-based visual demos
- Extract hardcoded module stacks into `.uzfx` asset files per demo
- Load assets via VFS from demo-local `assets/` directories

### Phase 3: Test Extraction
- Move validation/assertion logic from demos to `tests/engine/vfx/`
- Ensure test coverage matches what demos previously validated

## Impact
- Affected specs: vfx-system, particle-system, scene-system
- Affected code: runtime/demos/*, engine/renderer/scene/scene_renderer*, engine/vfx/*
- Breaking change: NO (engine API additions only, demos rewritten)
- User benefit: VFX demos become visual showcases; VFX system properly integrated in engine lifecycle
