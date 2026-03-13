# Render Graph — Automatic DAG-based GPU Resource Management

> **Status**: Done (Phase 3a-3.1.0)
> **Source**: Implementation in `runtime/engine/renderer/render_graph/`
> **Depends on**: RHI (§03), Render World (§12.1)
> **References**: UE5 `FRDGBuilder`, FrameGraph (Yixin Zhu 2017), Godot `RenderingDevice`

## Purpose

Manual render pass management is error-prone. Without a graph abstraction, developers must:
- Manually track resource lifetimes and state transitions
- Insert barriers at the right moments (too early = stalls, too late = corruption)
- Manage transient texture allocation and reuse
- Detect and remove unused passes to save GPU time
- Coordinate dependency chains correctly

The **render graph** solves these problems by accepting a declarative description of pass dependencies, then automatically:
- **Topologically sorts passes** to satisfy read/write dependencies
- **Inserts resource state barriers** at optimal points
- **Aliases transient resources** (reuse same GPU memory for non-overlapping lifetimes)
- **Culls unused passes** (outputs nobody consumes)
- **Manages frame pooling** (reuse physical allocations across frames)

```
┌──────────────────────────────────────────────────────────────┐
│ Frame N: Declare passes, resources, and dependencies         │
├──────────────────────────────────────────────────────────────┤
│ RgBuilder::CreateTexture("GBuffer", {1920x1080, RGBA8...})   │
│ RgBuilder::AddPass("GBuffer Pass", [](RgPassBuilder& pb) {   │
│     pb.Write(gbuffer, RenderTarget);                         │
│     pb.SetExecutionCallback([](IRHICommandList*, res) {...}); │
│ });                                                           │
└──────────────────────────────────────────────────────────────┘
                             ↓
┌──────────────────────────────────────────────────────────────┐
│ Compile(): Optimize the graph                                │
│ 1. Topological sort (Kahn's algorithm)                       │
│ 2. Compute resource lifetimes [first_pass, last_pass]        │
│ 3. Cull unreachable passes                                   │
│ 4. Schedule barriers based on state transitions              │
└──────────────────────────────────────────────────────────────┘
                             ↓
┌──────────────────────────────────────────────────────────────┐
│ Execute(): Run the optimized graph                           │
│ 1. Acquire physical resources from pool                      │
│ 2. Emit scheduled barriers                                   │
│ 3. Invoke pass callbacks (in sorted order)                   │
│ 4. Release transient resources back to pool                  │
└──────────────────────────────────────────────────────────────┘
```

## Core Components

### RgResourceId — Virtual Resource Handle

```cpp
/// Identifies a virtual texture or buffer in the render graph.
///
/// This is NOT a physical RHI handle. Instead, it is a graph-local index
/// that the compiler maps to a physical RHITextureHandle or RHIBufferHandle
/// at execution time. Virtual IDs allow the graph to alias and reuse GPU memory.
struct RgResourceId {
    uint32_t index = UINT32_MAX;  // Index into builder's resource array; UINT32_MAX = invalid

    bool IsValid() const { return index != UINT32_MAX; }
    static RgResourceId Invalid() { return {}; }
};
```

**Why virtual handles?**
- Decouples graph description from GPU memory allocation
- Enables transient resource aliasing (same GPU memory shared by non-overlapping resources)
- Allows the compiler to optimize without affecting the API contract

### RgTextureDesc / RgBufferDesc — Virtual Resource Descriptors

```cpp
/// Describes dimensions, format, and usage of a virtual texture.
struct RgTextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    uint16_t depthOrArraySize = 1;  // Depth for 3D, array size for arrays
    uint16_t mipLevels = 1;
    RHIFormat format = RHIFormat::RGBA8Unorm;
    RHITextureUsage usage = RHITextureUsage::ShaderResource;
};

/// Describes size, stride, and usage of a virtual buffer.
struct RgBufferDesc {
    uint64_t sizeBytes = 0;
    uint32_t stride = 0;            // For structured buffers
    RHIBufferUsage usage = RHIBufferUsage::None;
};
```

