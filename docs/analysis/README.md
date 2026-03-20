# UzEngine Analysis Documentation

<!-- doc-meta
type: index
category: analysis
theme: master-index
title: UzEngine Analysis — Complete Reference Index
created: 2026-03-17
updated: 2026-03-19
status: complete
author: Claude
-->

**Updated**: 2026-03-20

Complete reference documentation for UzEngine rendering analysis, architecture patterns, and parity studies across UE5, Godot, and Unity.

## Directory Structure (12 categories, 135 files)

### Rendering Analysis

- **[atmosphere/](atmosphere/)** (26 files) — Sky rendering, Bruneton atmospheric scattering, transmittance LUT, aerial perspective, fog systems, SkyLight IBL, height fog
  - UE5 sky atmosphere system, transmittance LUT, coordinate system mapping, SkyLight component, IBL cubemap capture, height fog complete reference, aerial perspective volume, blue horizon tint

- **[gi/](gi/)** (3 files) — Global illumination analysis, Lumen radiance cache, radiance compositing
  - Lumen radiance cache compositing, radiance cache quick reference, UE5 ground radiance analysis

- **[post-process/](post-process/)** (12 files) — Tone mapping, FXAA, volumetric rendering, auto-exposure, color grading, bloom, motion blur, DOF, screen-space effects
  - Auto-exposure histogram, ACES tone mapping, Frostbite volumetric approach, post-process pipeline integration, frame composition

- **[rendering-pipeline/](rendering-pipeline/)** (11 files) — General rendering architecture, deferred rendering, forward rendering, GBuffer layouts, shader patterns, debug visualization, velocity generation
  - UE5 rendering systems (depth prepass, shadow passes, base pass, lighting pass), shader compilation, debugging and profiling, view mode buffers, frame analysis

- **[lighting/](lighting/)** (21 files) — Shadow mapping, cascaded shadows, PCSS, distance field shadows, point/spot lights, light transport, reversed-Z depth, skylight ambient
  - UE5 shadow architecture, cascade selection and artifacts, PCF filtering, soft shadows, shadow receiver bias, static shadow caching, environment component defaults, fog-light integration

### Data Flow & Integration

- **[dataflow/](dataflow/)** (5 files) — Cross-subsystem data flow documents for multi-file implementations
  - SDF GI pipeline, GPU SDF bake, skylight-fog-ambient, SSGI, radiance cache dataflow

### Asset & Platform Analysis

- **[asset-pipeline/](asset-pipeline/)** (5 files) — Texture streaming, shader code library, PSO caching, material platform data, cooking pipeline
  - UE5 loading architecture, async streaming patterns, texture format cooking (BC7/ASTC), shader bytecode caching, memory-mapped lookups

- **[rhi/](rhi/)** (12 files) — RHI abstraction, constant buffers, bindless resources, shader model 5 fallbacks, Vulkan patterns, DX11/DX12 binding models
  - UE5 cbuffer layout analysis, SM5 feature detection and fallbacks, Vulkan bindless analysis, resource binding patterns, platform-specific optimizations

### Upscaling & Advanced Rendering

- **[upscaling/](upscaling/)** (9 files) — Temporal super-resolution (TSR), FSR, DLSS, 4K rendering, blend factor tuning, rejection shading
  - UE5 TSR architecture, blend factor analysis, temporal rejection heuristics, Godot upscaling patterns, frame composition for higher resolutions

### Materials & Shading

- **[materials/](materials/)** (16 files) — Material system architecture, material instances, material parameters, parallax occlusion mapping, shader authoring patterns
  - UE5 material data model extraction, parameter system, material directory structure, POM implementation (heightmap parallax, ray stepping), material patterns for UzEngine

### Project Parity & Roadmaps

- **[parity/](parity/)** (8 files) — UE5 vs UzEngine rendering gap analysis, visual impact assessment, rendering roadmap, code location references
  - Gap analysis with visual impact quantification, implementation priorities, code locations for improvements, 30-day roadmap, feature completeness matrix

### Engine Comparison & Architecture

- **[engine-comparison/](engine-comparison/)** (25 files) — Cross-engine architecture analysis (UE5, Godot, Unity), design patterns, rendering device abstractions, platform services
  - Godot architecture decisions and patterns, UE5 architecture summary, Unity rendering device patterns, D3D12 abstraction layers, application-layer design, scene management architecture

## Quick Navigation

### New to UzEngine Analysis? Start Here
1. [parity/](parity/) — Current rendering state and gap analysis
2. [rendering-pipeline/](rendering-pipeline/) — How rendering works end-to-end
3. [lighting/](lighting/) — Shadow and light systems
4. [atmosphere/](atmosphere/) — Sky and fog rendering

### Implementing a Specific Feature?
- **Sky & Fog** → [atmosphere/](atmosphere/)
- **Global Illumination** → [gi/](gi/)
- **Shadows & Lights** → [lighting/](lighting/)
- **Tone Mapping, Bloom, TAA** → [post-process/](post-process/)
- **Materials & Parallax** → [materials/](materials/)
- **Upscaling (TSR/FSR)** → [upscaling/](upscaling/)
- **RHI & Constant Buffers** → [rhi/](rhi/)
- **Asset Loading** → [asset-pipeline/](asset-pipeline/)
- **Cross-Subsystem Data Flow** → [dataflow/](dataflow/)

### Studying Engine Architecture?
- [engine-comparison/](engine-comparison/) — UE5, Godot, Unity patterns
- [engine-comparison/architecture/](engine-comparison/architecture/) — Application-layer design
- [engine-comparison/application-layer/](engine-comparison/application-layer/) — Rendering device abstractions

## Current Rendering State (2026-03-19)

### Implemented (UE5 parity ✓)
- Deferred PBR (GGX + Burley + SmithJoint + multiscatter energy compensation)
- Cascaded Shadow Maps (4 cascades, per-cascade PCF radius, cascade selection)
- Distance Field Shadows (atlas, cone trace, tile culling)
- IBL (irradiance SH L2 + prefiltered specular + BRDF LUT, SkyLight component)
- Bloom (6-mip pyramid, Karis average, per-stage tints)
- FilmToneMap (ACES 3-segment, 5 artist params)
- Sky Atmosphere (Bruneton, multi-scatter LUT, ozone, aerial perspective)
- Height Fog (2-layer, directional inscattering, sky→fog injection, volumetric integration)
- TAA (Halton jitter, YCoCg clamp, history ping-pong)
- SSR (Hi-Z ray trace, Fresnel resolve, deferred integration)
- GTAO (compute pipeline, denoise, temporal, multi-bounce)
- Contact Shadows (screen-space ray march, 8 steps)
- Film Grain + Vignette + Chromatic Aberration
- Auto-Exposure (histogram, min=0.03, max=8.0)
- Color Grading LUT (full CombineLUTs pipeline, 32x32x32 3D)
- Spot Lights (cone attenuation, UE5 SpotAngles packing)
- Motion Blur (velocity gather, depth-aware, adaptive samples)
- DOF (CoC setup + gather blur)

### Priority Gaps
- Volumetric clouds (froxel grid, phase functions, lighting)
- Decals (DBuffer projection, normal blending)
- Area lights (LTC approximation)
- Shading models (subsurface scattering, clear coat, hair)
- LOD system (distance-based, screen-coverage metrics)
- Post-process volumes (blendable parameters, camera triggers)
