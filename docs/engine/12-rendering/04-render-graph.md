# Render Graph — Overview

> **Status**: Planned (Milestone 2.0)
> **Source**: Spec in `docs/ENGINE.md` §32
> **Depends on**: RHI (§03), Render World (§12)
> **References**: UE5 `FRDGBuilder`, Godot `RenderingDevice`, FrameGraph (Yixin Zhu, 2017)

## Purpose

Writing correct render passes is error-prone. Manual resource barriers, lifetime management, and descriptor binding require careful sequencing. The Render Graph is a **directed acyclic graph (DAG)** of render passes that automatically:
- Inserts resource state barriers
- Aliases transient resources for memory reuse
- Culls unused passes
- Manages frame pooling of transient resources

Instead of manually managing GPU resources, you declare what each pass reads and writes. The graph handles the rest.

```
Input: Declare passes + resources
       ┌──────────┐
       │  GBuffer │  Reads: FrameDepth (None)
       │  Pass    │  Writes: BaseColor, Normal, Emissive
       └────┬─────┘
            │
       ┌────▼──────┐
       │ Lighting  │  Reads: GBuffer outputs
       │ Pass      │  Writes: SceneColor
       └────┬──────┘
            │
       ┌────▼──────┐
       │  Bloom    │  Reads: SceneColor
       │  Pass     │  Writes: BloomTarget
       └────┬──────┘
            │
       ┌────▼────────────┐
       │ UI Composite    │  Reads: SceneColor, UIRenderTarget
       │ Pass            │  Writes: BackBuffer
       └─────────────────┘

Output: Optimized command list with barriers inserted
        Resource aliasing plan (transient reuse)
        Culled passes removed
```

## Core Concepts

### RgPass — Render Graph Pass

```cpp
/// Describes a render pass: what it reads, writes, and how to execute.
class RgPass {
public:
    /// Declare texture resource this pass reads.
    void Read(RgTexture* tex, RgResourceState state);

    /// Declare buffer resource this pass reads.
    void Read(RgBuffer* buf, RgResourceState state);

    /// Declare texture resource this pass writes.
    void Write(RgTexture* tex, RgResourceState state);

    /// Declare buffer resource this pass writes.
    void Write(RgBuffer* buf, RgResourceState state);

    /// Declare the pass execution callback (lambda).
    /// Called during graph execution with command list + resource bindings.
    void SetExecution(std::function<void(RgContext&)> fn);

    /// Pass name (for debugging / command list markers).
    void SetName(std::string_view name);
};
```

**Example: GBuffer generation pass**

```cpp
// In renderer initialization
void RenderGraph::RegisterGBufferPass() {
    auto* pass = graph.CreatePass("GBuffer");

    // Declare what this pass reads
    pass->Read(scene_depth.get(), RgResourceState::DepthRead);
    pass->Read(mesh_buffer.get(), RgResourceState::StorageBufferRead);

    // Declare what this pass writes (outputs)
    auto* base_color_out = pass->Write(
        graph.CreateTexture({
            .width = 1920, .height = 1080,
            .format = RHIFormat::R8G8B8A8_UNORM,
            .name = "GBuffer.BaseColor"
        }),
        RgResourceState::RenderTarget
    );

    auto* normal_out = pass->Write(
        graph.CreateTexture({
            .width = 1920, .height = 1080,
            .format = RHIFormat::R10G10B10A2_UNORM,
            .name = "GBuffer.Normal"
        }),
        RgResourceState::RenderTarget
    );

    // Execution callback: records actual GPU commands
    pass->SetExecution([this](RgContext& ctx) {
        ctx.SetRenderTargets({base_color_out, normal_out});
        ctx.SetViewport(0, 0, 1920, 1080);
        ctx.ClearRenderTargets(Vec4(0, 0, 0, 1));

        // Bind mesh data
        ctx.SetConstantBuffer(0, scene_constants);
        ctx.SetShaderResourceView(0, mesh_buffer);

        // Draw all opaque geometry
        for (auto& draw_call : opaque_draws) {
            ctx.SetGraphicsPipeline(draw_call.pso);
            ctx.DrawIndexed(draw_call.index_count, draw_call.index_offset);
        }
    });
}
```

### RgTexture / RgBuffer — Virtual Resources

