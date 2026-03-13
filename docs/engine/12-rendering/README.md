# Rendering Pipeline — Complete Reference

> **Status**: Partial (Milestones 1.2–2.0)
> **Source**: `runtime/engine/renderer/`
> **Depends on**: RHI (§03), Shader Pipeline (§05), Entity/Component (§10)

## Section Overview

The rendering pipeline system is split across multiple documents covering architecture, data structures, GPU binding, lighting models, and the render graph.

| Document | Status | Topic | For... |
|----------|--------|-------|--------|
| [01-render-world.md](01-render-world.md) | Planned (2.0) | Render world, sync point, proxy architecture | Scene rendering organization |
| [02-visibility-pipeline.md](02-visibility-pipeline.md) | Planned (2.0) | Frustum culling, visibility buffer, LOD selection | Renderer optimization |
| [03-frame-pipeline.md](03-frame-pipeline.md) | Planned (2.0) | Complete per-frame flow, deferred/forward passes, post-processing | Frame structure |
| [04-render-graph.md](04-render-graph.md) | Done (1.2) | Render graph architecture, passes, resource management, barriers | Pass composition |
| [05-render-graph-guide.md](05-render-graph-guide.md) | Done (1.2) | Render graph usage guide, pass registration, execution | Implementing passes |
| [06-pbr-material-system.md](06-pbr-material-system.md) | **Done (1.2)** | **PBRMaterial struct (48B), MaterialBuffer GPU upload, defaults** | **Material data** |
| [07-sort-key-architecture.md](07-sort-key-architecture.md) | **Done (1.2)** | **64-bit sort key layout, opaque/transparent ordering, batching** | **Draw call ordering** |
| [08-forward-pbr-pass.md](08-forward-pbr-pass.md) | **Done (1.2)** | **Forward PBR render pass, render graph integration, per-object CB binding** | **Forward rendering** |
| [09-cook-torrance-brdf.md](09-cook-torrance-brdf.md) | **Done (1.2)** | **Cook-Torrance BRDF, GGX NDF, Smith geometry, Schlick Fresnel** | **PBR lighting math** |
| [07-shader-compatibility.md](07-shader-compatibility.md) | **Done (1.2)** | **Cross-backend shader compilation, SM6.6 bindless vs SM5, DXIL/DXBC/SPIR-V targets** | **Multi-target shaders** |
| [06-shadow-system.md](06-shadow-system.md) | **Done (Phase 2)** | **Cascaded Shadow Maps, frustum splitting, atlas layout, PCF filtering, cascade blending** | **Directional light shadows** |
| [06-lighting.md](06-lighting.md) | Planned (2.0+) | Directional lights, point lights, spot lights, global illumination | Lighting systems |
| [10-renderer-passes.md](10-renderer-passes.md) | **Done (1.2–Phase 2)** | **PBR, GBuffer, Lighting, Shadow passes — complete reference** | **All render passes** |
| [11-multi-material-multi-uv.md](11-multi-material-multi-uv.md) | Planned (3+) | Submesh ranges, multi-material per mesh, UV channels (UV0–UV3), glTF import | Multi-material rendering |
| [12-vertex-layout.md](12-vertex-layout.md) | Done (1.2) | Vertex layout system, format flags, input element generation, D3D12 binding | Vertex structure management |
| [13-scene-lighting-helper.md](13-scene-lighting-helper.md) | **Done (1.2)** | **SceneLighting struct, BuildPerFrameCB() helper** | **Per-frame CB setup** |
| [07-post-processing.md](07-post-processing.md) | Planned (2.0) | Post-process chains, bloom, tonemap, SSAO, TAA, upscaling | Post-processing effects |

## Quick Navigation

### I want to...

**Understand how rendering works**

1. Start with [00-overview/](../00-overview/) for high-level architecture
2. Read [03-frame-pipeline.md](03-frame-pipeline.md) for per-frame flow
3. Read [04-render-graph.md](04-render-graph.md) for pass composition

**Implement a custom render pass**

