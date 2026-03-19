<!-- doc-meta
type: index
category: analysis
theme: rhi
title: RHI & GPU Abstraction Analysis — Constant Buffers, Bindless, SM5
created: 2026-03-19
updated: 2026-03-19
status: complete
author: Claude
-->

# RHI & GPU Abstraction Analysis

Complete analysis of RHI (Render Hardware Interface) abstraction, constant buffer systems, bindless resources, shader model 5 fallbacks, and platform-specific patterns.

## Files

- [cbuffer-quick-reference.md](cbuffer-quick-reference.md) — Quick reference for constant buffer layout and binding
- [godot-rendering-device-resource-states.md](godot-rendering-device-resource-states.md) — Godot's RenderingDevice resource state management
- [godot-renderingdevice-architecture.md](godot-renderingdevice-architecture.md) — Godot's RenderingDevice abstraction architecture
- [index.md](index.md) — RHI analysis index
- [ue5-cbuffer-layout-dx12-vulkan-analysis.md](ue5-cbuffer-layout-dx12-vulkan-analysis.md) — Constant buffer layout analysis for DX12 and Vulkan
- [ue5-sm5-code-patterns.md](ue5-sm5-code-patterns.md) — SM5 (Shader Model 5) code patterns and idioms
- [ue5-sm5-decision-tree.md](ue5-sm5-decision-tree.md) — Decision tree for SM5 feature availability and fallback paths
- [ue5-sm5-fallback-patterns.md](ue5-sm5-fallback-patterns.md) — Patterns for graceful degradation on SM5 hardware
- [ue5-sm5-summary.md](ue5-sm5-summary.md) — Summary of SM5 capabilities and limitations
- [ue5-uniform-buffer-shader-binding-system.md](ue5-uniform-buffer-shader-binding-system.md) — UE5's uniform buffer binding and shader parameter system
- [ue5-vulkan-bindless-analysis.md](ue5-vulkan-bindless-analysis.md) — Vulkan bindless resource binding analysis and techniques

## Quick Navigation

### Understanding RHI Abstraction?
1. [godot-renderingdevice-architecture.md](godot-renderingdevice-architecture.md) — Godot RHI pattern
2. [ue5-cbuffer-layout-dx12-vulkan-analysis.md](ue5-cbuffer-layout-dx12-vulkan-analysis.md) — Buffer layout cross-platform
3. [ue5-uniform-buffer-shader-binding-system.md](ue5-uniform-buffer-shader-binding-system.md) — Parameter binding

### Constant Buffers & Binding?
- [cbuffer-quick-reference.md](cbuffer-quick-reference.md) — Quick reference
- [ue5-cbuffer-layout-dx12-vulkan-analysis.md](ue5-cbuffer-layout-dx12-vulkan-analysis.md) — Layout analysis

### SM5 Fallbacks?
- [ue5-sm5-summary.md](ue5-sm5-summary.md) — SM5 capabilities
- [ue5-sm5-decision-tree.md](ue5-sm5-decision-tree.md) — Feature detection
- [ue5-sm5-fallback-patterns.md](ue5-sm5-fallback-patterns.md) — Fallback patterns
- [ue5-sm5-code-patterns.md](ue5-sm5-code-patterns.md) — Code examples

### Bindless Resources?
- [ue5-vulkan-bindless-analysis.md](ue5-vulkan-bindless-analysis.md) — Vulkan bindless techniques

### Godot's Approach?
- [godot-renderingdevice-architecture.md](godot-renderingdevice-architecture.md) — Architecture
- [godot-rendering-device-resource-states.md](godot-rendering-device-resource-states.md) — Resource states

## Key Concepts

### RHI Abstraction
- **Platform abstraction**: DX11, DX12, Vulkan behind common interface
- **Resource management**: Textures, buffers, samplers, render targets
- **Pipeline state**: PSO caching, shader compilation, variant management
- **Command recording**: Deferred rendering via command buffers/lists

### Constant Buffers (CBuff)
- **Root constants**: Small inline data (root signature, DX12 only)
- **Constant buffers**: 256-byte aligned blocks of uniform data
- **Layout rules**: Padding, alignment, row-major vs. column-major
- **Binding**: Descriptors/slots for shader access
- **Update**: CPU writes, GPU reads with proper synchronization

### Bindless Resources
- **Descriptor tables**: Arrays of descriptors (Vulkan descriptor arrays, DX12 descriptor heaps)
- **Dynamic indexing**: Shaders index into arrays via run-time values
- **Advantages**: Reduces binding overhead, more flexible material parameters
- **Trade-offs**: Performance varies by platform, requires careful layout

### SM5 (Shader Model 5)
- **Generation**: DX11 generation (Fermi, Kepler, etc.)
- **Missing features**: Typed UAVs, 64-bit operations, indirect dispatch
- **Fallbacks**: Compute dispatches fixed size, UAV formats limited
- **Detection**: Query device feature level at runtime

### Resource States
- **Read**: SRV (Shader Resource View), constant buffer read
- **Write**: UAV (Unordered Access View), render target, depth target
- **Transitions**: Barriers between passes for synchronization
- **Hazards**: RAW, WAR, WAW data races require careful management