```cpp
/// Virtual texture reference (not a real RHI resource).
/// Lifetime is the scope of a single render graph.
/// The graph may alias this with other textures for memory efficiency.
class RgTexture {
public:
    /// Physical dimensions and format.
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    RHIFormat GetFormat() const;

    /// Actual RHI handle (valid only during pass execution).
    RHITextureHandle GetRHIHandle() const;
};

/// Virtual buffer reference.
class RgBuffer {
public:
    uint32_t GetByteSize() const;
    RHIFormat GetFormat() const;
    RHIBufferHandle GetRHIHandle() const;
};
```

**Why virtual references?** The same memory might back multiple "RgTexture" objects during different frame phases. The graph compiles all usage and aliases non-overlapping resources to the same GPU memory, reducing allocation pressure.

### Resource State Tracking

```cpp
enum class RgResourceState : uint8_t {
    // Read states (shared)
    ShaderResourceRead,
    StorageBufferRead,
    DepthRead,
    IndirectArgument,

    // Write states (exclusive)
    RenderTarget,
    DepthTarget,
    StorageBufferWrite,
    CopyDest,

    // Special
    Present,     // Backbuffer final state
    Undefined,   // Resource is unused (will be culled or aliased)
};
```

The graph automatically inserts `ResourceBarrier()` calls when a resource transitions states between passes.

## Automatic Barrier Insertion

```
Pass A writes to SceneColor (RenderTarget state)
    ↓
Transition barrier: RenderTarget → ShaderResourceRead
    ↓
Pass B reads from SceneColor (ShaderResourceRead state)
```

This is automatic. You don't write barriers — you declare access, the graph inserts them.

## Transient Resource Aliasing

Textures that don't overlap in time can share GPU memory:

```
Pass A → SceneColor (used frames 0-100)
Pass B → NormalMap  (used frames 200-300)

During compilation:
  SceneColor's lifetime ends before NormalMap begins
  → Allocate same GPU memory, alias under different handles
  → Save VRAM, improve cache locality
```

```cpp
// In RgBuilder
struct AliasingPlan {
    std::vector<std::pair<RgResource, RgResource>> aliases;
    // maps virtual resources to actual GPU memory
};

AliasingPlan plan = builder.GetAliasingPlan();
for (auto [virtual_res, physical_mem] : plan.aliases) {
    gpu_memory_pool.RegisterAlias(virtual_res, physical_mem);
}
```

## Pass Culling

Unused passes are automatically removed:

```cpp
// Declare a Bloom pass
auto* bloom_pass = graph.CreatePass("Bloom");
bloom_pass->Write(bloom_texture);
// ... setup execution

// But if nothing reads bloom_texture (e.g., user disabled bloom),
// the pass is culled from the final execution list
```

This saves GPU time on low-end hardware or when effects are disabled.

## Render Graph Context (RgContext)

During pass execution, you receive an `RgContext` with resource bindings:

```cpp
class RgContext {
public:
    /// Get physical RHI handle for a virtual resource.
    RHITextureHandle GetTexture(RgTexture* res);
    RHIBufferHandle  GetBuffer(RgBuffer* res);

    /// Render target / depth binding
    void SetRenderTargets(std::span<RgTexture*> targets);
    void SetDepthTarget(RgTexture* depth);

    /// Pipeline and input binding
    void SetGraphicsPipeline(RHIPipelineHandle pso);
    void SetConstantBuffer(uint32_t slot, RHIBufferHandle buf);
    void SetShaderResourceView(uint32_t slot, RHITextureHandle tex);

    /// Draw commands
    void Draw(uint32_t vertex_count, uint32_t vertex_start = 0);
    void DrawIndexed(uint32_t index_count, uint32_t index_start = 0);
    void DrawInstanced(uint32_t vertex_count, uint32_t instance_count);

    /// Compute dispatch
    void Dispatch(uint32_t x, uint32_t y, uint32_t z);

    /// Resource copy
    void CopyTexture(RgTexture* src, RgTexture* dst);
};
```

## Built-In Passes

The graph provides common rendering passes out-of-the-box:

