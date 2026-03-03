# Phase 2 -- GPU Instancing

## Why

Without instancing, every entity in the scene generates a separate draw call even when many share the same mesh and material (e.g., 500 trees, 200 crates, 1000 grass clumps). Each draw call incurs CPU overhead for state setup and GPU command processor overhead. GPU instancing batches identical mesh+material pairs into a single `DrawIndexedInstanced` call, reducing draw calls by 10-100x in typical scenes. This is essential for maintaining 60fps with complex scenes.

## What Changes

- New `BatchBuilder` system: per-frame grouping of `CMeshRenderer` entities by `{mesh, material, variant_key}`.
- `InstancedDrawCall` struct: mesh handle, material handle, instance count, transform `StructuredBuffer`.
- GPU upload: `Array<mat4>` world transforms uploaded to `StructuredBuffer<float4x4>` via upload ring buffer.
- Shader variant: `#define INSTANCED` toggles between per-object cbuffer (b0) and StructuredBuffer (t7) for world transform.
- Vertex shader reads `SV_InstanceID` to index into transform buffer when instanced.
- Single-instance batches fall back to standard per-object cbuffer (no performance penalty).
- Transparent objects excluded from instancing (require per-object depth sorting).
- Skinned meshes excluded from instancing (per-instance bone palettes).
- GBuffer pass and shadow passes both use instanced batches.
- New IPC handler: `renderer.batchStats` for debugging.

## Dependencies

- **phase2-pbr-renderer** (GBuffer pass and deferred pipeline)
- **phase2-shader-variants** (`INSTANCED` define in variant system)

## Spec Reference

[GPU Instancing Spec](../../../docs/specs/gpu-instancing.md)

## Impact
- Affected specs: `renderer.md`, `gpu-instancing.md` (new), `shader-variants.md`
- Affected code: `src/renderer/instancing/` (new), `src/renderer/passes/gbuffer_pass`, `src/renderer/passes/shadow_pass`, `shaders/builtin/pbr.hlsl`
- Breaking change: NO (additive system, non-instanced path unchanged)
- User benefit: Dramatic draw call reduction; 60fps with thousands of identical objects; no manual batching required
