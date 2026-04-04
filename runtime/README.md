# @uzmigames/runtime

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)]()
[![Zig](https://img.shields.io/badge/Zig-0.15-orange.svg)]()
[![MSVC](https://img.shields.io/badge/MSVC-17.14-blue.svg)]()
[![Clang](https://img.shields.io/badge/Clang-21-green.svg)]()

UzEngine core runtime — deferred PBR renderer with industry-standard lighting, atmosphere, and post-processing. Multi-backend RHI (DX12 SM6.8 bindless, DX12 SM5, DX11, Vulkan, Metal).

**Standalone C++ static library.** Consumed by demos, CLI tools, or the VSCode extension via N-API.

## Build

```bash
cd runtime && zig build           # Build runtime static lib
cd demos/pbr_test && zig build    # Build PBR demo
zig build test                    # Run all tests (~3600 tests)
```

## Rendering Features (v0.2.33)

### Lighting
- Deferred PBR: GGX NDF + Smith GGX visibility + Schlick Fresnel + Disney Burley diffuse
- Multiscatter energy compensation (Kulla-Conty/Turquin 2018)
- Directional light + 64 point lights + 16 spot lights (cone attenuation)
- IBL: SH L2 irradiance + prefiltered specular (5 mips) + BRDF LUT
- Contact shadows (screen-space ray march, 8 steps)

### Shadows
- 4-cascade CSM with per-cascade PCF radius
- Distance field shadows (SDF atlas, cone trace, tile culling)
- Per-cascade bias + sharpen + overblur correction

### Atmosphere
- Sky atmosphere (Bruneton single-scatter + multi-scatter LUT + ozone)
- Transmittance LUT (256x64 Bruneton 2017)
- Aerial perspective (per-pixel 8-step ray march)
- Height fog (2-layer exponential + directional inscattering + sky injection)
- Sky cubemap capture (compute + PS paths) for IBL

### Post-Processing
- TAA (Halton jitter, YCoCg neighbourhood clamp, history ping-pong)
- SSR (Hi-Z ray trace + Fresnel resolve)
- GTAO (12-tap, bilateral denoise, temporal accumulation, multi-bounce)
- Bloom (6-mip pyramid, Karis average, per-stage tints)
- ACES tone curve (3-segment S-curve, 5 artist params)
- Auto-exposure (histogram, percentile-based)
- DOF (CoC setup + 5-ring gather blur)
- Motion blur (velocity gather, depth-aware, adaptive samples)
- Color grading LUT (32x32x32 3D, full CombineLUTs pipeline)
- Film grain + vignette + chromatic aberration

### Materials
- PBR textures: albedo, normal, roughness, metallic, AO, emissive
- Emissive materials (HDR emission + bloom)
- Material graph system (expressions, baker, GPU baker, shader cache)

### Backends
- DX12 SM6.8 (bindless ResourceDescriptorHeap)
- DX12 SM5 (descriptor tables)
- DX11 (feature-level 11.1)
- Vulkan (via volk + VMA)
- Metal (macOS)

## Directory Structure

```
runtime/
├── engine/
│   ├── app/              Application framework, engine loop
│   ├── ecs/              EnTT ECS (scene, lighting, mesh, environment)
│   ├── renderer/
│   │   ├── core/         ConstantBuffers, PipelineLibrary, GBuffer, ShaderManager
│   │   ├── passes/       Render graph passes (geometry, lighting, atmosphere, postprocess)
│   │   ├── atmosphere/   AtmosphereRenderer, sky cubemap capture
│   │   ├── fog/          FogRenderer (height fog + sky injection)
│   │   ├── shadows/      ShadowRenderer, SDF atlas, shadow cascade
│   │   ├── lighting/     LightRenderer (point + spot extraction)
│   │   ├── ibl/          IBLBaker (irradiance + prefiltered + SH projection)
│   │   ├── material/     Material system (graph, baker, shader cache)
│   │   ├── camera/       Camera, FPS controller
│   │   └── render_graph/ RgBuilder, RgResources
│   ├── rhi/              RHI abstraction
│   │   ├── dx12/         DX12 backend (D3D12MA, Agility SDK)
│   │   ├── dx11/         DX11 backend
│   │   ├── vulkan/       Vulkan backend (volk + VMA)
│   │   └── metal/        Metal backend (macOS)
│   ├── shader/           Shader compiler (DXC/FXC), hot-reload, cache
│   ├── scripting/        LuaJIT 2.1 VM + API bindings
│   ├── math/             Vec2/3/4, Mat4, Quaternion, Transform
│   └── job/              Job system (fence + counter)
├── shaders/runtime/      HLSL shaders
│   ├── geometry/         gbuffer_ps, depth_prepass_ps
│   ├── lighting/         deferred_lighting_ps, shadow shaders
│   ├── atmosphere/       sky_atmosphere_ps, height_fog_ps, transmittance_lut_cs
│   ├── postprocess/      bloom, gtao, taa, ssr, tonemap, dof, motion_blur
│   ├── shadows/          dfs_cone_trace_cs, dfs_cull_objects_cs
│   └── include/          pbr_common, lighting, shadow_common, film_tonemap
├── demos/                Standalone demo apps (pbr_test, deferred_test, etc.)
├── tests/                GoogleTest suites (~3600 tests)
├── platform/windows/     Win32 window, input, crash handler
└── thirdparty/           All dependencies committed (spdlog, tracy, entt, luajit, etc.)
```

## Code Quality

- **~3600 tests** across 86+ suites (GoogleTest)
- **Three compilers**: Zig, MSVC, Clang — all zero warnings
- **95%+ coverage** required for all new code
- **Rust-style `///` documentation** on all public APIs
- **Published reference citations** (`@ref Author Year "Title"`) on all rendering code

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