These map to physical RHI resources at execution time via the resource pool.

### RgBuilder — Frame-Level Graph Construction

The primary public API for building a render graph. **Create a new `RgBuilder` each frame.**

```cpp
/// Constructs a render graph for this frame.
/// Arguments:
///   device — RHI device used for resource queries (must outlive builder)
///   pool — Persistent resource pool for transient allocation/reuse
RgBuilder rg(device, &pool);

// ──────────────────────────────────────────────────────────
// 1. DECLARE RESOURCES
// ──────────────────────────────────────────────────────────

// Create transient texture (graph-owned, freed after frame)
auto gBufferAlbedo = rg.CreateTexture("GBuffer.Albedo", {
    .width = 1920,
    .height = 1080,
    .format = RHIFormat::RGBA8Unorm,
    .usage = RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource
});

// Create transient buffer (graph-owned)
auto lightProbeData = rg.CreateBuffer("LightProbeData", {
    .sizeBytes = 1024 * 1024,
    .stride = sizeof(Vec4),
    .usage = RHIBufferUsage::StorageBuffer | RHIBufferUsage::UnorderedAccess
});

// Import external resource (caller owns lifetime, e.g., swapchain backbuffer)
auto backBuffer = rg.ImportTexture("BackBuffer", swapChainHandle,
                                   RHIResourceState::Present);

// ──────────────────────────────────────────────────────────
// 2. DECLARE PASSES AND DEPENDENCIES
// ──────────────────────────────────────────────────────────

rg.AddPass("GBuffer", [&](RgPassBuilder& pb) {
    pb.Write(gBufferAlbedo, RHIResourceState::RenderTarget);
    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        // Record GPU commands using res.GetTexture(gBufferAlbedo)
        cmd->SetRenderTargets({res.GetTexture(gBufferAlbedo)}, nullptr);
        cmd->ClearRenderTargets({0, 0, 0, 1});
        // ... draw calls ...
    });
});

rg.AddPass("Lighting", [&](RgPassBuilder& pb) {
    pb.Read(gBufferAlbedo, RHIResourceState::ShaderResource);
    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        // Reads GBuffer produced by previous pass
        // Compiler automatically inserts barrier: RenderTarget → ShaderResource
        auto albedoTex = res.GetTexture(gBufferAlbedo);
        cmd->SetShaderResourceView(0, albedoTex);
        // ... dispatch compute or draw ...
    });
});

// ──────────────────────────────────────────────────────────
// 3. COMPILE AND EXECUTE
// ──────────────────────────────────────────────────────────

rg.Compile();  // Topological sort, barrier scheduling, pass culling
rg.Execute(cmd);  // Run all non-culled passes in dependency order
```

### RgPassBuilder — Declaring Pass Dependencies

Passed to `AddPass()` callbacks to declare resource reads/writes and the execution callback.

```cpp
class RgPassBuilder {
public:
    /// Declare that this pass reads a resource.
    /// Compiler inserts a barrier if resource's current state differs from `state`.
    RgResourceId Read(RgResourceId id, RHIResourceState state);

    /// Declare that this pass writes a resource.
    /// Compiler inserts a barrier if needed, and tracks state for subsequent passes.
    RgResourceId Write(RgResourceId id, RHIResourceState state);

    /// Set the callback that executes during graph execution.
    /// Receives the command list and resolved physical handles.
    void SetExecutionCallback(RgPassExecuteFn fn);
};
```

**Resource state examples:**
- **Read states**: `ShaderResource`, `CopySrc`, `IndirectArgument`, `DepthRead`
- **Write states**: `RenderTarget`, `DepthStencilWrite`, `UnorderedAccess`, `CopyDst`

### RgPassResources — Resolved Physical Handles

Passed to execution callbacks to obtain physical RHI handles for declared resources.

```cpp
class RgPassResources {
public:
    /// Returns the physical RHI texture handle.
    /// Only valid for resources declared as Read() or Write() by this pass.
    RHITextureHandle GetTexture(RgResourceId id) const;

    /// Returns the physical RHI buffer handle.
    RHIBufferHandle GetBuffer(RgResourceId id) const;
};
```

