# Module: Renderer

> **Location**: `src/renderer/`
> **Type**: Static library (`uz_renderer.lib`)
> **Language**: C++23
> **GPU required**: Yes (D3D12) — except `ShaderCompiler` which can run headless for Build

---

## Responsibility

The Renderer module **produces frames**. It takes a stable read-only snapshot of ECS data from the Engine module and issues GPU commands to produce rendered images. It is the only module that owns D3D12 objects.

The Renderer is used in three contexts:

| Context | What it does |
|---------|-------------|
| **Runtime (editor mode)** | Full pipeline: render → readback → JPEG → WebSocket stream to VSCode |
| **Runtime (shipping game)** | Full pipeline: render → present to swap chain. No readback, no streaming |
| **Build tool (headless)** | `ShaderCompiler` only — compile HLSL → DXIL without a window or D3D12 device |

---

## Subsystems

### `dx12/` — Device & Core
Direct3D 12 object ownership. These are never shared outside this module.

| File | Purpose |
|------|---------|
| `device.h/cpp` | `D3D12CreateDevice`, adapter selection (prefer discrete GPU) |
| `command_queue.h/cpp` | Direct + copy command queues, `ExecuteCommandLists`, `Signal`/`Wait` |
| `swap_chain.h/cpp` | `CreateSwapChainForHwnd`, resize, back buffer management |
| `descriptor_heap.h/cpp` | CPU/GPU descriptor heaps (RTV, DSV, CBV/SRV/UAV), linear allocator |
| `fence.h/cpp` | `ID3D12Fence` RAII wrapper, `WaitForGpu()` helper |

### `render_graph/` — Frame Graph
Declarative pass system. Passes are registered per-frame, sorted by `sort_key`, barriers inserted automatically.

| File | Purpose |
|------|---------|
| `render_graph.h/cpp` | `RenderPass` registration, barrier batching (`D3D12_BARRIER_GROUP`), execution |
| `pass_context.h` | Per-pass data: command list, frame index, viewport, camera matrices |

**Passes registered by default** (in order):

```
ShadowPass             (sort_key=50)   — Directional CSM 3 cascades, PCF 3×3 (Phase 2)
PointLightShadowPass   (sort_key=110)  — Point light dual-paraboloid shadow atlas (Phase 2)
SpotLightShadowPass    (sort_key=120)  — Spot light perspective shadow atlas (Phase 2)
GBufferPass            (sort_key=200)  — Deferred: Albedo+AO (RT0), Normal+Rough+Metal (RT1), Emissive (RT2), Velocity (RT3, RG16F for TAA) (Phase 2/3)
DeferredLightingPass   (sort_key=300)  — PBR: GGX BRDF + IBL + shadow sampling (Phase 2)
SSAOPass               (sort_key=350)  — HBAO+ ambient occlusion, half-res read from GBuffer (Phase 2)
SSRPass                (sort_key=400)  — Hi-Z SSR: trace depth pyramid against velocity buffer (Phase 3)
SkyboxPass             (sort_key=450)  — Cubemap skybox, depth=EQUAL (Phase 2)
TransparencyPass       (sort_key=500)  — Sorted back-to-front forward alpha-blend (Phase 2)
ParticlePass           (sort_key=550)  — Billboard quads, GPU-simulated particles (Phase 3)
UIPass                 (sort_key=800)  — 2D overlay, delegates to UI module (Phase 2)
GizmoPass              (sort_key=900, editor-only) — Transform gizmos, outline, wireframe overlay (Phase 1)
TAAPass                (sort_key=950)  — Temporal AA resolve: blend jittered frame with reprojected history (Phase 3)
ReadbackPass           (sort_key=1000) — GPU→CPU JPEG copy (editor mode only, #ifdef UZ_EDITOR)
```

**Note**: Phase order in parentheses indicates which phase introduces each pass. Phase 2 establishes the full deferred pipeline; Phase 3 adds temporal effects (TAA, SSR) and particles.

**Halton Jitter**: TAA applies sub-pixel jitter to projection matrix per frame (8-sample Halton(2,3) sequence), requiring velocity vectors in G-Buffer RT3.

