<!-- doc-meta
type: index
category: rendering
title: Rendering Analysis — UE5 Patterns for UzEngine
description: Complete analysis of UE5 rendering patterns extracted from source code, organized by topic.
tags: [ue5, rendering-architecture, shadow-mapping, sky-atmosphere, post-processing]
status: complete
last-updated: 2026-03-16
author: Claude
-->

# Rendering Analysis — UE5 Patterns for UzEngine

Complete architectural analysis of UE5 5.6 rendering systems extracted from source code at `W:\UE_5.6_Source`, organized into topic-specific subdirectories.

## Directory Structure

### [shadows/](shadows/README.md) — Shadow Mapping & Cascade Systems
Analysis of cascade shadow maps (CSM), PCF filtering, PCSS soft shadows, reversed-Z depth handling, and static shadow caching.

**Key files**:
- [ue5-reversed-z-shadow-maps.md](ue5-reversed-z-shadow-maps.md) — **Complete reversed-Z implementation** (clear values, projection matrices, depth tests, samplers, PCF logic)
- [shadow-comparison-semantics.md](shadows/shadow-comparison-semantics.md) — Three-phase depth conversion pattern
- [ue5-far-shadow-cascades-analysis.md](shadows/ue5-far-shadow-cascades-analysis.md) — Far cascade system
- [ue5-pcss-implementation-analysis.md](shadows/ue5-pcss-implementation-analysis.md) — Percentage-Closer Soft Shadows
- [ue5-reversed-z-architecture.md](shadows/ue5-reversed-z-architecture.md) — Reversed-Z depth space

**15 files total** — depth output, PCF, PCSS, far cascades, cache system

---

### [atmosphere/](atmosphere/README.md) — Sky & Atmosphere Systems
Analysis of Bruneton 2017 atmospheric scattering, transmittance LUT precomputation, and SkyLight IBL ambient lighting.

**Key files**:
- [ue5-sky-atmosphere-system.md](atmosphere/ue5-sky-atmosphere-system.md) — Complete system architecture
- [ue5-sky-atmosphere-transmittance-lut.md](atmosphere/ue5-sky-atmosphere-transmittance-lut.md) — LUT generation and sampling
- [ue5-skylight-component-analysis.md](atmosphere/ue5-skylight-component-analysis.md) — SkyLight IBL ambient
- [ue5-skyatmosphere-coordinate-system.md](atmosphere/ue5-skyatmosphere-coordinate-system.md) — Unit systems and coordinates

**14 files total** — sky atmosphere, transmittance, SkyLight, IBL, coordinate systems

---

### [post-process/](post-process/README.md) — Post-Processing Effects
Analysis of tone mapping, anti-aliasing (FXAA), and volumetric fog rendering.

**Key files**:
- [ue5-tonemap-fxaa-analysis.md](post-process/ue5-tonemap-fxaa-analysis.md) — ACES tone mapping and FXAA
- [frostbite-volumetric-rendering.md](post-process/frostbite-volumetric-rendering.md) — Volumetric fog (froxel-based)

**3 files total** — tonemap, FXAA, volumetrics

---

### [materials/](materials/README.md) — Materials & Parallax Mapping
Analysis of parallax occlusion mapping (POM), displacement mapping, and surface detail techniques.

**Key files**:
- [ue5-pom-code-reference.md](materials/ue5-pom-code-reference.md) — UE5 POM implementation
- [godot-pom-heightmap-analysis.md](materials/godot-pom-heightmap-analysis.md) — Godot's POM approach
- [parallel-ox-mapping-analysis.md](materials/parallel-ox-mapping-analysis.md) — Optimization variants

**4 files total** — POM, displacement, heightmap, parallax

---

### [debug-visualization/](debug-visualization/README.md) — Debug & Visualization Tools
Analysis of debug visualization modes, buffer visualization, and rendering diagnostics.

**Key files**:
- [ue5-view-mode-buffer-visualization-analysis.md](debug-visualization/ue5-view-mode-buffer-visualization-analysis.md) — UE5 view modes
- [godot-debug-modes-taxonomy.md](debug-visualization/godot-debug-modes-taxonomy.md) — Godot debug mode classification
- [godot-vs-ue5-debug-visualization.md](debug-visualization/godot-vs-ue5-debug-visualization.md) — Engine comparison

**5 files total** — view modes, buffer visualization, debug UI

---

### [general/](general/README.md) — General Rendering Architecture
Overview and architecture analysis of complete rendering systems, shader patterns, and rendering practices.

**Key files**:
- [ue5-rendering-systems-analysis.md](general/ue5-rendering-systems-analysis.md) — Complete rendering architecture
- [ue5-default-scene-rendering-systems.md](general/ue5-default-scene-rendering-systems.md) — Scene rendering pipeline
- [ue5-shader-patterns-analysis.md](general/ue5-shader-patterns-analysis.md) — Shader best practices

**3 files total** — rendering pipeline, systems, patterns

---

### Godot Engine Analysis — High-Resolution Upscaling
Complete analysis of Godot rendering architecture with focus on 4K upscaling (FSR 1.0/2.0, MetalFX) as reference for UzEngine implementation.