**Key point**: Physical handles are resolved **only at execution time**, enabling resource pooling and aliasing.

### RgResourcePool — Cross-Frame Physical Resource Cache

Persistent (survives across frames). Manages allocation, reuse, and lifetime of physical GPU resources.

```cpp
class RgResourcePool {
public:
    /// Acquires a physical texture matching the descriptor.
    /// Returns cached texture if available, else creates new one.
    RHITextureHandle AcquireTexture(const RgTextureDesc& desc, const char* debugName);

    /// Acquires a physical buffer matching the descriptor.
    RHIBufferHandle AcquireBuffer(const RgBufferDesc& desc, const char* debugName);

    /// Returns texture to the pool for future reuse.
    void ReleaseTexture(RHITextureHandle handle, const RgTextureDesc& desc);

    /// Returns buffer to the pool for future reuse.
    void ReleaseBuffer(RHIBufferHandle handle, const RgBufferDesc& desc);

    /// Destroys all unused cached resources (call on memory pressure).
    void Trim();

    /// Query pool status.
    uint32_t GetCachedTextureCount() const;
    uint32_t GetCachedBufferCount() const;
};
```

**Resource pooling benefits:**
- **Reuse**: Same GPU memory backing multiple frames
- **Aliasing**: Non-overlapping virtual resources share same GPU memory
- **Efficiency**: Avoid allocation/deallocation stalls

## Compilation Phases

When `Compile()` is called, four optimizations run in sequence:

### 1. Topological Sort (Kahn's Algorithm)

Ensures dependencies are satisfied: if pass A writes resource R and pass B reads R, A executes before B.

```cpp
// Compiler builds adjacency from resource accesses:
Pass A writes GBuffer
Pass B reads GBuffer
→ Edge: A → B (B depends on A)
→ Sorted order: [A, B, ...]
```

**Handles all dependency types:**
- Write → Read (resource produced by A, consumed by B)
- Write → Write (A outputs to R, B overwrites R)
- Read → Write (A reads R, B modifies R)

### 2. Compute Resource Lifetimes

For each resource, compute `[firstPass, lastPass]` — the passes where it first appears and last appears in sorted order.

```cpp
GBuffer.Albedo:
  Created in GBuffer pass (index 0)
  Read in Lighting pass (index 1)
  Read in PostProcess pass (index 3)
  Lifetime: [0, 3]

TempBuffer:
  Created in ComputePass (index 2)
  Read in DebugPass (index 2)
  Lifetime: [2, 2]
```

**Used for**: Identifying which resources can be aliased (non-overlapping lifetimes).

### 3. Pass Culling

Removes passes whose outputs nobody consumes.

```cpp
auto bloom = rg.CreateTexture("Bloom", ...);
rg.AddPass("Bloom Pass", [](RgPassBuilder& pb) {
    pb.Write(bloom, RHIResourceState::UnorderedAccess);
    // ... but if nobody reads bloom:
});

// Compiler marks "Bloom Pass" as culled (no reader)
// Pass is skipped during execution
// GPU time saved (especially for optional effects)
```

**Rules:**
- Passes with no writes are never culled (assumed to have side effects)
- Resources written by culled passes are also marked culled
- Culling propagates backward (if culling B removes last reader of A, A may be culled)

### 4. Schedule Barriers

Walks sorted passes, tracking each resource's current state, and inserts barriers where states mismatch.

```cpp
GBuffer.Albedo transitions:
  Created as Undefined
  Pass GBuffer writes as RenderTarget → State = RenderTarget
  Barrier inserted before Lighting: RenderTarget → ShaderResource
  Pass Lighting reads as ShaderResource → State = ShaderResource
  Barrier inserted before PostProcess: ShaderResource → ShaderResource (no-op, skipped)
```

**Optimization**: No-op barriers (Undefined → any state, or same state) are skipped.

## Pass Authoring Guide

### Example 1: Simple Render Target Pass

