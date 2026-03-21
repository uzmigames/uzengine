# 64-bit Sort Key for Draw Command Batching

**Category**: architecture
**Tags**: rendering, draw-list, sorting, batching, performance, gpu

## Description

DrawList uses 64-bit sort keys to minimize GPU state changes. Layout: [63-56] Pipeline/Shader ID (8 bits, most expensive — PSO switches), [55-48] Material ID (8 bits, texture binds), [47-24] Depth (24 bits, front-to-back reduces overdraw), [23-0] Mesh ID (24 bits, batch same geometry). Transparent objects use back-to-front via depth inversion. Helpers: SortKey::Opaque() and SortKey::Transparent().

## Example

// Sort key construction:\nsortKey = SortKey::Opaque(pipelineId, materialId, depth, meshId);\n// Most significant bits → fewest state changes\n// Pipeline switches > Material switches > Depth sorting > Mesh batching\n\n// Transparent: back-to-front\nsortKey = SortKey::Transparent(pipelineId, materialId, depth, meshId);

## When to Use

When adding new render passes that submit draw commands. When implementing new material/shader types. When optimizing draw call performance.

## When NOT to Use

For fullscreen quad passes (no sorting needed). For compute-only passes.
