# Phase 100 — RHI Optimization & Engine Scaling

> **Status**: Planned
> **Priority**: High — These are the bottlenecks that will surface as scenes grow beyond demos.
> **Scope**: RHI layer, renderer, memory subsystem, GPU scheduling

## Motivation

The RHI is functional and correct. Demos run at 6000+ FPS on simple scenes.
But "functional" and "scalable" are different things. As scene complexity grows
(more materials, meshes, textures, draw calls), these missing systems will
become real bottlenecks:

1. **PSO creation stutter** — no disk cache, no deduplication, no warmup
2. **CPU-bound submission** — no GPU-driven rendering, no indirect draws
3. **Memory pressure** — no streaming, no residency control, no eviction
4. **Single-threaded recording** — infrastructure exists but isn't exercised
5. **Wasted GPU parallelism** — async compute queue exists but unused
6. **Upload stalls** — copy queue exists but isn't integrated

## What's Already Done (NOT in scope)

These are **implemented and working** — the ChatGPT analysis incorrectly marked some as planned:

| System | Status | Evidence |
|--------|--------|----------|
| Render Graph / Frame Graph | **IMPLEMENTED** | Full DAG, topological sort, barrier scheduling, transient resource pooling |
| D3D12MA integration | **IMPLEMENTED** | v3.1.0, budget queries, per-allocation tracking |
| Vulkan VMA | **IMPLEMENTED** | Memory allocation via VMA |
| Breadcrumbs + DRED + Timestamps | **IMPLEMENTED** | Full diagnostic pipeline |
| Triple-buffered allocators | **IMPLEMENTED** | Per-frame command allocator rotation |
| Global bindless root signature (DX12) | **IMPLEMENTED** | SM6.6 path with persistent descriptor heap |
| Copy queue infrastructure | **IMPLEMENTED** | Queue + fence exist, not yet wired to upload path |

## Optimization Roadmap (ordered by ROI)

### P0 — Highest Impact

1. **PSO Cache** — Hash-based deduplication + disk serialization + warmup/precache
2. **GPU Memory Subsystem** — Streaming pool, upload ring, residency control, category budgets
3. **Upload Ring + Copy Queue Integration** — Wire existing copy queue for async uploads

### P1 — High Impact

4. **Async Compute** — Wire compute queue, schedule SSAO/culling/mipgen off graphics queue
5. **Vulkan Descriptor Overhaul** — Reduce flush frequency, group writes by pass, eliminate WaitIdle paths
6. **Parallel Command Recording** — Thread-pool recording, merge command lists per pass

### P2 — Medium Impact (Future)

7. **GPU-Driven Rendering** — Indirect draws, compute culling, draw list compaction
8. **Frame Pacing / Latency Tooling** — Cross-backend pacing telemetry, VRR support
9. **Shader Backend Parity Strategy** — Define DX12 high-end / Vulkan compat / Metal Tier2 paths

### Not Prioritized

- **DX11 optimization** — Keep minimal and stable, don't invest optimization effort
- **Micro-optimizing API wrappers** — Gains are in scheduling and memory, not call overhead