```cpp
auto sceneColor = rg.CreateTexture("SceneColor", {
    .width = 1920, .height = 1080,
    .format = RHIFormat::RGBA16Float,
    .usage = RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource
});

rg.AddPass("Lighting", [&](RgPassBuilder& pb) {
    // Declare reads
    pb.Read(gBuffer, RHIResourceState::ShaderResource);
    pb.Read(shadowMap, RHIResourceState::ShaderResource);

    // Declare writes
    pb.Write(sceneColor, RHIResourceState::RenderTarget);

    // Set execution
    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        auto rtv = res.GetTexture(sceneColor);
        auto gbufferSrv = res.GetTexture(gBuffer);
        auto shadowSrv = res.GetTexture(shadowMap);

        // Compile knows: before this pass, insert barriers
        // - GBuffer: ShaderResourceRead (from previous write)
        // - ShadowMap: ShaderResourceRead

        cmd->SetRenderTargets({rtv}, nullptr);
        cmd->ClearRenderTargets({0, 0, 0, 1});
        cmd->SetShaderResourceView(0, gbufferSrv);
        cmd->SetShaderResourceView(1, shadowSrv);
        cmd->SetGraphicsPipeline(lightingPSO);
        cmd->DrawInstanced(4, numLights);  // Full-screen quad per light
    });
});
```

### Example 2: Compute Pass with Buffer Output

```cpp
auto hzbBuffer = rg.CreateBuffer("HZBMipChain", {
    .sizeBytes = 1024 * 1024,  // HZB buffer
    .stride = 4,  // uint32
    .usage = RHIBufferUsage::StorageBuffer | RHIBufferUsage::UnorderedAccess |
             RHIBufferUsage::CopyDst
});

rg.AddPass("BuildHZB", [&](RgPassBuilder& pb) {
    pb.Read(sceneDepth, RHIResourceState::ShaderResource);
    pb.Write(hzbBuffer, RHIResourceState::UnorderedAccess);

    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        auto depthSrv = res.GetTexture(sceneDepth);
        auto hzbUav = res.GetBuffer(hzbBuffer);

        cmd->SetComputePipeline(hzbBuildPSO);
        cmd->SetShaderResourceView(0, depthSrv);
        cmd->SetUnorderedAccessView(0, hzbUav);
        cmd->Dispatch(64, 64, 1);  // Build 64x64 pyramid
    });
});
```

### Example 3: Conditional Pass Execution (Culling)

```cpp
auto bloomTexture = rg.CreateTexture("Bloom", {
    .width = 960, .height = 540,
    .format = RHIFormat::RGBA16Float,
    .usage = RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource
});

if (settings.bloomEnabled) {
    rg.AddPass("Bloom", [&](RgPassBuilder& pb) {
        pb.Read(sceneColor, RHIResourceState::ShaderResource);
        pb.Write(bloomTexture, RHIResourceState::RenderTarget);
        pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
            // Extract bright pixels and blur
            cmd->SetRenderTargets({res.GetTexture(bloomTexture)}, nullptr);
            // ...
        });
    });
}

// If bloom is disabled, the pass is never added.
// If enabled but nobody reads bloomTexture → culled automatically.
```

### Example 4: Imported External Resource

```cpp
// Swapchain backbuffer (managed by platform/window system)
auto backBuffer = rg.ImportTexture("BackBuffer", swapChainHandle,
                                   RHIResourceState::Present);

// Final composite pass
rg.AddPass("UIComposite", [&](RgPassBuilder& pb) {
    pb.Read(sceneColor, RHIResourceState::ShaderResource);
    pb.Read(uiRenderTarget, RHIResourceState::ShaderResource);
    pb.Write(backBuffer, RHIResourceState::RenderTarget);

    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        auto rtv = res.GetTexture(backBuffer);

        // Compiler knows: before this pass, transition backbuffer
        // from Present to RenderTarget

        cmd->SetRenderTargets({rtv}, nullptr);
        cmd->SetShaderResourceView(0, res.GetTexture(sceneColor));
        cmd->SetShaderResourceView(1, res.GetTexture(uiRenderTarget));
        cmd->SetGraphicsPipeline(composeUIPSO);
        cmd->DrawInstanced(4, 1);  // Full-screen quad
    });
});
```