1. Read [05-render-graph-guide.md](05-render-graph-guide.md) for pass registration
2. Reference [08-forward-pbr-pass.md](08-forward-pbr-pass.md) as an example implementation
3. Study [04-render-graph.md](04-render-graph.md) for resource management

**Work with materials**

1. Read [06-pbr-material-system.md](06-pbr-material-system.md) for data layout
2. Read [09-cook-torrance-brdf.md](09-cook-torrance-brdf.md) for lighting calculations
3. Reference [08-forward-pbr-pass.md](08-forward-pbr-pass.md) for shader integration

**Optimize draw call ordering**

1. Read [07-sort-key-architecture.md](07-sort-key-architecture.md) for the 64-bit key design
2. Understand opaque vs. transparent ordering
3. Learn batching strategies

**Implement custom shader code**

1. Read [09-cook-torrance-brdf.md](09-cook-torrance-brdf.md) for BRDF math
2. Reference `runtime/shaders/runtime/mesh_ps.hlsl` for implementation examples
3. See [08-forward-pbr-pass.md](08-forward-pbr-pass.md) for GPU binding patterns

**Support multiple GPU architectures (DX12, DX11, Vulkan)**

1. Read [07-shader-compatibility.md](07-shader-compatibility.md) for cross-backend strategies
2. Understand the `#ifdef UZ_BINDLESS` dual-path design
3. Learn SM5 register-bound texture conventions for fallback paths

**Support multi-material meshes and UV channels**

1. Read [11-multi-material-multi-uv.md](11-multi-material-multi-uv.md) for submesh architecture
2. Understand DrawCommand extensions (indexOffset, indexCount, vertexOffset)
3. Learn vertex format flags and multi-UV layout (UV0, UV1, UV2)
4. Study RHI backend changes (DX12, Vulkan, Metal input layouts)

**Implement cascaded shadow maps**

1. Read [06-shadow-system.md](06-shadow-system.md) for CSM architecture and split scheme
2. Understand the practical split algorithm and texel snapping
3. Learn cascade blending and PCF filtering for soft shadows
4. Reference the shadow pass setup in render graph integration

## Milestone Status

### Milestone 1.2 (Forward PBR) — DONE

**Implemented:**

- ✅ PBRMaterial struct (48 bytes, Cook-Torrance parameters)
- ✅ MaterialBuffer GPU upload with dirty tracking
- ✅ Sort-key architecture (64-bit opaque/transparent ordering)
- ✅ Forward PBR pass with render graph integration
- ✅ Per-object constant buffer ring allocation
- ✅ Cook-Torrance BRDF with GGX/Smith/Schlick
- ✅ Mesh pixel shader (Cook-Torrance + Reinhard tonemap)
- ✅ SM6.6 bindless + SM5 fallback paths
- ✅ Editor grid, stats overlay, text rendering, screenshot capture

**Test coverage:** 6069 FPS clear screen demo (RTX 4090), 183 RHI tests passing

### Phase 3A Rendering Track — PLANNED

**3.3.0 — Deferred Pipeline**
- [ ] G-Buffer layout (Albedo RGBA8, Normal RGB10A2, MetalRough RGBA8, Depth D32)
- [ ] Geometry pass writing to G-Buffer MRTs
- [ ] Deferred lighting pass (Cook-Torrance evaluation from G-Buffer)
- [ ] Forward transparent pass composited over deferred output
- **Depends on**: 3.2.0 (forward PBR — done)

**3.4.0 — Shadow Maps — DONE (Phase 2)**
- [x] Cascaded shadow maps (CSM 4-cascade, practical split scheme)
- [x] Depth-only pass for shadow rendering (render graph integrated)
- [x] Shadow atlas 2×2 grid layout and packing
- [x] PCF filtering (3×3 soft shadows)
- [x] Cascade blending and texel snapping (no swimming)
- [x] ShadowDataCB (512 bytes, root CBV at b4)
- [x] SM6.6 bindless + SM5 fallback
- [x] 35 unit tests (ComputeCascades, split distances, bias computation)
- **Dependencies**: DirectionalLight component (phase 3+), depth pipeline, render graph

