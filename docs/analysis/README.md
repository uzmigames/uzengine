# UzEngine Analysis Documentation

**Updated**: 2026-03-17

## Directory Structure

```
docs/analysis/
├── README.md                    (this file)
├── materials/                   UE5 material system analysis
│   ├── QUICK-REFERENCE-MATERIAL-DATA.txt
│   ├── ue5-material-parameter-system-extraction.md
│   ├── ue5-material-patterns-uzengine-adaptation.md
│   └── ue5-material-struct-layouts-concrete.md
├── rendering-parity/            UE5 vs UzEngine gap analysis + roadmap
│   ├── VISUAL-IMPACT-SUMMARY.txt
│   ├── ue5-vs-uzengine-rendering-gap-analysis.md
│   ├── rendering-roadmap-next-30-days.md
│   └── code-locations-for-improvements.md
├── shadows/                     Shadow system analysis
│   ├── ue5-shadow-darkening-analysis.md
│   ├── ue5-shadow-parity-debugging-checklist.md
│   └── ue5-shadow-receiver-bias-implementation.md
├── application-layer/           App architecture analysis
│   ├── ue5-application-layer-architecture.md
│   └── ue5-dx11-readback-analysis.md
├── rendering/                   Rendering subsystem analysis
│   ├── atmosphere/              Sky atmosphere + transmittance LUT
│   ├── shadows/                 CSM + PCSS + DFS analysis
│   ├── materials/               POM + material analysis
│   ├── post-process/            Tonemap + volumetrics
│   ├── debug-visualization/     Buffer viz + debug modes
│   ├── godot/                   Godot engine reference
│   └── unity/                   Unity engine reference
├── rhi/                         RHI abstraction analysis
│   ├── ue5-cbuffer-layout-dx12-vulkan-analysis.md
│   ├── ue5-sm5-fallback-patterns.md
│   └── ue5-vulkan-bindless-analysis.md
├── architecture/                Godot architecture patterns
└── general/                     Cross-engine comparisons
```

## Current Rendering State (2026-03-17)

### Implemented (UE5 parity)
- Deferred PBR (GGX + Burley + SmithJoint + multiscatter energy compensation)
- Cascaded Shadow Maps (4 cascades, per-cascade PCF radius)
- Distance Field Shadows (atlas, cone trace, tile culling)
- IBL (irradiance SH L2 + prefiltered specular + BRDF LUT)
- Bloom (6-mip pyramid, Karis average, per-stage tints)
- FilmToneMap (ACES 3-segment, 5 artist params)
- Sky Atmosphere (Bruneton, multi-scatter LUT, ozone, aerial perspective)
- Height Fog (2-layer, directional inscattering, sky→fog injection)
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

### Remaining gaps
- Material system (implementing now)
- Volumetric fog (froxel)
- Volumetric clouds
- Decals (DBuffer)
- Area lights (LTC)
- Shading models (SSS, ClearCoat, Hair)
- LOD system
- PostProcess volumes