## API Reference

### RgBuilder Methods

| Method | Purpose | Returns |
|--------|---------|---------|
| `CreateTexture(name, desc)` | Create transient texture | `RgResourceId` |
| `CreateBuffer(name, desc)` | Create transient buffer | `RgResourceId` |
| `ImportTexture(name, handle, state)` | Register external texture | `RgResourceId` |
| `ImportBuffer(name, handle, state)` | Register external buffer | `RgResourceId` |
| `AddPass(name, setupFn)` | Register a pass | `void` |
| `Compile()` | Optimize graph (sort, cull, barriers) | `void` |
| `Execute(cmdList)` | Run all non-culled passes | `void` |
| `GetPassCount()` | Total passes (including culled) | `uint32_t` |
| `GetCulledPassCount()` | Count of culled passes | `uint32_t` |
| `GetBarrierCount()` | Count of scheduled barriers | `uint32_t` |
| `GetResourceCount()` | Total resources | `uint32_t` |
| `GetResource(id)` | Lookup resource entry | `const RgResourceEntry&` |

### RgPassBuilder Methods

| Method | Purpose | Returns |
|--------|---------|---------|
| `Read(id, state)` | Declare resource read | `RgResourceId` (same as input) |
| `Write(id, state)` | Declare resource write | `RgResourceId` (same as input) |
| `SetExecutionCallback(fn)` | Set GPU command callback | `void` |

### RgPassResources Methods

| Method | Purpose | Returns |
|--------|---------|---------|
| `GetTexture(id)` | Resolve virtual handle to physical texture | `RHITextureHandle` |
| `GetBuffer(id)` | Resolve virtual handle to physical buffer | `RHIBufferHandle` |

### RgResourcePool Methods

| Method | Purpose | Returns |
|--------|---------|---------|
| `AcquireTexture(desc, debugName)` | Get or create physical texture | `RHITextureHandle` |
| `AcquireBuffer(desc, debugName)` | Get or create physical buffer | `RHIBufferHandle` |
| `ReleaseTexture(handle, desc)` | Return texture to pool | `void` |
| `ReleaseBuffer(handle, desc)` | Return buffer to pool | `void` |
| `Trim()` | Destroy unused cached resources | `void` |
| `GetCachedTextureCount()` | Count cached (available) textures | `uint32_t` |
| `GetCachedBufferCount()` | Count cached (available) buffers | `uint32_t` |

## Thread Safety

> **⚠️ Not thread-safe.** All builder methods must be called from a single thread.

The renderer creates a new `RgBuilder` each frame on the render thread. The `RgResourcePool` is persistent but only accessed during `Execute()`, also on the render thread.

**Do not:**
- Call `RgBuilder` methods from multiple threads concurrently
- Share an `RgBuilder` across frames (create a new one each frame)
- Access `RgResourcePool` outside of `RgBuilder::Execute()` on the same thread

## Common Patterns

### Pattern 1: Double-Buffered Render Target

```cpp
// Ping-pong between two textures to read last frame's output
auto rtA = rg.CreateTexture("RenderTarget.A", desc);
auto rtB = rg.CreateTexture("RenderTarget.B", desc);

auto& current = (frame_index % 2 == 0) ? rtA : rtB;
auto& previous = (frame_index % 2 == 0) ? rtB : rtA;

rg.AddPass("PostProcess", [&](RgPassBuilder& pb) {
    pb.Read(previous, RHIResourceState::ShaderResource);
    pb.Write(current, RHIResourceState::RenderTarget);
    // ...
});
```

### Pattern 2: Multi-Pass Algorithm (Downsample Chain)

```cpp
std::vector<RgResourceId> mips;

auto full = rg.CreateTexture("Mip0", { .width=1024, .height=1024, ... });
mips.push_back(full);

for (int i = 1; i < 10; ++i) {
    auto mip = rg.CreateTexture(
        std::format("Mip{}", i).c_str(),
        { .width = 1024 >> i, .height = 1024 >> i, ... }
    );
    mips.push_back(mip);

    rg.AddPass(std::format("Downsample{}", i).c_str(),
               [=](RgPassBuilder& pb) {
        pb.Read(mips[i-1], RHIResourceState::ShaderResource);
        pb.Write(mips[i], RHIResourceState::RenderTarget);
        pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
            // Sample from mip i-1, render to mip i
            // ...
        });
    });
}
```

