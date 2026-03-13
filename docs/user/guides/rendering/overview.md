# Rendering

> **Status**: The rendering pipeline is under active development. This page
> describes the current state and planned architecture.

## Current State

The runtime currently renders an animated clear-screen loop via DX12:

1. Triple-buffered swapchain (FLIP_DISCARD)
2. Per-frame fence synchronization
3. Command list: barrier → clear RTV → barrier → present
4. Resize handling with swapchain recreation

This validates the full RHI stack (device, swapchain, command list, fences,
barriers) and serves as the foundation for the renderer.

## Shader Pipeline

The engine includes a full shader compilation pipeline (`runtime/engine/shader/`):

| Component | Purpose |
|-----------|---------|
| `ShaderVFS` | Virtual filesystem for shader sources — mount points, path resolution |
| `ShaderSourceManager` | `#include` parsing, xxHash64 content-based identity hashing |
| `ShaderDependencyGraph` | Bidirectional dependency tracking for hot-reload |
| `ShaderCache` | Disk-based bytecode cache keyed by content hash |
| `ShaderCompiler` | DXC integration — HLSL → DXIL (SM5.1–SM6.7) and SPIRV (Vulkan 1.3) |
| `ShaderPipeline` | High-level orchestrator — combines VFS, source, cache, compiler |
| `ShaderPermutation` | Feature flag variant expansion (`#pragma uz_permutation`) |
| `ShaderReflection` | SPIRV-Reflect binding extraction (CBV/SRV/UAV/sampler) |
| `ShaderHotReload` | FileWatcher + reverse dependency graph + dirty flags |
| `ShaderArchive` | Shipping mode precompiled archive (`.uzpak`) |

Key design decisions:
- **Content-addressed caching**: `hash = xxHash64(source + includes + defines + target + compiler_version)` — no timestamps, no stale cache
- **Two modes**: `UZ_SHADER_DEV` (load from disk + hot-reload) / `UZ_SHADER_SHIP` (precompiled archive)
- **Dynamic DXC loading**: `dxcompiler.dll` loaded at runtime, resolved relative to executable via `uz::ResolveEngineResource()`
- **No embedded shaders**: POC 1 proved that embedding shaders as C strings causes stale cache bugs

See [Shader Pipeline Guide](shader-pipeline.md) for developer workflow, or [Shader Pipeline Spec](../../../specs/1.5.0-shader-pipeline.md) for detailed technical design.

## Planned Pipeline

The target architecture is a compute-driven deferred renderer:

```
GBuffer Pass   →  Lighting Pass  →  Post-Processing  →  Present
(geometry)        (compute)          (TAA, Bloom,        (swapchain)
                                      Tonemap)
```

### Milestone 1.3 — Renderer (planned)

| Phase | Deliverable |
|-------|-------------|
| 1.3.0 | Triangle on screen (vertex buffer, shader, PSO) |
| 1.3.1 | Static mesh rendering (OBJ loader, transforms) |
| 1.3.2 | GBuffer pass (albedo, normals, depth) |
| 1.3.3 | Deferred lighting (PBR, point/directional lights) |
| 1.3.4 | Shadow mapping (cascaded shadow maps) |
| 1.3.5 | Post-processing (TAA, bloom, tonemap, SSAO) |
| 1.3.6 | Sky and atmosphere |

## SM6.6 Bindless Architecture

All resource access uses the SM6.6 bindless model:

- One global CBV/SRV/UAV heap (1M descriptors)
- Shaders access resources via a push-constant descriptor index
- No descriptor tables, no root descriptor changes per draw
- One root signature shared by all pipelines

```hlsl
// HLSL (SM6.6)
StructuredBuffer<Vertex> vertices = ResourceDescriptorHeap[descriptorIndex];
```

## Related Pages

- [RHI Overview](../rhi/overview.md) — Hardware abstraction layer
- [First Window](../../getting-started/first-window.md) — Current render loop
