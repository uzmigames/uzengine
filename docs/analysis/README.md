# Analysis Documentation

This directory contains architectural and rendering analysis documents from reference engines (UE5, Godot, Unity) that inform UzEngine's design decisions.

## Directory Structure

### `/rendering/` — Rendering Systems & Shader Analysis

Analysis of rendering pipelines, visual effects, debug visualization, and shader patterns:

- **godot-debug-modes-quick-reference.md** — Godot's debug view modes (wireframe, vertex colors, etc.)
- **godot-debug-modes-taxonomy.md** — Complete taxonomy of Godot's debug visualization system
- **godot4-debug-visualization-analysis.md** — Godot 4 implementation details for debug rendering
- **godot-vs-ue5-debug-visualization.md** — Comparative analysis: Godot vs UE5 debug systems
- **ue5-view-mode-buffer-visualization-analysis.md** — UE5's buffer visualization view modes (GBuffer, light buffer, etc.)
- **godot-pom-heightmap-analysis.md** — Parallax Occlusion Mapping (heightmap displacement) in Godot
- **godot-pom-hlsl-reference.md** — Production HLSL code for POM implementations
- **godot-rendering-analysis.md** — Godot's overall rendering architecture
- **godot4-tonemap-fxaa-analysis.md** — Tone mapping and FXAA antialiasing in Godot 4
- **ue5-tonemap-fxaa-analysis.md** — UE5's tone mapping and FXAA implementations
- **ue5-pom-code-reference.md** — UE5's parallax occlusion mapping implementation reference
- **ue5-rendering-systems-analysis.md** — UE5 rendering system architecture
- **ue5-shader-patterns-analysis.md** — Common shader patterns and conventions in UE5
- **ue5-default-scene-rendering-systems.md** — Default rendering systems in UE5's base scene
- **parallel-ox-mapping-analysis.md** — Parallax Occlusion Mapping algorithm variants and analysis

### `/rhi/` — Graphics API & RHI Abstraction

Analysis of rendering hardware interfaces, buffer layouts, and GPU memory management:

- **cbuffer-quick-reference.md** — Quick reference for constant buffer layouts and packing rules
- **ue5-cbuffer-layout-dx12-vulkan-analysis.md** — Constant buffer layout differences between DX12 and Vulkan
- **ue5-vulkan-bindless-analysis.md** — UE5's bindless resource system on Vulkan
- **godot-rendering-device-resource-states.md** — Godot's RenderingDevice resource state management
- **godot-renderingdevice-architecture.md** — Godot's unified rendering device abstraction architecture

### `/general/` — Architecture & Comparative Analysis

High-level architectural decisions, engine comparisons, and gap analysis:

- **ue5-architecture-summary.md** — Executive summary of UE5's architecture
- **godot-architecture-decisions.md** — Key architectural decisions in Godot
- **godot-d3d12-patterns-uzengine.md** — D3D12 patterns in Godot applicable to UzEngine
- **godot-vs-ue5-renderingdevice.md** — Comparative analysis: Godot RenderingDevice vs UE5 RHI
- **godot-vs-uzengine.md** — Godot vs UzEngine feature/architecture comparison
- **ue5-basic-scene-gap-analysis-2026-03-14.md** — Gap analysis: default rendering features in UE5

## Usage by Role

### Render Engineers
1. Start with `/rendering/` subdirectory for your feature area
2. Review reference engine analysis (Godot, UE5)
3. Check `/rhi/` for GPU-level implementation details

### RHI / Graphics API Engineers
1. Focus on `/rhi/` subdirectory for buffer layouts and resource management
2. Cross-reference with `/rendering/` for how shaders consume RHI abstractions
3. Review `/general/` for architectural patterns

### Architects / Technical Leads
1. Read `/general/` for high-level decisions and comparisons
2. Dive into specific subdirectories based on feature areas
3. Use analysis for feature prioritization and feasibility assessment

### Performance Engineers
1. Review tone mapping, antialiasing, and debug visualization docs
2. Check constant buffer layout analysis for memory optimization
3. Look at bindless resource analysis for GPU memory strategies

## Quick Search Guide

**Looking for...**
- Debug view modes → `/rendering/godot-debug-modes-*.md` and `/rendering/ue5-view-mode-*.md`
- Parallax occlusion mapping → `/rendering/*pom*.md`
- Constant buffers → `/rhi/cbuffer-quick-reference.md` and `/rhi/*cbuffer*.md`
- Bindless resources → `/rhi/*bindless*.md`
- Tone mapping / antialiasing → `/rendering/*tonemap*.md` and `/rendering/*fxaa*.md`
- Engine architecture → `/general/*architecture*.md`
- Feature gaps → `/general/*gap-analysis*.md`

## Document Dates & Maintenance

All analysis documents include date metadata and source file references. When planning implementation:
1. Check the analysis date — very recent analysis (< 3 months) is more reliable
2. Verify source engine version (e.g., UE5.6, Godot 4.x)
3. Cross-reference with current engine source code if analysis is > 6 months old

## Contributing

When adding new analysis documents:
1. Place in appropriate subdirectory (`/rendering/`, `/rhi/`, or `/general/`)
2. Use kebab-case filenames
3. Include title, analysis date, and source engine information in the document header
4. Update this README with a one-line description in the appropriate section