### `gpu_resources/` — RAII GPU Objects

| File | Purpose |
|------|---------|
| `gpu_buffer.h/cpp` | `GpuBuffer` — `ID3D12Resource` + heap type + state tracking |
| `gpu_texture.h/cpp` | `GpuTexture` — 2D/cube, DXGI format, RTV/SRV/DSV descriptor allocation |
| `upload_ring.h/cpp` | Per-frame upload ring buffer (mapped, CPU-write) for dynamic data (bones, cbuffers) |
| `readback_buffer.h/cpp` | Double-buffered readback: ping-pong with fence, maps on CPU after `WaitForGpu()` |

### `shader_compiler/` — DXC Wrapper
Can be instantiated without a D3D12 device. Used by both Runtime and Build (headless).

| File | Purpose |
|------|---------|
| `shader_compiler.h/cpp` | `LoadLibraryW(dxcompiler.dll)`, compile HLSL → DXIL, return diagnostics in VSCode format |
| `pso_cache.h/cpp` | `ID3D12PipelineState` cache keyed by `{shader_path, vertex_layout, blend_state, depth_state}` |
| `shader_hot_swap.h/cpp` | Queues a reload; applies on next frame after `WaitForGpu()` — never mid-frame |

**Diagnostic format** (maps directly to VSCode problem matcher):
```
shaders/pbr.hlsl(42,15): error X3013: 'normalize': no matching overload
```

### `material/` — Material System

| File | Purpose |
|------|---------|
| `material.h/cpp` | `Material`: shader path + typed parameter block (scalar, vector, texture slots) |
| `material_loader.h/cpp` | `.uzmat` → `Material`, uploads cbuffer to GPU |
| `parameter_block.h` | Typed cbuffer layout — maps `.uzmat` properties to HLSL register slots |

### `mesh_buffer/` — GPU Mesh Data

| File | Purpose |
|------|---------|
| `mesh_buffer.h/cpp` | GPU-resident VB + IB for a single mesh LOD. Loaded from `.uzmesh` |
| `skinned_mesh_buffer.h/cpp` | Adds bone palette `StructuredBuffer<float4x4>` (Phase 3) |

### `instancing/` — GPU Instancing (Phase 2)
Groups entities with identical mesh+material into instanced draw calls. Reduces draw call count significantly for scenes with repeated objects.

| File | Purpose |
|------|---------|
| `batch_builder.h/cpp` | Per-frame batching: collects `CMeshRenderer` entities, groups by `{mesh, material, variant_key}`, outputs `InstancedDrawCall` array |
| `instanced_draw.h` | `InstancedDrawCall` struct: mesh handle, material handle, instance count, transform `StructuredBuffer` |

**How it works**: After frustum culling, `BatchBuilder::build()` groups visible opaque entities into batches. Each batch uploads `Array<mat4>` world transforms to a `StructuredBuffer<float4x4>` via the upload ring buffer. The vertex shader reads transforms via `SV_InstanceID`. Single-instance batches fall back to the standard per-object cbuffer path. Transparent objects are excluded (they require per-object depth sorting). See [gpu-instancing.md](../../specs/gpu-instancing.md).

### `shadow_system/` — Shadow Atlas Management (Phase 2)
Manages shadow map atlases for all light types (directional CSM, point, spot).

| File | Purpose |
|------|---------|
| `shadow_system.h/cpp` | CSM cascade split computation, light-space frustum, texel stabilization (existing) |
| `point_shadow_system.h/cpp` | Point light dual-paraboloid shadow atlas (2048x2048, 8 lights max), hemisphere rendering |
| `spot_shadow_system.h/cpp` | Spot light perspective shadow atlas (2048x2048, 16 lights max), per-light view-proj |
| `shadow_atlas.h/cpp` | Generic atlas tile allocator: assign/release tiles for shadow-casting lights |

See [point-spot-light-shadows.md](../../specs/point-spot-light-shadows.md).