**Key files**:
- [godot-rendering-analysis.md](godot/godot-rendering-analysis.md) — Godot rendering overview
- [godot-sky-atmosphere-architecture.md](godot/godot-sky-atmosphere-architecture.md) — Godot sky system
- [godot-4k-upscaling-summary.md](godot-4k-upscaling-summary.md) — **Quick reference** (5 modes, decision tables, 4K profiles, key numbers)
- [godot-high-resolution-upscaling.md](godot-high-resolution-upscaling.md) — **Technical deep-dive** (FSR 1.0/2.0 implementation, fallback chain, jitter management, mipmap bias)
- [godot-upscaling-uzengine-adaptation.md](godot-upscaling-uzengine-adaptation.md) — **UzEngine adaptation guide** (C++23 code patterns, render graph integration, Lua API, motion vectors, capability detection)

**5 files total** — 19 KB summary + 19 KB technical analysis + 17 KB adaptation guide

---

### [unity/](unity/README.md) — Unity Engine Analysis
Analysis of Unity rendering and application architecture as reference for comparison.

**Key files**:
- [unity-analysis-index.md](unity/unity-analysis-index.md) — Unity rendering overview
- [unity-application-abstraction.md](unity/unity-application-abstraction.md) — Application abstraction patterns
- [unity-application-abstraction-uzengine-roadmap.md](unity/unity-application-abstraction-uzengine-roadmap.md) — UzEngine roadmap

**4 files total** — rendering, application abstraction, roadmap

---

## Total Files

**53+ analysis documents** organized into 8 topic directories:
- Shadows: 15 files
- Atmosphere: 14 files
- Post-Process: 3 files
- Materials: 4 files
- Debug Visualization: 5 files
- General: 3 files
- Godot: 5 files (2 base + 3 new: summary, deep-dive, adaptation guide)
- Unity: 4 files

## Quick Start by Task

### Implementing Shadow Mapping?
1. Read [shadows/shadow-comparison-semantics.md](shadows/shadow-comparison-semantics.md)
2. Copy templates from [shadows/shadow-shader-templates.md](shadows/shadow-shader-templates.md)
3. Reference [shadows/ue5-shadow-sampling-exact-code.md](shadows/ue5-shadow-sampling-exact-code.md)

### Implementing Sky & Atmosphere?
1. Start with [atmosphere/ue5-sky-atmosphere-system.md](atmosphere/ue5-sky-atmosphere-system.md)
2. Learn coordinate systems: [atmosphere/ue5-skyatmosphere-coordinate-system.md](atmosphere/ue5-skyatmosphere-coordinate-system.md)
3. Implement transmittance LUT: [atmosphere/ue5-sky-atmosphere-transmittance-lut.md](atmosphere/ue5-sky-atmosphere-transmittance-lut.md)

### Implementing IBL Ambient?
1. Read [atmosphere/ue5-skylight-component-analysis.md](atmosphere/ue5-skylight-component-analysis.md)
2. Learn capture modes: [atmosphere/ue5-skylight-cubemap-capture-ibl.md](atmosphere/ue5-skylight-cubemap-capture-ibl.md)

### Understanding Rendering Pipeline?
1. Overview: [general/ue5-rendering-systems-analysis.md](general/ue5-rendering-systems-analysis.md)
2. Scene-specific: [general/ue5-default-scene-rendering-systems.md](general/ue5-default-scene-rendering-systems.md)
3. Shader patterns: [general/ue5-shader-patterns-analysis.md](general/ue5-shader-patterns-analysis.md)

### Implementing Post-Processing?
1. Tone mapping: [post-process/ue5-tonemap-fxaa-analysis.md](post-process/ue5-tonemap-fxaa-analysis.md)
2. Volumetric fog: [post-process/frostbite-volumetric-rendering.md](post-process/frostbite-volumetric-rendering.md)

## Reference Engine Sources

- **UE5 source**: W:\UE_5.6_Source — Unreal Engine 5.6 full source
- **Godot source**: E:\godot — Godot Engine source
- **Unity docs**: Unity official documentation (as available)

## Cross-Cutting Concepts

### Depth Spaces
- **Reversed-Z**: Main view uses 1=near, 0=far (UE5 default)
- **Linear Depth**: Shadow maps use standard [0=near, 1=far]
- **Conversion**: 1 - ReversedZDepth = LinearDepth
- See [shadows/ue5-reversed-z-architecture.md](shadows/ue5-reversed-z-architecture.md)

### Coordinate Systems
- **World Units**: Centimeters (UE5 parity)
- **Atmosphere**: Kilometers (bottom=6360km, top=6420km)
- **Ray Direction**: Unit vector from eye toward pixel
- See [atmosphere/ue5-skyatmosphere-coordinate-system.md](atmosphere/ue5-skyatmosphere-coordinate-system.md)

### Rendering Pipeline Stages
1. **Depth Prepass** — Occlusion and depth pyramid
2. **Shadow Passes** — Shadow map rendering
3. **Base Pass** — GBuffers (deferred) or direct output (forward)
4. **Lighting Pass** — Compute lighting from GBuffers
5. **Transparency** — Blend translucent geometry
6. **Post-Processing** — Bloom, tone map, color grading
7. **UI** — Final overlay

## Related Documentation

- **UzEngine Specs**: `/docs/specs/rendering/` — Implementation specifications
- **Architecture Directives**: `/docs/architecture/` — Design decisions and patterns
- **RHI Abstraction**: `/docs/architecture/rhi-abstraction.md` — Rendering hardware interface
