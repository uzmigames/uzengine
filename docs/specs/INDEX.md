# UzEngine 0.2 — Specs Index

> Detailed specifications per major milestone. Each spec includes comparative analysis (Unreal/Godot/Unity), problem statement, and UzEngine approach.

## Cross-Cutting

| Spec | Scope | Status |
|------|-------|--------|
| [observability/observability-cpp.md](observability/observability-cpp.md) | C++ Runtime: Tracy, spdlog+UZ_LOG, GPU profiling (DRED+breadcrumbs), Crash Reporter (separate exe) | Planned |
| [observability/observability-ts.md](observability/observability-ts.md) | TS Extension + N-API + MCP: structured logging, build diagnostics, VSCode Output integration, end-to-end debug | Planned |

## Phase 1: Runtime Foundation

| Spec | Milestone | Status |
|------|-----------|--------|
| [1.0-bootstrap.md](1.0-bootstrap.md) | Bootstrap & Project Setup | Planned |
| [1.1-math-containers.md](1.1-math-containers.md) | Math & Containers | Planned |
| [1.2-rhi.md](1.2-rhi.md) | RHI (DX12 SM6.6) | Planned |
| [1.3-renderer.md](1.3-renderer.md) | Renderer (Triangle → Deferred → PostFX → Advanced) | Planned |
| [1.4-scene-system.md](1.4-scene-system.md) | Scene System (ECS, hierarchy, serialization) | Planned |
| [1.5-gameplay.md](1.5-gameplay.md) | Gameplay Systems (Physics → Animation → Audio → Nav → Script → Terrain → VFX → UI) | Planned |

## Phase 1.5: Build Pipeline

| Spec | Milestone | Status |
|------|-----------|--------|
| [1.5-build-pipeline.md](1.5-build-pipeline.md) | Shader Compiler, Asset Processor, Packager | Planned |

## Phase 2: Editor Extension

| Spec | Milestone | Status |
|------|-----------|--------|
| [2.0-napi-foundation.md](2.0-napi-foundation.md) | N-API Foundation (POC 0.2 validated) | Partial |
| [2.1-viewport-ui.md](2.1-viewport-ui.md) | Viewport & Core UI | Planned |
| [2.2-2.3-editors.md](2.2-2.3-editors.md) | Core & Advanced Editors | Planned |

## Phase 3: MCP Integration

| Spec | Milestone | Status |
|------|-----------|--------|
| [3.0-mcp-integration.md](3.0-mcp-integration.md) | MCP Server, AI Tools, AI Content Pipeline | Planned |

## Phase 4: Polish & Shipping

| Spec | Milestone | Status |
|------|-----------|--------|
| [4.0-polish-shipping.md](4.0-polish-shipping.md) | Platform SDKs, Networking, Optimization, Release | Planned |

---

*Each spec follows the same structure:*
1. *How Unreal/Godot/Unity solve it*
2. *The problem*
3. *UzEngine approach*
4. *Deliverables*
5. *Dependencies*