### `post/` — Post-Processing Effects (Phase 2+)
Screen-space effects that operate on render targets after lighting. Includes SSAO, SSR, TAA, bloom, and tonemapping.

| File | Purpose |
|------|---------|
| `ssao.h/cpp` | HBAO+ ambient occlusion: read GBuffer half-res, output occlusion term, blend into lighting pass |
| `ssr.h/cpp` | Screen-space reflections: trace rays in screen space against Hi-Z depth pyramid (Phase 3), accumulate reflections |
| `hi_z_pyramid.h/cpp` | Compute pyramid of half-resolution depth mips for SSR ray marching (Phase 3) |
| `taa.h/cpp` | Temporal anti-aliasing: blend current jittered frame with reprojected history using variance clamping, Catmull-Rom sharpening (Phase 3) |
| `bloom.h/cpp` | Dual-KawaseBlur: downsample, blur, upsample and blend with bright pixels (Phase 2) |
| `tonemapping.h/cpp` | ACES tonemapping, exposure adjustment, post-process color grading (Phase 2) |

**Execution order**: SSAO writes to intermediate, SSR reads SSAO + GBuffer, TAA blends current frame + history and outputs final, bloom and tonemapping applied last.

### `particles/` — GPU Particle System (Phase 3)
Full GPU-driven particle simulation: compute-shader simulation (no CPU readback), billboard rendering via `DrawIndexedInstanced`.

| File | Purpose |
|------|---------|
| `particle_emitter.h/cpp` | `CParticleEmitter` ECS component, `.uzfx` asset parsing and lifecycle |
| `particle_simulation.h/cpp` | Compute shader execution: `simulate.hlsl`, `emit.hlsl`. Maintains per-emitter `StructuredBuffer<Particle>` and indirect draw buffer |
| `particle_renderer.h/cpp` | Billboard quad expansion in vertex shader, reads simulated particle data, outputs to framebuffer (rendered by ParticlePass) |

Particle data persists in GPU VRAM between frames. Simulation runs each frame as a compute job before rendering. See [particle-system.md](../../specs/particle-system.md).

### `streaming/` — Editor Preview (UZ_EDITOR only)
Compiled out when `UZ_SHIPPING=1`.

| File | Purpose |
|------|---------|
| `jpeg_encoder.h/cpp` | libjpeg-turbo TurboJPEG: RGBA readback → JPEG blob. Runs on Encode thread |
| `ws_server.h/cpp` | IXWebSocket server on port 9002. Broadcasts JPEG blobs |

### `passes/` — Render Pass Implementations
Each pass is a self-contained `.h/cpp` that registers itself in the `RenderGraph`.

| File | Phase | Purpose |
|------|-------|---------|
| `shadow_pass.h/cpp` | 2 | Cascaded Shadow Maps (3 cascades, PCF 3×3, directional light shadow atlas) |
| `point_shadow_pass.h/cpp` | 2 | Point light dual-paraboloid shadow maps (2 faces per light, shadow atlas 2048x2048) |
| `spot_shadow_pass.h/cpp` | 2 | Spot light perspective shadow maps (1 face per light, shadow atlas 2048x2048) |
| `gbuffer_pass.h/cpp` | 2 | Deferred G-Buffer: RT0 Albedo+AO, RT1 Normal+Rough+Metal, RT2 Emissive, RT3 Velocity (RG16F for TAA). Writes to depth buffer |
| `deferred_lighting_pass.h/cpp` | 2 | Full-screen PBR: GGX BRDF, IBL specular/diffuse, shadow sampling from cascades |
| `ssao_pass.h/cpp` | 2 | HBAO+ ambient occlusion, half-res, reads GBuffer, outputs occlusion term |
| `ssr_pass.h/cpp` | 3 | Screen-space reflections: Hi-Z pyramid trace against velocity buffer for temporal stability |
| `skybox_pass.h/cpp` | 2 | Cubemap skybox, depth=EQUAL to render behind opaque geometry |
| `transparency_pass.h/cpp` | 2 | Forward rendering: sorted back-to-front alpha-blend for transparent objects |
| `particle_pass.h/cpp` | 3 | GPU particle rendering: billboard quads, reads simulated particle buffers |
| `ui_pass.h/cpp` | 2 | 2D overlay, delegates to UI module, rendered in screen-space |
| `gizmo_pass.h/cpp` | 1, editor-only | Transform gizmo handles, outline rendering, wireframe debug overlay (UZ_EDITOR only) |
| `taa_pass.h/cpp` | 3 | Temporal anti-aliasing resolve: blend jittered current frame with reprojected history, variance clamping, Catmull-Rom sharpening |
| `bloom_pass.h/cpp` | 2 | Dual-KawaseBlur: identify bright pixels, downsample/blur, upsample and composite |
| `tonemapping_pass.h/cpp` | 2 | ACES filmic tonemapping, exposure adjustment, optional color grading |
| `fps_overlay_pass.h/cpp` | 0 ✅ | Bitmap font FPS counter, debug text (existing, compiles on all phases) |

