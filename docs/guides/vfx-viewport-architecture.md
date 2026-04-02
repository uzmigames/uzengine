<!-- doc-meta
title: VFX Viewport Architecture
category: guide
theme: rendering
created: 2026-03-26
updated: 2026-03-27
status: active
author: claude
-->

# VFX Viewport Architecture

## Overview

The VFX Viewport demo (`demos/vfx_viewport/`) is a standalone DX12 application that showcases the engine's GPU-driven VFX system alongside a complete forward rendering pipeline. It inherits from `DemoApplication` and uses the engine's RHI abstraction, ShaderManager (SM6.6/SM6.0), PipelineLibrary, and CBRingAllocator.

This replaces the legacy `tools/uz_viewport.cpp` monolith (5095 lines, raw DX12, SM5.0 FXC) with standard engine infrastructure.

## File Structure

The demo is organized into logical modules:

```
demos/vfx_viewport/
├── build.zig               — Zig build script (links runtime.lib + editor.lib)
├── build.zig.zon           — Package metadata
├── main.cpp                — Entry point, RenderDoc init, CLI arg parsing
├── vfx_viewport_demo.h     — Demo class declaration (VFXViewportDemo : DemoApplication)
├── vfx_viewport_demo.cpp   — Main implementation (~1700 lines)
├── vfx_viewport_mesh.h     — Procedural mesh generation declarations
├── vfx_viewport_mesh.cpp   — Mesh builders (sphere, cube, plane, cylinder, torus)
├── vfx_viewport_vfx.h      — GPU particle system declarations
└── vfx_viewport_vfx.cpp    — GPU particle system implementation
```

## Rendering Pipeline (Frame Order)

Each frame executes the following rendering stages in order:

### 1. VFX Compute Dispatch
GPU-driven particle system in four compute shader stages:
- **Spawn CS** — Atomic pop from freeList, PCG hash random initialization, cone-spread velocity, position initialization within emitter radius
- **Update CS** — Gravity + damping integration, lifetime decrement, dead particles pushed back to freeList
- **IndirectArgs CS** — Single-thread dispatch writing DrawInstanced args (vertexCount = 4 × aliveCount) + next frame's DispatchIndirect args
- **Sort CS** — Bitonic merge sort: local phase (groupshared 256) + global merge phases. Sign-bit-flipped distance keys for back-to-front ordering.

### 2. Shadow Pass
- **Target**: 2048×2048 R32_FLOAT depth map
- **View**: Orthographic from light direction
- **Sampling**: PCF 3×3 in scene shader
- **Geometry**: Scene meshes rendered to depth only

### 3. Sky Pass
- **Output**: RGBA16Float HDR target (fullscreen)
- **Technique**: Rayleigh + Mie single-scattering atmosphere (Nishita/Preetham)
- **Alternative**: Background presets skip sky shader and clear to solid color
- **Output**: HDR radiance for tone mapping

### 4. Scene Pass
- **Target**: HDR (RGBA16Float) + D32Float depth
- **Technique**: Forward PBR mesh rendering
- **Lighting**: Directional light (NdotL) + up to 3 point lights (Karis 2013 attenuation)
- **Interaction**: Object rotation via Alt+LMB
- **Material**: Bindless SRV access for albedo, normal, roughness, metallic, AO, emissive

### 5. HDR Copy
- **Purpose**: Copy HDR to `m_preDistortionCopy` for grid ground reflection
- **Timing**: Before grid draws (grid must read pre-distortion HDR)
- **Format**: RGBA16Float

### 6. Editor Grid
- **Technique**: Infinite grid via fullscreen triangle + ray/XZ-plane intersection
- **Features**: Anti-aliased fwidth lines, screen-space planar reflection
- **Reflection**: Schlick Fresnel F0=0.04, mip 2.0 blur of pre-distortion HDR
- **Occlusion**: Depth reads from scene pass

### 7. Decal Pass
- **Geometry**: Forward projected decals (OBB projector)
- **Depth**: Bindless SRV read from scene depth
- **Control**: D key toggle visibility
- **Technique**: Projective decal with depth-based clipping

### 8. VFX Billboard Pass
- **Geometry**: Camera-facing quads via SV_VertexID
- **Features**: Soft particle depth fade (30cm), flipbook animation, alpha blend
- **Draw**: ExecuteIndirect (args from compute IndirectArgs CS)
- **Sorting**: Back-to-front via bitonic sort compute pass

### 9. Trail Pass
- **Simulation**: CPU-simulated ribbon strips
- **Upload**: Dynamic VB/IB per frame
- **Interpolation**: Catmull-rom width interpolation
- **Geometry**: Quad strips connecting trail points