### Pattern 3: Optional Effects (Post-Processing Chain)

```cpp
auto sceneColorPostAA = sceneColor;  // Start with base scene color

if (settings.ssrEnabled) {
    auto ssr = rg.CreateTexture("SSR", ...);
    rg.AddPass("ScreenSpaceReflections", [=](RgPassBuilder& pb) {
        pb.Read(sceneColor, RHIResourceState::ShaderResource);
        pb.Write(ssr, RHIResourceState::UnorderedAccess);
        // ... setup ...
    });
    sceneColorPostAA = ssr;  // Chain output as input to next effect
}

if (settings.bloomEnabled) {
    auto bloom = rg.CreateTexture("Bloom", ...);
    rg.AddPass("Bloom", [=](RgPassBuilder& pb) {
        pb.Read(sceneColorPostAA, RHIResourceState::ShaderResource);
        pb.Write(bloom, RHIResourceState::RenderTarget);
        // ... setup ...
    });
    sceneColorPostAA = bloom;
}

// Final tonemap uses whatever the last effect produced
rg.AddPass("Tonemap", [=](RgPassBuilder& pb) {
    pb.Read(sceneColorPostAA, RHIResourceState::ShaderResource);
    pb.Write(backBuffer, RHIResourceState::RenderTarget);
    // ... setup ...
});
```

## Debugging & Profiling

### Debug Logging

The render graph logs at `Renderer` category, `Trace`/`Info` levels:

```cpp
// Enable trace logs to see graph structure:
uz::SetLogLevel("Renderer", "Trace");

// Output:
// RenderGraph: created transient texture 'GBuffer.Albedo' (1920x1080, id=0)
// RenderGraph: registered pass 'GBuffer Pass' (3 accesses)
// RenderGraph: compiling 10 passes, 15 resources
// RenderGraph: culled pass 'Bloom Pass'  // Unused bloom effect
// RenderGraph: compiled — 10 passes (1 culled), 23 barriers
```

### Diagnostics

```cpp
// After Compile(), query graph state:
uint32_t totalPasses = rg.GetPassCount();
uint32_t culledPasses = rg.GetCulledPassCount();
uint32_t totalBarriers = rg.GetBarrierCount();

UZ_LOG(Renderer, Info, "Graph stats: {} passes ({} culled), {} barriers",
       totalPasses, culledPasses, totalBarriers);

// Verify all resources have valid IDs:
for (uint32_t i = 0; i < rg.GetResourceCount(); ++i) {
    auto id = RgResourceId{ i };
    const auto& entry = rg.GetResource(id);
    UZ_ASSERT(entry.name != nullptr);
}
```

### GPU Debugging

- **PIX / Xcode Profiler**: Each pass is a command list marker (via `IRHICommandList::BeginMarker()`)
- **Resource naming**: Physical resources are named using pass + resource name
  - Example: `"Lighting Pass / GBuffer.Albedo"`
- **Barrier visualization**: Barriers appear as state transitions in GPU debuggers

## Performance Considerations

### Memory Efficiency

**Transient resource aliasing**: The compiler identifies non-overlapping resources and reuses GPU memory.

```cpp
// 100 MB scene color (frames 0-5)
// 100 MB temp buffer (frames 10-15)
// → Same 100 MB allocation (saves 100 MB VRAM)
```

To inspect aliasing:

```cpp
// (Future: RgBuilder::GetAliasingPlan() would expose this)
```

### Barrier Optimization

The compiler minimizes barriers:
- Skips no-op transitions (Undefined → any, same → same)
- Groups barriers before a pass (single `ResourceBarrier()` call)
- Avoids false dependencies (proper DAG analysis)

### Pass Culling

**Free optimization**: Disabled effects are culled automatically.