**3.5.0 — Basic Post-Processing**
- [ ] ACES tonemap operator (industry standard)
- [ ] Bloom effect (5-mip Gaussian pyramid)
- [ ] FXAA screen-space anti-aliasing
- [ ] Post-process compositing pass in render graph
- **Dependencies**: Render targets, fullscreen pass utilities

**3.6.0 — Multi-Light Support**
- [ ] Point light evaluation in PBR shader
- [ ] Spot light support with angle attenuation
- [ ] Light array in PerFrameCB (max 8 dynamic lights)
- [ ] CPU-side distance culling for light selection
- [ ] Per-light constant buffers for deferred passes
- **Dependencies**: LightComponent (point, spot, directional types)

**3.7.0 — IBL Environment Lighting**
- [ ] BRDF lookup texture (LUT) precomputation
- [ ] Irradiance convolution from cubemap
- [ ] Prefiltered specular cubemap with LOD (5+ mips)
- [ ] SkyLight component for environment contribution
- [ ] Bindless cubemap sampling in PBR shader
- **Dependencies**: Cubemap asset format, mipmap generation, offline preprocessing

**3.8.0 — Sky & Atmosphere**
- [ ] Bruneton physical atmosphere model
- [ ] Rayleigh and Mie scattering in sky shader
- [ ] Exponential height fog with camera fade
- [ ] Sky-to-IBL capture (dynamic time-of-day support)
- [ ] Phase function modeling for volumetric effects
- **Dependencies**: Sky material, atmosphere parameters (turbidity, sun angle)

**3.9.0 — Advanced Post-Processing**
- [ ] GTAO (Ground Truth Ambient Occlusion)
- [ ] SSR (Screen-space reflections) with Hi-Z traversal
- [ ] TAA (Temporal anti-aliasing) with motion vectors
- [ ] Depth of field (circle of confusion, separation layers)
- [ ] Motion blur (per-pixel velocity)
- [ ] Auto-exposure (histogram-based)
- [ ] Color grading (LUT-based)
- [ ] Contact shadows from directional light
- [ ] Film grain and vignette effects
- [ ] Chromatic aberration
- [ ] CAS (Contrast adaptive sharpening)
- [ ] PostProcessVolume for local effect overrides
- **Dependencies**: Full forward/deferred pipeline, motion vector pass, multiple render targets

**3.10.0 — Volumetric Effects**
- [ ] Ray-marched volumetric clouds (3D Perlin noise)
- [ ] Temporal reprojection for cloud stability
- [ ] God rays from directional light through clouds
- [ ] Fog volume sampling in forward/deferred passes
- **Dependencies**: 3D noise textures, volumetric lighting buffer, ray-march shader

**3.11.0 — glTF & Mesh Primitives**
- [ ] glTF 2.0 importer using fastgltf library
- [ ] Multi-submesh support in Mesh asset
- [ ] Primitive generators: sphere, cone, cylinder, plane, torus
- [ ] Node hierarchy from glTF import
- [ ] Material metadata import (metallic, roughness textures)
- **Dependencies**: Asset pipeline, Material format (3.12.0)

**3.12.0 — Material Format & Management**
- [ ] .uzmat file format (JSON-based material definition)
- [ ] MaterialManager with caching and hot-reload
- [ ] Texture path resolution relative to material file
- [ ] Material variants and parameter overrides
- [ ] Integration with Lua scripting for dynamic materials
- **Dependencies**: Asset format spec, file I/O, texture streaming

### Phase 5 — Editor Rendering — PLANNED

**5.1.0 — Editor Visual Tools**
- [ ] Transform gizmos (translate, rotate, scale)
- [ ] Node picking via pick buffer (object ID + face ID per pixel)
- [ ] Selection outline rendering
- [ ] Node-type specific gizmos
- [ ] Debug draw API for in-editor visualization
- [ ] Wireframe/normal/texel density visualization modes
- **Dependencies**: Selection system, picking ray-cast, overlay rendering

### Phase 7+ — Advanced Rendering — FUTURE