### 10. Distortion Pass
- **Technique**: Two-pass accumulation + resolve
- **Accumulation**: Procedural heat-haze accumulation to distortion buffer
- **Resolve**: Fullscreen UV warp from distortion buffer applied to HDR
- **Effect**: Heat wave and refraction simulation

### 11. Bloom Pass
- **Technique**: GPU compute (part of engine BloomPass)
- **Stages**: Threshold → downsample chain → upsample chain
- **Output**: HDR with bloom contribution added

### 12. Tonemap Pass
- **Algorithm**: ACES filmic (Stephen Hill fit) + gamma 2.2 + vignette
- **Conversion**: HDR → LDR backbuffer
- **Output**: Ready for presentation

### 13. Stats Overlay
- **Content**: FPS, frame time, camera info
- **Render**: Direct to backbuffer
- **Font**: Engine's SDF font rendering

### 14. Shadow Debug (Optional)
- **Activation**: G key toggle
- **Display**: 256×256 overlay of shadow depth map
- **Location**: Bottom-right corner
- **Content**: Visualization of shadow atlas for debugging

### 15. Screenshot Capture (Conditional)
- **Trigger**: F9 key or --screenshot flag
- **Source**: Backbuffer copy
- **Format**: PNG or base64 to stdout

## Constant Buffer Layout

### PerFrameCB (768 bytes, register b1)

```cpp
cbuffer PerFrameCB : register(b1)
{
    float4x4 viewProj;              // c0-c3
    float4 cameraPos;               // c4
    float4 timeParams;              // c5 — totalTime, dt, frameIndex, 0
    float4 lightDir;                // c6
    float4 lightColor;              // c7
    float4 ambientColor;            // c8
    float4x4 invViewProj;           // c9-c12
    float4x4 shadowViewProj;        // c13-c16
    float nearPlane;                // c17.x
    float farPlane;                 // c17.y
    float exposure;                 // c17.z
    float pad0;                     // c17.w
    float4 _pad1[16];               // c18-c32
    uint numPointLights;            // c33.w
    float4 pointLights[3][2];       // c34-c39 (posRadius, colorIntensity × 3)
    float4 _pad2[8];                // c40-c47
};
```

### PerObjectCB (register b2)

Material and transform properties per object:

```cpp
cbuffer PerObjectCB : register(b2)
{
    // Bindless SRV indices
    uint srvAlbedo;
    uint srvNormal;
    uint srvRoughness;
    uint srvMetallic;
    uint srvAO;
    uint srvEmissive;
    uint srvOpacity;
    uint materialFlags;

    // Transforms
    float4x4 model;
    float4x4 normalMatrix;

    // Material properties
    float4 baseColorTint;
    float roughnessMultiplier;
    float metallicMultiplier;
    float emissiveIntensity;
    float pad0;

    float4 emissiveColor;
    float2 uvScale;
    float2 uvOffset;
};
```

### VFX Compute CBs (various)

- **VFXSpawnCB**: Emitter position, velocity cone angle, radius, spawn rate
- **VFXUpdateCB**: Gravity, damping, lifetime parameters
- **VFXBillboardConstants (b0)**: Root constants for quad scale, flipbook dimensions, animation speed
- **VFXBillboardCB (b2)**: Material blend mode, color tint, soft particle fade distance
- **VFXEmitterCB**: Per-emitter configuration (active flag, emission budget, pool size)

## Shared Root Signature Layout

All PSOs share one root signature for flexibility:

```
b0 = Root constants (64 bytes max) or root CBV
b1 = PerFrameCB (global frame parameters)
b2 = PerObjectCB (per-object material + transform)
b4 = Additional CBV (effect-specific parameters)
Visibility: ALL (shared across VS/PS/CS)
Bindless: ResourceDescriptorHeap[] access in SM6.6 mode
```

This unified layout enables rapid PSO switching and resource rebinding without root signature changes.

## GPU Resource Management

### Render Targets

| Resource | Format | Purpose |
|----------|--------|---------|
| HDR Target | RGBA16Float | Main scene accumulation, tone-mapped in final pass |
| Depth Buffer | D32Float | Scene depth (DSV for depth writing, SRV for reads) |
| Distortion RT | RGBA16Float | Accumulation buffer for heat haze effect |
| Pre-distortion Copy | RGBA16Float | HDR snapshot before distortion/grid (for reflection) |

### Shadow Resources

| Resource | Format | Size | Purpose |
|----------|--------|------|---------|
| Shadow Map | R32_TYPELESS | 2048×2048 | D32Float DSV (depth write) + R32Float SRV (PCF read) |

### VFX Buffers (Structured)

Ping-pong SoA (Structure-of-Arrays) layout for cache efficiency:

```
50K particles × 6 channels × 2 sets (ping-pong):
  - Position (R32G32B32_FLOAT)
  - Velocity (R32G32B32_FLOAT)
  - Lifetime (R32_FLOAT)
  - Rotation (R32_FLOAT)
  - Scale (R32_FLOAT)
  - Color (R8G8B8A8_UNORM)

Metadata buffers:
  - freeList (uint[50K]) — stack of dead particle indices
  - aliveCount (uint) — atomic counter, read via staging buffer
  - indirectArgs (DrawIndirectArgs) — written by IndirectArgs CS
  - sortIndices (uint[50K]) — scratch buffer for sort passes
```

### Trail Buffers

Dynamic upload buffers per frame:

```
VB: 1024 vertices (float3 position + float4 color + float2 uv)
IB: 3072 indices (uint16)
CB: Trail material properties (width, color, animation)
```

Regenerated each frame from trail path data.

## Engine Infrastructure Used

| Component | Usage |
|-----------|-------|
| **DemoApplication** | Base class: window, device, swapchain, input handling, camera control, frame loop |
| **ShaderManager** | SM6.6/SM6.0 HLSL compilation via DXC with async caching |
| **PipelineLibrary** | Cached PSO creation (graphics + compute) with thread-safe access |
| **CBRingAllocator** | Per-frame constant buffer uploads (ring buffer, CPU-GPU sync) |
| **EditorGrid** | Infinite anti-aliased grid with ground reflection sampling |
| **StatsOverlay** | FPS/frame time/camera info real-time display |
| **ScreenshotCapture** | Backbuffer capture to PNG or base64 stdout |
| **BloomPass** | GPU compute bloom (threshold + downsample + upsample chains) |
| **TrailSystem** | CPU trail simulation and ribbon mesh generation per frame |
| **TextureLoader** | PBR texture loading from CLI argument paths |
| **RenderDocCapture** | Frame capture integration (F11 to capture, renderdoc.dll optional) |

## Material Presets

Eight physically-based material presets for quick scene composition:

| Preset | Base Color | Metallic | Roughness | Use Case |
|--------|-----------|----------|-----------|----------|
| Red Plastic | 0.8, 0.1, 0.1 | 0.0 | 0.5 | Colored dielectric |
| Gold Metal | 1.0, 0.84, 0.0 | 1.0 | 0.2 | Polished metal |
| Rough Stone | 0.5, 0.5, 0.5 | 0.0 | 0.9 | Matte surface |
| Polished Chrome | 0.9, 0.9, 0.9 | 1.0 | 0.05 | Mirror-like |
| Rubber | 0.2, 0.2, 0.2 | 0.0 | 0.8 | Dark matte |
| Copper | 0.95, 0.64, 0.54 | 1.0 | 0.3 | Oxidized metal |
| Ceramic White | 0.95, 0.95, 0.95 | 0.0 | 0.1 | Glazed ceramic |
| Emissive Blue | 0.0, 0.2, 1.0 | 0.0 | 0.5 | Glow source |

## VFX Emitter Presets

Five preconfigured emitter templates demonstrating different VFX effects:

| Preset | Colors | Velocity | Life | Animation | Effect |
|--------|--------|----------|------|-----------|--------|
| Fire Fountain | Red→Yellow | 0→1500 cm/s upward (Z-up) | 3.0s | 1×1 (single frame) | Warm flame fountain |
| Smoke Plume | Gray gradients | Slow rise (50 cm/s) | 3.0s | 2×2 flipbook | Dense smoke column |
| Sparks | Bright white/yellow | High velocity radial | 0.5s | 1×1 (single) | Energetic burst |
| Magic Orb | Blue→Purple | Orbital paths | 2.0s | 2×2 flipbook | Mystical aura |
| Explosion Debris | Orange/gray mix | Radial burst | 1.0s | 1×1 (single) | Impact effect |

Each preset is fully customizable via UI or Lua API.

## Key Takeaways

- **Modular rendering**: 15-stage pipeline enables independent iteration on each system (shadows, VFX, post-process, etc.)
- **GPU-driven particles**: Compute-based spawn/update/sort eliminates CPU bottlenecks and enables 50K+ particles at 60 FPS
- **Unified resource binding**: Shared root signature across all PSOs reduces switching overhead and simplifies material management
- **Engine-standard infrastructure**: Leverages DemoApplication, ShaderManager, PipelineLibrary, and CBRingAllocator for maintainability
- **Visual debugging**: Shadow debug overlay, RenderDoc integration, and screenshot capture enable rapid iteration

## References

- [VFX System Overview](../analysis/vfx-system-overview.md)
- [GPU Compute Patterns](../guides/gpu-compute-patterns.md)
- [DemoApplication Base Class](../api/demo-application.md)
- [Render Graph Architecture](../guides/render-graph-architecture.md)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-26 | Initial creation. Documented file structure, 15-stage rendering pipeline, CB layout, GPU resource management, material/emitter presets, and engine infrastructure. |
