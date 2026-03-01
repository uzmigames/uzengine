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
ShadowPass             (sort_key=100)  — CSM 3 cascades (Phase 2)
PointLightShadowPass   (sort_key=110)  — dual-paraboloid point light shadows (Phase 2)
SpotLightShadowPass    (sort_key=120)  — perspective spot light shadows (Phase 2)
DepthPrepass           (sort_key=200)  — early-Z, writes depth buffer
GBufferPass       (sort_key=300)  — albedo/normal/roughness/metallic MRTs (Phase 2)
LightingPass      (sort_key=400)  — deferred PBR, IBL (Phase 2)
TransparencyPass  (sort_key=500)  — sorted back-to-front alpha blend
SkyboxPass        (sort_key=600)  — cubemap, depth=EQUAL
PostProcessPass   (sort_key=700)  — SSAO, bloom, tonemapping ACES (Phase 2)
UIPass            (sort_key=800)  — 2D overlay (delegates to UI module)
OverlayPass       (sort_key=900)  — FPS counter, debug overlays
ReadbackPass      (sort_key=1000) — GPU→CPU copy (editor mode only, #ifdef UZ_EDITOR)
```

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
| `shadow_pass.h/cpp` | 2 | Cascaded Shadow Maps (3 cascades, PCF 3×3, shadow atlas) |
| `point_shadow_pass.h/cpp` | 2 | Point light dual-paraboloid shadow maps (2 faces per light, shadow atlas 2048x2048) |
| `spot_shadow_pass.h/cpp` | 2 | Spot light perspective shadow maps (1 face per light, shadow atlas 2048x2048) |
| `depth_prepass.h/cpp` | 1 | Early-Z, writes `DSV` |
| `gbuffer_pass.h/cpp` | 2 | Deferred: albedo (R8G8B8A8), normal (R16G16_SNORM), material (R8G8B8A8) |
| `lighting_pass.h/cpp` | 2 | Full-screen deferred PBR: GGX BRDF + IBL |
| `transparency_pass.h/cpp` | 2 | Sorted forward alpha-blend |
| `skybox_pass.h/cpp` | 2 | Cubemap skybox, depth=EQUAL trick |
| `postprocess_pass.h/cpp` | 2 | SSAO (HBAO+), bloom (dual-KawaseBlur), ACES tonemapping |
| `fps_overlay_pass.h/cpp` | 0 ✅ | Bitmap font FPS counter (CPU-composited, existing) |

---

## Data Contract with Engine

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

---

## Related Docs

- [lifecycle-and-concurrency.md](../../engine/lifecycle-and-concurrency.md) — render loop sequence, semaphore sync with game loop
- [../engine/README.md](../engine/README.md) — ECS components that Renderer reads
- [../runtime/README.md](../runtime/README.md) — who owns the D3D12 device lifetime
- [../ui/README.md](../ui/README.md) — UIPass is the last pass before readback