- **Tiled Forward+ Light Culling** — Compute shader light tile classification
- **Light Probes (SH9)** — Spherical harmonics global illumination probes
- **Reflection Probes** — Pre-baked and dynamic reflection capture
- **Virtual Shadow Maps** — Dynamically allocated shadow texture space
- **SSGI / DXR Global Illumination** — Ray-traced or screen-space GI
- **Virtual Geometry (Nanite-style)** — Hierarchical mesh compression

## Key Concepts

### PBR Material System

Each material is a **48-byte struct** stored in a GPU StructuredBuffer:

```cpp
struct PBRMaterial {
    Vec4   baseColor;    ///< Albedo + opacity
    float  metallic;     ///< [0, 1]
    float  roughness;    ///< [0, 1] — perceptual
    float  ao;           ///< [0, 1] — ambient occlusion
    float  _pad0;        ///< Alignment
    Vec4   emissive;     ///< Color + intensity
};  // 48 bytes
```

Materials are referenced by **index** from per-object constant buffers, enabling material-based sort keys without per-material PSO switches.

**See**: [06-pbr-material-system.md](06-pbr-material-system.md)

### Sort-Key Architecture

A **64-bit key** encodes the most expensive GPU state changes:

```
[63:56] Pipeline ID      (8 bits  — PSO switches are most expensive)
[55:48] Material ID      (8 bits  — descriptor table changes)
[47:24] Depth            (24 bits — front-to-back opaque, back-to-front transparent)
[23:0]  Mesh ID          (24 bits — VB/IB rebind is least expensive)
```

Ascending sort by this key minimizes GPU state transitions.

**See**: [07-sort-key-architecture.md](07-sort-key-architecture.md)

### Forward PBR Pass

A single render graph pass that:

1. Clears backbuffer and depth
2. Binds the PBR pipeline, per-frame CB, and material buffer
3. Iterates the sorted DrawList
4. For each draw: allocates a per-object CB, sets transforms/material, issues DrawIndexed
5. Outputs linearized Cook-Torrance BRDF, Reinhard tonemapped to sRGB

**See**: [08-forward-pbr-pass.md](08-forward-pbr-pass.md)

### Cook-Torrance BRDF

A physically-based lighting equation combining:

- **GGX Normal Distribution** — statistical microfacet spread
- **Smith Geometry Function** — microfacet visibility/shadowing
- **Schlick Fresnel** — angle-dependent reflectance
- **Lambertian Diffuse** — scattered light (energy-conserving)

Parameters: metallic, perceptual roughness, ambient occlusion, emissive.

**See**: [09-cook-torrance-brdf.md](09-cook-torrance-brdf.md)

## Architecture Decisions

### Why PBRMaterial struct instead of per-material PSO?

- **Smaller state space** — 8-bit material ID vs. hundreds of PSO handles
- **Dynamic material changes** — UpdateMaterial() costs ~memcpy, no shader recompilation
- **Better batching** — sort key includes material; similar materials batch together
- **Simpler shader** — single unified shader with PBR parameters, not permutation variants

### Why 64-bit sort key?

- **Efficient** — fits in a single 64-bit integer, cache-friendly
- **Flexible** — 8-bit field granularity allows future refinement
- **Proven** — matches industry standards (UE4/5, Godot, most engines)
- **Supports all passes** — opaque + transparent + shadow all use the same layout

### Why SM6.6 bindless + SM5 fallback?

- **Modern GPU efficiency** — bindless eliminates descriptor table switches
- **Compatibility** — SM5 fallback for older NVIDIA/AMD/Intel GPUs
- **Single shader source** — `#ifdef UZ_BINDLESS` handles both paths
- **Future-proof** — Vulkan native support; DX11 compatibility layer via DXVK

**Complete implementation strategy**: [07-shader-compatibility.md](07-shader-compatibility.md) documents cross-backend compilation, resource binding models, and validation for all 7 runtime shaders (mesh, gbuffer, deferred lighting, shadow, fullscreen, gbuffer debug).

## References

### Engine Comparisons

This documentation provides detailed comparisons with:

- **Unreal Engine 5.6** (`W:\UE_5.6_Source`) — reference for AAA features
- **Godot 4.x** (`E:\godot`) — alternative architectural approaches
- **Unity** (general knowledge) — for industry context

