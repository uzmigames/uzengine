<!-- doc-meta
type: index
category: analysis
theme: engine-comparison
title: Engine Comparison & Architecture — UE5, Godot, Unity
created: 2026-03-19
updated: 2026-03-19
status: complete
author: Claude
-->

# Engine Comparison & Architecture Analysis

Complete analysis of architecture patterns and design decisions across UE5, Godot, and Unity engines, including application-layer abstraction, rendering device patterns, and design directives.

## Directories

### [application-layer/](application-layer/)
- Application-layer architecture analysis
- Rendering device abstraction patterns
- Platform services and abstraction layers
- DX11 readback patterns and performance considerations

### [architecture/](architecture/)
- Executive summaries of engine architecture
- Godot application architecture patterns
- Cross-engine architectural patterns for UzEngine adoption

### [general/](general/)
- Cross-engine architecture comparisons (UE5, Godot, Unity)
- Architecture decision patterns
- Rendering device abstraction analysis

### [godot-analysis/](godot-analysis/)
- Godot-specific rendering architecture
- Sky atmosphere implementation in Godot
- Rendering analysis and patterns

### [unity-analysis/](unity-analysis/)
- Unity-specific rendering patterns
- Application abstraction in Unity
- Cross-platform considerations

## Files

- [godot-analysis-summary.md](godot-analysis-summary.md) — Executive summary of Godot architecture analysis
- [research-summary.md](research-summary.md) — Cross-engine research findings

## Quick Navigation

### Understanding Engine Architectures?
1. [architecture/00-executive-summary.md](architecture/00-executive-summary.md) — Start here
2. [general/](general/) — Architecture comparisons
3. [application-layer/](application-layer/) — Application patterns

### Godot Patterns?
- [godot-analysis/](godot-analysis/) — Godot-specific analysis
- [general/godot-vs-ue5-renderingdevice.md](general/godot-vs-ue5-renderingdevice.md) — Godot vs. UE5 comparison

### Application-Layer Design?
- [application-layer/ue5-application-layer-architecture.md](application-layer/ue5-application-layer-architecture.md) — UE5 patterns
- [application-layer/ue5-4k-rendering-upscaling.md](application-layer/ue5-4k-rendering-upscaling.md) — 4K rendering architecture

### Learning for UzEngine?
- [architecture/02-godot-patterns-for-uzengine.md](architecture/02-godot-patterns-for-uzengine.md) — Godot patterns applicable to UzEngine

## Key Concepts

### Application-Layer Abstraction
- **Rendering Device**: Platform-agnostic interface (DX11, DX12, Vulkan)
- **Resource Management**: Textures, buffers, shaders behind abstraction
- **Command Recording**: Deferred or immediate mode rendering
- **Synchronization**: Fences, semaphores, GPU-CPU sync

### Godot RenderingDevice
- **Feature parity across backends**: Unified API for DX11, Vulkan, etc.
- **Descriptor management**: Automatic binding of resources
- **Pipeline caching**: PSO caching transparent to user
- **Resource tracking**: Automatic lifetime management

### UE5 Architecture
- **RHI (Render Hardware Interface)**: Low-level platform abstraction
- **FShaderCache**: PSO caching at system level
- **Modular systems**: Rendering, lighting, particles as plugins
- **Data-driven**: Parameters and settings from configs

### Unity Patterns
- **Graphics.DrawMesh**: High-level render API
- **Shader system**: Material/Shader/Variant hierarchy
- **Built-in Render Pipeline**: Default rendering system
- **URP (Universal Render Pipeline)**: Modular, scalable alternative

### Cross-Engine Learnings
- **Plugin architecture**: Modular effect systems
- **Resource pooling**: Reuse GPU resources
- **Deferred rendering**: Batch light processing
- **Temporal techniques**: Amortize expensive operations over frames