### `shaders/` — HLSL Source Code

All effect and compute shaders live in `src/renderer/shaders/`. Compiled offline by `UzBuild.exe` (Phase 4) and embedded in `.uzpak` assets, or compiled on-demand in editor mode.

| Directory | Purpose |
|-----------|---------|
| `include/` | Shared HLSL header files: `GlobalParams.hlsli` (cbuffer at b15), `pbr.hlsli` (GGX BRDF), `sampling.hlsli` (distributions), `noise.hlsli` |
| `vertex/` | VS entry points for deferred, forward, skinned, instanced rendering |
| `pixel/` | PS entry points for PBR, skybox, SSAO, SSR |
| `compute/` | Compute shaders: `simulate_particles.hlsl`, `emit_particles.hlsl`, `hi_z_pyramid.hlsl`, `taa_resolve.hlsl` |

**GlobalParams cbuffer** (constant buffer at `b15`): view matrix, projection matrix, view-projection, inverse VP, camera position, time, frame count, Halton jitter offset (TAA), previous frame V/P (motion vectors).

### `shader_variants/` — Variant System (Phase 2)

Shader variants allow conditional compilation: `SKINNED`, `INSTANCED`, `PARALLAX`, `EMISSIVE`, etc.

| File | Purpose |
|------|---------|
| `variant_flags.h` | Enum of variant flags and their HLSL #define equivalents |
| `variant_compiler.h/cpp` | Pre-compiles all variant combinations during build (offline) or on-demand (editor) |
| `pso_builder.h/cpp` | Maps material + variant flags → PSO cache key lookup |

---

## Data Contract with Engine

### G-Buffer Layout (Phase 2, extended Phase 3)

The G-Buffer consists of 4 render targets (all written by `gbuffer_pass`, read by `deferred_lighting_pass` and post-processing passes):

| RT | Format | Contents | Purpose |
|----|--------|----------|---------|
| RT0 | R8G8B8A8_UNORM | Albedo RGB + Ambient Occlusion (A) | Base color sampling in PBR |
| RT1 | R16G16B8A8_SNORM | Normal (RG), Roughness (B), Metallic (A) | Surface normals and material parameters |
| RT2 | R8G8B8A8_UNORM | Emissive RGB + pad (A) | Self-emissive surfaces, light maps |
| RT3 | R16G16_FLOAT | Velocity XY (motion vectors for TAA) | Per-pixel screen-space motion, Phase 3 |
| DS  | D32_FLOAT | Depth | Depth buffer for occlusion, SSAO, SSR Hi-Z |

**Note**: RT3 added in Phase 3 for TAA reprojection. Phase 2 uses only RT0–RT2.

### ECS Data Contract

The Renderer **reads** the following ECS components. It never writes any component.

The Renderer **reads** the following ECS components. It never writes any component.