All comparisons are **clean-room** — study approach only, never copy code.

### Publications

Key references for implementation:

1. **"Real Shading in Unreal Engine 4"** (Karis 2013) — Smith GGX parameterization
2. **"Physically Based Rendering"** (Pharr, Jakob, Humphreys) — complete BRDF derivation
3. **DirectX 12 Documentation** — GPU resource binding, barriers, render passes
4. **Disney Principia** (Burley 2012) — perceptual roughness remapping

## Rules & Best Practices

### Materials

1. **ALWAYS use perceptual roughness (0–1)** — never expose raw alpha to artists
2. **ALWAYS validate capacity** — AddMaterial() returns UINT32_MAX if full
3. **ALWAYS call Upload() once per frame** — before render pass execution

### Sort Keys

1. **ALWAYS use Opaque() for non-transparent geometry** — front-to-back minimizes overdraw
2. **ALWAYS use Transparent() for transparent geometry** — back-to-front ensures correct blending
3. **NEVER mix transparent and opaque keys** — they require opposite depth ordering

### Render Passes

1. **ALWAYS use the render graph** — never raw command lists
2. **ALWAYS declare resource access** — builder validates transitions
3. **ALWAYS use per-object CB ring allocator** — triple-buffered to avoid stalls

## Implementation Roadmap

The rendering system evolves through phases, starting with core forward PBR (✅ 1.2) and expanding to industry-standard techniques (deferred, shadows, post-processing) in Phase 3A, then editor tooling (Phase 5) and advanced techniques (Phase 7+).

**See "Milestone Status"** above for the complete phase-by-phase breakdown, including Phase 3A rendering track (3.3.0–3.12.0) and Phase 5 editor tools (5.1.0).

Key decisions:
- **Forward PBR first** (1.2) validates material system and render graph
- **Deferred pipeline** (3.3.0) adds multi-light efficiency for many-light scenarios
- **Shadows** (3.4.0) enable realistic depth cues and self-shadowing
- **Post-processing** (3.5.0–3.9.0) polish visual quality (tonemap, bloom, GTAO, TAA, etc.)
- **IBL & Sky** (3.7.0–3.8.0) enable photorealistic lighting from sky environment
- **Advanced effects** (3.10.0–3.12.0) volumetric clouds, glTF import, material system
- **Phase 7+ techniques** virtual geometry, DXR GI for next-generation fidelity

## File Manifest

```
12-rendering/
├── README.md                        # This file
├── 01-render-world.md              # Render world, sync point, proxy architecture
├── 02-visibility-pipeline.md       # Culling, visibility buffer, LOD
├── 03-frame-pipeline.md            # Per-frame flow, deferred/forward/post
├── 04-render-graph.md              # Render graph core (done, 1.2)
├── 05-render-graph-guide.md        # Render graph usage guide (done, 1.2)
├── 06-pbr-material-system.md       # ★ Material struct, GPU upload (1.2)
├── 07-sort-key-architecture.md     # ★ 64-bit sort key design (1.2)
├── 07-shader-compatibility.md      # ★ Cross-backend compilation, SM6.6/SM5 dual-path (1.2)
├── 08-forward-pbr-pass.md          # ★ Forward pass implementation (1.2)
├── 09-cook-torrance-brdf.md        # ★ BRDF math & shaders (1.2)
├── 10-renderer-passes.md           # ★★ Complete pass reference (1.2–Phase 2)
├── 06-shadow-system.md             # ★★ Cascaded shadow maps, atlas, PCF (Phase 2)
├── 06-lighting.md                  # Lighting systems (planned 2.0+)
├── 11-multi-material-multi-uv.md   # ★★★ Submesh ranges, UV0/1/2, multi-material (Phase 3+)
├── 12-vertex-layout.md             # ★ Vertex layout factory (1.2)
├── 13-scene-lighting-helper.md     # ★ SceneLighting & BuildPerFrameCB() (1.2)
└── 07-post-processing.md           # Post-processing effects (planned 2.0)
```

(★ = New in Milestone 1.2, ★★ = New in Phase 2, ★★★ = New in Phase 3+)
