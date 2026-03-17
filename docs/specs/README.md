# UzEngine — Specifications

Detailed specifications per major milestone. Each spec includes comparative analysis (Unreal/Godot/Unity), problem statement, and the UzEngine approach.

See also: [index.md](index.md) for status tracking.

## Phase 1: Runtime Foundation

| Spec | Milestone | Status |
|------|-----------|--------|
| [1.0-bootstrap.md](1.0-bootstrap.md) | Bootstrap & Project Setup | Complete |
| [1.1-math-containers.md](1.1-math-containers.md) | Math & Containers | Complete |
| [1.2-rhi.md](1.2-rhi.md) | RHI (4 backends: DX12, DX11, Vulkan, Metal) | Complete |
| [1.3-renderer.md](1.3-renderer.md) | Renderer (Deferred, IBL, shadows, atmosphere, post-process) | Mostly Complete |
| [1.4-scene-system.md](1.4-scene-system.md) | Scene System (ECS core, components, scene loader) | Implemented |
| [1.5-gameplay.md](1.5-gameplay.md) | Gameplay Systems (Physics → Animation → Audio → Nav → Script → Terrain → VFX → UI) | Partial (Lua scripting complete, physics/audio/animation pending) |

## Phase 1.5: Build Pipeline

| Spec | Milestone | Status |
|------|-----------|--------|
| [1.5-build-pipeline.md](1.5-build-pipeline.md) | Shader Compiler, Asset Processor, Packager | Partial |
| [1.5.0-shader-pipeline.md](1.5.0-shader-pipeline.md) | Shader compilation, caching, hot-reload, VFS, permutations, FXC/DXC/Slang | Complete |

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

## Rendering Sub-Specs

| Spec | Topic | Status |
|------|-------|--------|
| [rendering/shadows.md](rendering/shadows.md) | Shadow System — cascades, atlas, cache, distance fields | Implemented |
| [rendering/ibl-pipeline.md](rendering/ibl-pipeline.md) | IBL — GPU compute baker, BRDF LUT, irradiance, prefilter | Implemented |
| [rendering/atmosphere.md](rendering/atmosphere.md) | Atmosphere — procedural sky, transmittance LUT, height fog, aerial perspective | Implemented |
| [rendering/post-process.md](rendering/post-process.md) | Post-Process — bloom, auto-exposure, FXAA, tonemap, velocity | Implemented |
| [rendering/pso-cache-plan.md](rendering/pso-cache-plan.md) | PSO cache design and invalidation strategy | Reference |
| [rendering/debug-view-modes.md](rendering/debug-view-modes.md) | Debug visualization modes — GBuffer, shadow, lighting channels | Implemented |
| [ue5-dx11-readback-analysis.md](ue5-dx11-readback-analysis.md) | UE5 DX11 readback analysis for reference | Reference |
