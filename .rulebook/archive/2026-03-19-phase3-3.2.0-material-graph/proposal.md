# Proposal: Material Graph System (Phase 3)

## Why

UzEngine currently uses a fixed-function material model — each mesh gets a `Material` struct with texture paths (albedo, normal, roughness, metallic, AO, emissive) and scalar multipliers. This cannot express procedural patterns (checker grids, tri-planar mapping, noise, blend masks), material layering, or per-pixel parameter computation. The UE5 Basic Scene's floor uses `MI_ProcGrid` — a node-graph material with procedural checker, angle darkening, and water level masking — which is impossible to replicate in our current system.

A Material Graph compiler that transforms a DAG of nodes into HLSL code, compiles via DXC, and caches PSOs unlocks procedural materials, material layering, per-pixel parameter variation, and artist-friendly authoring.

This is a **runtime-only** milestone — the visual editor (Phase 5.6) comes later. This phase delivers the C++ graph representation, HLSL code generator, DXC compilation pipeline, and PSO cache integration.

## What Changes

### New Systems

1. **MaterialGraph** — DAG of MaterialNode objects with typed pins (float, float2, float3, float4, texture). Serializable to `.uzmat` JSON.
2. **MaterialNode library** — Built-in node types: constants, math (Add/Multiply/Lerp/Clamp/Pow/Abs/Saturate), texture sampling, procedural patterns (Checker, Noise, Gradient), coordinate nodes (WorldPosition, VertexNormal, TexCoord, TriPlanar), utility (Mask, Append, ComponentMask, OneMinus, Fresnel).
3. **MaterialCompiler** — Walks the graph topologically, emits HLSL pixel shader body. Dead-code elimination. Generates unique shader source keyed by graph content hash.
4. **MaterialShaderCache** — Content-addressed cache (xxHash64 of generated HLSL). On cache hit, reuses compiled DXGI blob. On miss, compiles via DXC and stores.
5. **MaterialInstance runtime** — Extends current MaterialInstance to reference a compiled MaterialGraph. Per-instance scalar/vector parameter overrides without recompilation.

### Refactoring

- `mesh_ps.hlsl` split into a template with `#include "material_body.hlsli"` injection point. The compiler generates `material_body.hlsli` per-material.
- `PerObjectCB` extended with a material parameter block (16 float4 slots = 256 bytes) for runtime-tweakable graph parameters.
- `PipelineLibrary` accepts a `MaterialGraphHash` to select compiled shader variants.

## Impact

- Affected specs: `docs/engine/12-rendering/16-material-system.md`, `docs/specs/shader-variants.md`
- Affected code: `runtime/engine/renderer/material/`, `runtime/shaders/runtime/geometry/mesh_ps.hlsl`, `runtime/engine/renderer/core/shader/`, `runtime/engine/renderer/core/pipeline_library.h`
- Breaking change: NO — existing `Material` struct continues to work. Graph materials are opt-in via `.uzmat` files.
- User benefit: Procedural materials, material layering, no texture dependency for patterns
- Enables: Phase 5.6 Material Editor (VSCode node graph UI), terrain splatmap materials, decal materials