| Component | Read fields | Used in pass |
|-----------|-------------|--------------|
| `CTransform` | `world` (mat4) | All passes |
| `CMeshRenderer` | `mesh`, `material`, `cast_shadow` | GBuffer, Shadow, Transparency |
| `CSkinnedMesh` | `mesh`, `material`, `bone_palette_slot` | GBuffer (Phase 3) |
| `CDirLight` | `color`, `intensity`, `shadow_enabled` | Shadow, Lighting |
| `CPointLight` | `color`, `intensity`, `radius`, `cast_shadow` | Lighting, PointLightShadow (Phase 2) |
| `CSpotLight` | `color`, `intensity`, `range`, `inner_angle`, `outer_angle`, `cast_shadow` | Lighting, SpotLightShadow (Phase 2) |
| `CCamera` | `fov`, `near_plane`, `far_plane`, `is_primary` | All passes (view/proj matrix) |

**Synchronization**: Renderer only reads ECS after `game_done.acquire()` (Phase 3 semaphore). Before that point, Game Logic thread may still be writing transforms.

---

## Key Invariants

1. **All D3D12 calls on Main thread only.** No other thread may call command list methods.
2. **PSO swap only between frames.** `WaitForGpu()` → build new PSO → swap → new frame starts.
3. **Renderer never modifies ECS.** It only reads component data.
4. **Readback and streaming compiled out in shipping.** `#ifdef UZ_EDITOR` guards all streaming code.
5. **`ShaderCompiler` is the only Renderer subsystem usable without a window.** Build tool instantiates only this.
6. **TAA requires velocity G-Buffer (RT3).** Phase 2 deferred renderer may omit RT3; Phase 3 TAA requires it for reprojection.
7. **SSAO runs at half-resolution.** Reads G-Buffer at half-res for performance, then upsamples with edge-aware filtering.
8. **Instancing excludes transparent + skinned meshes.** Only static or non-animated CMeshRenderer entities are batched; CSkinnedMesh and transparent materials use per-object rendering.

---

## Related Docs

- [lifecycle-and-concurrency.md](../../engine/lifecycle-and-concurrency.md) — render loop sequence, semaphore sync with game loop
- [../engine/README.md](../engine/README.md) — ECS components that Renderer reads
- [../runtime/README.md](../runtime/README.md) — who owns the D3D12 device lifetime
- [../ui/README.md](../ui/README.md) — UIPass is the last pass before readback
- [../../specs/pbr-renderer.md](../../specs/pbr-renderer.md) — PBR deferred pipeline, GGX BRDF, IBL
- [../../specs/point-spot-light-shadows.md](../../specs/point-spot-light-shadows.md) — dual-paraboloid and perspective shadow atlases
- [../../specs/gpu-instancing.md](../../specs/gpu-instancing.md) — BatchBuilder, InstancedDrawCall pattern
- [../../specs/screen-space-effects.md](../../specs/screen-space-effects.md) — SSAO, SSR, TAA, Hi-Z pyramid
- [../../specs/particle-system.md](../../specs/particle-system.md) — GPU particle simulation, compute shaders
- [../../specs/shader-variants.md](../../specs/shader-variants.md) — variant compilation, PSO caching

## Rulebook Tasks

Phase 2 rendering: [phase2-pbr-renderer](../../.rulebook/tasks/phase2-pbr-renderer/), [phase2-shadow-maps](../../.rulebook/tasks/phase2-shadow-maps/), [phase2-point-spot-shadows](../../.rulebook/tasks/phase2-point-spot-shadows/), [phase2-ssao](../../.rulebook/tasks/phase2-ssao/), [phase2-shader-library](../../.rulebook/tasks/phase2-shader-library/), [phase2-shader-variants](../../.rulebook/tasks/phase2-shader-variants/), [phase2-gpu-instancing](../../.rulebook/tasks/phase2-gpu-instancing/)

Phase 3 advanced effects: [phase3-taa](../../.rulebook/tasks/phase3-taa/), [phase3-ssr](../../.rulebook/tasks/phase3-ssr/), [phase3-particle-system](../../.rulebook/tasks/phase3-particle-system/), [phase3-skeletal-mesh](../../.rulebook/tasks/phase3-skeletal-mesh/)

Editor gizmos: [phase1-editor-gizmos](../../.rulebook/tasks/phase1-editor-gizmos/)
