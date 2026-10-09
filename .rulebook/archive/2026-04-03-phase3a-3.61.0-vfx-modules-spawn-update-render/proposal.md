# VFX Modules — Spawn, Update, Render Module System

## Summary

Implement the modular VFX module system that enables composable particle effects. Modules are reusable behavior units (Spawn, Update, Decay, SpriteRender, MeshRender, Turbulence, Constraint) that chain together to define complex effects. Each module contributes HLSL code snippets that are composed into a single compute shader per emitter. This is UzEngine's equivalent of Niagara's Module/Script system.

## Why

The GPU particle core (3.60.0) provides raw particle simulation with hardcoded gravity+damping. The module system makes VFX authoring flexible and extensible:

1. **Composability**: Artists/designers mix modules to create effects (e.g., Spawn + Gravity + Turbulence + SpriteRender + Decay = fire effect).
2. **Extensibility**: Game-specific modules (Wind, Attraction, Vortex) register via ModuleRegistry without modifying engine code.
3. **Shader composition**: Modules contribute HLSL snippets that compile into a single optimized compute shader, avoiding per-module dispatch overhead.
4. **Parameter exposure**: Each module exposes parameters (float, Vec3, Vec4, curves) tweakable at runtime via Lua/C++ without reloading assets.

## UE5 Niagara Reference (Clean-Room)

### Key Patterns to Adapt

1. **FNiagaraScriptBase → HLSL Compilation**: Niagara modules are visual-script graphs that compile to HLSL. UzEngine uses simpler JSON config + predefined HLSL snippets per module type. Less flexible but much simpler and LLM-friendly.

2. **Module Execution Order**: Niagara evaluates modules in user-defined order within a simulation stage. UzEngine mirrors this with the `module_order` array in .uzvfx files.

3. **Parameter Binding (FNiagaraParameterStore)**: Niagara has a rich parameter store with bindings between system/emitter/module levels. UzEngine uses a flat `std::unordered_map<std::string, Vec4>` per emitter, simpler but sufficient for Phase 3.

4. **Curve Sampling on GPU**: Niagara bakes curves into lookup textures (1D textures with linear filtering). UzEngine will do the same — curves are baked into small GPU textures sampled in compute shader.

5. **Custom Modules**: Niagara allows C++ and visual-script modules. UzEngine allows C++ modules via `Module` virtual interface + `ModuleRegistry`.

## Impact

- **New files**: `runtime/engine/vfx/modules/` — 8+ module implementations
- **New shaders**: Per-module HLSL snippets composed into compute shaders
- **New asset support**: `.uzcurve` JSON curve format (GPU-baked 1D lookup textures)
- **Module registry**: Runtime module type registration for extensibility

## Dependencies

- GPU Particle Core (3.60.0) — particle buffers, compute dispatch, render graph
- RHI texture creation (✅ exists) — for curve lookup textures

## Acceptance Criteria

1. Module base class with Execute(), GetShaderCode(), GetParameters() virtual interface
2. SpawnModule: configurable rate, lifespan range, velocity, spread angle, spawn shape (point, sphere, box)
3. UpdateModule: gravity, drag/damping, angular velocity
4. DecayModule: kills particles where age >= lifespan, recycles to free list
5. SpriteRenderModule: billboard rendering with size_over_life and color_over_life curves
6. TurbulenceModule: 3D Perlin noise-based velocity perturbation with frequency and strength
7. ConstraintModule: AABB bounding box clamping
8. ModuleRegistry: Register<T>(name), Create(name) factory pattern
9. Curve system: .uzcurve JSON format, baked to 1D GPU texture, sampled in compute shader with linear filtering
10. Shader composition: modules' HLSL snippets concatenated into single compute shader per emitter
11. Demo: fire effect (Spawn + Update + Turbulence + SpriteRender + Decay) with visible turbulence
12. All unit tests pass, 95%+ coverage