```cpp
// Without explicit guard:
if (enable_bloom) {
    rg.AddPass("Bloom", ...);  // Not added if disabled
}
// OR
rg.AddPass("Bloom", ...);  // Added, but nobody reads output
rg.Compile();  // Compiler culls the pass automatically
```

Both approaches work; the second is more declarative.

## Common Errors & Solutions

| Error | Cause | Solution |
|-------|-------|----------|
| `UZ_CHECK(id.IsValid())` fails | Accessing invalid resource | Verify resource created/imported before use |
| `UZ_CHECK(entry.kind == Texture)` fails | Type mismatch (buffer as texture) | Call `GetBuffer()` for buffers, `GetTexture()` for textures |
| `UZ_CHECK(pass.executeFn != nullptr)` fails | No callback set | Call `pb.SetExecutionCallback()` in AddPass setup |
| Pass never executes | Pass is culled (output not consumed) | Read output in another pass or import it |
| Barriers appear wrong | State tracking mismatch | Verify all reads/writes declare correct state |
| Crash on resource access | Accessing resource not declared by pass | Declare all reads/writes before SetExecutionCallback |

## Engine Comparison

| Feature | Unreal 5 (FRDI) | Godot 4.x | **UzEngine** |
|---------|-----------------|-----------|------------|
| **Model** | FRDGBuilder DAG | Manual RenderingDevice | RgBuilder + RgGraph DAG |
| **Resource declaration** | `CreateTexture(Desc)` | `render_target_create()` | `CreateTexture(Desc)` |
| **Pass registration** | `GraphBuilder.AddPass()` | Manual state mgmt | `AddPass(setupFn)` |
| **Dependency declaration** | `AddPass` closure | Explicit barriers | `Read()` / `Write()` |
| **Barrier insertion** | Automatic | Manual | Automatic |
| **Pass culling** | Automatic | N/A | Automatic |
| **Resource aliasing** | Automatic (transient reuse) | N/A | Automatic (transient reuse) |
| **Async compute** | Supported (FRDGBuilder tracks) | Limited | Graph-aware (future) |

## Rules

1. **MUST** create new `RgBuilder` each frame — do not reuse across frames
2. **MUST** declare all resource reads/writes before `SetExecutionCallback()` — graph analysis requires complete dependency info
3. **MUST** call `Compile()` before `Execute()` — compilation is required for optimization
4. **MUST NOT** access RHI handles in pass callbacks — only use `RgPassResources::GetTexture()` / `GetBuffer()`
5. **MUST NOT** manually insert barriers in pass callbacks — declare resource state, let compiler insert barriers
6. **MUST NOT** create physical resources in pass callbacks — use graph resource creation
7. **SHOULD** name all passes and resources — essential for debugging and GPU profiler interpretation
8. **SHOULD** check pass output is consumed (not culled) if pass is critical — use culled count or explicit reads
9. **NEVER** share `RgBuilder` across threads — render graph is single-threaded
10. **NEVER** modify `RgResourcePool` directly — only via `RgBuilder::Execute()`

## Related Documentation

- **Render World** (`12-render-world.md`) — Scene data structures passed to render graph
- **Visibility Pipeline** (`12-visibility-pipeline.md`) — Frustum culling feeding render graph
- **Frame Pipeline** (`12-frame-pipeline.md`) — Game loop integration with render graph
- **RHI** (`03-rhi.md`) — GPU abstraction layer below render graph
- **Shader Pipeline** (`05-shader-pipeline.md`) — Shader compilation for render passes

## Phase Roadmap

| Phase | Milestone | Features |
|-------|-----------|----------|
| **3a-3.1.0** | Render Graph Core | Topological sort, barrier scheduling, pass culling, resource pooling |
| **3a-3.2.0** | Forward+ PBR | Material system integration, lighting architecture |
| **3a-3.3.0** | Deferred Pipeline | GBuffer, light accumulation, transparency |
| **3a-3.4.0** | Shadow Maps | CSM, VSM, omnidirectional shadows |
| **3a-3.5.0** | Post-Processing | Bloom, tonemap, TAA, color grading |
