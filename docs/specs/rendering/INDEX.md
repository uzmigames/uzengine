# Rendering Specs

| Spec | Description |
|------|-------------|
| [renderer.md](renderer.md) | Hybrid Deferred PBR pipeline, G-Buffer layout, shadow integration, post-process |
| [render-graph.md](render-graph.md) | Frame resource abstraction, pass dependencies, D3D12 barrier automation, resource aliasing |
| [shader-system.md](shader-system.md) | HLSL/GLSL pipeline, DXC compilation, hot-reload, built-in shaders |
| [shader-variants.md](shader-variants.md) | Permutation system: variant keys, PSO cache, offline DXC compilation |
| [shader-library.md](shader-library.md) | Shared HLSL includes: BRDF, lighting, shadows, skinning, tonemapping, noise, GlobalParams cbuffer |
| [screen-space-effects.md](screen-space-effects.md) | SSAO (HBAO+), SSR (Hi-Z ray march), TAA (temporal resolve + Halton jitter), velocity buffer |
| [global-illumination.md](global-illumination.md) | Hybrid GI: static lightmaps + dynamic DDGI probes + reflection captures |
| [point-spot-light-shadows.md](point-spot-light-shadows.md) | Point light dual-paraboloid shadows, spot light perspective shadows, shadow atlases, PCF |
| [gpu-instancing.md](gpu-instancing.md) | Dynamic GPU instancing: BatchBuilder, StructuredBuffer transforms, SV_InstanceID |
| [particle-system.md](particle-system.md) | GPU compute particle simulation, `.uzfx` format, behavior modules, billboard rendering, LOD |
