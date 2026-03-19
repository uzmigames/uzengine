<!-- doc-meta
type: index
category: analysis
theme: upscaling
title: Upscaling & 4K Rendering Analysis — TSR, FSR, DLSS
created: 2026-03-19
updated: 2026-03-19
status: complete
author: Claude
-->

# Upscaling & 4K Rendering Analysis

Complete analysis of temporal super-resolution (TSR), frame upscaling techniques (FSR, DLSS), 4K rendering patterns, blend factor tuning, and temporal rejection heuristics.

## Files

- [godot-4k-upscaling-summary.md](godot-4k-upscaling-summary.md) — Summary of Godot's 4K upscaling approach
- [godot-high-resolution-upscaling.md](godot-high-resolution-upscaling.md) — Godot's high-resolution upscaling implementation
- [godot-upscaling-index.md](godot-upscaling-index.md) — Index of Godot upscaling concepts
- [godot-upscaling-uzengine-adaptation.md](godot-upscaling-uzengine-adaptation.md) — How to adapt Godot's upscaling to UzEngine
- [readme-tsr.md](readme-tsr.md) — Temporal super-resolution entry point and overview
- [ue5-tsr-architecture.md](ue5-tsr-architecture.md) — Complete TSR architecture, passes, feedback loops
- [ue5-tsr-blend-factors.md](ue5-tsr-blend-factors.md) — TSR blend factor analysis: history weight, rejection thresholds
- [ue5-tsr-rejection-shading.md](ue5-tsr-rejection-shading.md) — Rejection heuristics for detecting ghosting and temporal artifacts

## Quick Navigation

### Understanding Temporal Super-Resolution?
1. [readme-tsr.md](readme-tsr.md) — TSR overview
2. [ue5-tsr-architecture.md](ue5-tsr-architecture.md) — Complete architecture
3. [ue5-tsr-blend-factors.md](ue5-tsr-blend-factors.md) — Blend factor tuning

### Implementing TSR?
- [ue5-tsr-architecture.md](ue5-tsr-architecture.md) — Pipeline and passes
- [ue5-tsr-blend-factors.md](ue5-tsr-blend-factors.md) — Parameter tuning
- [ue5-tsr-rejection-shading.md](ue5-tsr-rejection-shading.md) — Artifact detection

### Godot Approach?
- [godot-high-resolution-upscaling.md](godot-high-resolution-upscaling.md) — Implementation
- [godot-upscaling-uzengine-adaptation.md](godot-upscaling-uzengine-adaptation.md) — Adaptation guidance

## Key Concepts

### Temporal Super-Resolution (TSR)
- **Input**: Low-resolution (e.g., 1080p) rendered frame + motion vectors
- **Output**: Higher resolution (e.g., 1440p, 4K) via temporal history reprojection
- **History**: Previous upscaled frames accumulated and blended
- **Motion vectors**: Per-pixel velocity for accurate history lookup
- **Rejection**: Detect ghosting via color variance, depth discontinuities

### Blend Factors
- **History weight**: How much previous frame contributes (0.9-0.95 typical)
- **Rejection threshold**: How much variance before rejecting history (adaptive)
- **Clamp color**: AABB or YCoCg clamp to detect out-of-range history
- **Confidence**: Increase blend when motion uncertainty is high

### Rejection Heuristics
- **Depth discontinuity**: Large depth differences near pixels → motion occlusion
- **Luminance variance**: Large color differences → ghosting risk
- **Motion magnitude**: High motion → lower history weight
- **Screen edge proximity**: Near edges → more cautious blending

### Alternatives (FSR, DLSS)
- **FSR (AMD)**: Spatial upscaling + temporal feedback, open-source
- **DLSS (NVIDIA)**: ML-based super-resolution, proprietary
- **Native TSR**: Algorithm-based temporal reprojection, full source control