| Pass | Input | Output | Purpose |
|------|-------|--------|---------|
| **GBuffer** | Scene, FrameDepth | BaseColor, Normal, Emissive | Deferred shading geometry |
| **Lighting** | GBuffer, Depth | SceneColor | Light accumulation (deferred) |
| **Shadow** | Scene, LightProj | ShadowMap | Directional/spot/point shadows |
| **SSAO** | Normal, Depth | AO | Screen-space ambient occlusion |
| **SSR** | SceneColor, Normal, Depth | Reflections | Screen-space reflections |
| **TAA** | SceneColor, Velocity | AASceneColor | Temporal anti-aliasing |
| **Bloom** | SceneColor | BloomResult | Bright object bloom |
| **Tonemap** | SceneColor, BloomResult | ToneMappedColor | HDR→SDR conversion |
| **UI Composite** | ToneMappedColor, UIRenderTarget | BackBuffer | Blend UI on top |

**Example: Enable specific passes**

```cpp
graph.CreatePass<BuiltInPass::TAA>({
    .enabled = post_process_settings.taa_enabled,
    .color_input = scene_color,
    .velocity_input = velocity_buffer,
    .output = &final_scene_color,
});
```

## Async Compute Integration

Some passes (post-processing, occlusion queries) can overlap with graphics:

```cpp
// Graphics timeline
Pass GBuffer        ▓▓▓▓
Pass Lighting           ▓▓▓
Pass Bloom              ▓▓▓
Pass TAA                    ▓▓▓

// Compute timeline (overlaps)
Pass SSAOCompute   ▒▒▒▒▒
Pass OcclusionCull        ▒▒▒▒▒

GPU utilization: ▓▓▓▓▓▓▓▓▓ + ▒▒▒▒▒▒▒▒▒▒ (better than sequential)
```

The graph automatically detects compute passes that can run async and schedules them on separate command queues.

```cpp
auto* ssao_pass = graph.CreatePass("SSAO");
ssao_pass->SetQueueType(RgQueueType::Compute);  // Can overlap with graphics
ssao_pass->Read(normal_buffer);
ssao_pass->Read(depth_buffer);
ssao_pass->Write(ao_buffer);
```

## Frame Resource Pooling

To avoid allocating new descriptors every frame:

```cpp
// Renderer initialization
struct FrameResources {
    RHIDescriptorHeap* cbv_srv_uav_heap;  // Per-frame descriptor heap
    RHIBuffer* upload_buffer;              // Per-frame upload buffer
    std::vector<RgTexture*> transient;     // Per-frame transient textures
};

std::array<FrameResources, 3> frame_resources;  // Triple-buffer

// Each frame reuses the same pooled resources
void Renderer::Render() {
    auto& frame = frame_resources[current_frame % 3];
    frame.cbv_srv_uav_heap->Reset();  // Clear old descriptors
    frame.upload_buffer->Reset();      // Reset upload pointer

    graph.SetFrameResources(frame);
    graph.Compile();
    graph.Execute();
}
```

## Architecture

```
RgBuilder (Public API)
  ├─► CreatePass(name) → RgPass*
  ├─► CreateTexture(desc) → RgTexture*
  ├─► CreateBuffer(desc) → RgBuffer*
  └─► Compile() → RgGraph*

RgGraph (Internal)
  ├─► Dependency Analysis (DAG topological sort)
  ├─► Aliasing Plan (lifetime analysis → reuse map)
  ├─► Pass Culling (unused nodes removal)
  ├─► Barrier Insertion (state transitions)
  └─► Execute() → Submits command list to RHI
```

## Engine Comparison

| Feature | Unreal 5 | Godot | **UzEngine** |
|---------|----------|-------|--------------|
| Graph model | FRDGBuilder (DAG) | Manual RenderingDevice | **RgBuilder + RgGraph** |
| Pass declaration | Lambdas with reads/writes | Explicit state management | **Read()/Write() + SetExecution()** |
| Resource aliasing | Automatic transient pooling | N/A | **Automatic alias planning** |
| Barrier insertion | Automatic from graph | Manual barriers | **Automatic from resource state** |
| Async compute | Supported via graph hints | Limited | **Graph-aware queue scheduling** |
| Built-in passes | Many (10+) | None | **8 core passes** |

## Rules

1. **NEVER access RHI directly from pass callbacks** — only use `RgContext` methods
2. **NEVER manually insert barriers** — declare resource states, let the graph insert barriers
3. **ALWAYS declare all reads/writes before SetExecution** — graph analysis requires complete dependency info
4. **ALWAYS create transient resources via `graph.CreateTexture()`** — never with `device->CreateTexture()`
5. **NEVER read a written resource in the same pass** — use separate input/output resources
6. **ALWAYS name passes and resources** — critical for profiling and debugging GPU captures
