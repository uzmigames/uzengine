# RHI — Resource Barriers

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/engine/rhi/rhi_types.h` (RHIResourceState, RHIBarrierDesc), `runtime/engine/rhi/rhi.h` (IRHICommandList::ResourceBarrier)
> **Depends on**: RHI types

## Purpose

GPU resources can only be used in specific states — a texture must be in `RenderTarget` state to receive draw output, and `ShaderResource` state to be read by a shader. Transitions between states require explicit barriers. Missing or incorrect barriers cause GPU hangs, data corruption, or validation layer errors.

Barriers exist because modern GPUs are massively parallel: without explicit synchronization, the GPU might read a texture that's still being written by a previous pass.

## Resource States

```cpp
enum class RHIResourceState : uint32_t {
    Undefined         = 0,         // Initial/unknown state
    Common            = 1 << 0,    // General-purpose (implicit promotion)
    VertexBuffer      = 1 << 1,    // Bound as vertex buffer
    IndexBuffer       = 1 << 2,    // Bound as index buffer
    ConstantBuffer    = 1 << 3,    // Bound as constant buffer
    ShaderResource    = 1 << 4,    // Read by shaders (SRV)
    UnorderedAccess   = 1 << 5,    // Read/write from shaders (UAV)
    RenderTarget      = 1 << 6,    // Color render target output
    DepthStencilWrite = 1 << 7,    // Depth-stencil write
    DepthStencilRead  = 1 << 8,    // Depth-stencil read-only
    CopySrc           = 1 << 9,    // Source of GPU copy
    CopyDst           = 1 << 10,   // Destination of GPU copy
    Present           = 1 << 11,   // Ready for swap chain present
    IndirectArgument  = 1 << 12,   // Indirect draw/dispatch arguments
};
```

## Barrier API

```cpp
struct RHIBarrierDesc {
    RHITextureHandle texture;       // Texture to transition (or invalid)
    RHIBufferHandle  buffer;        // Buffer to transition (or invalid)
    RHIResourceState stateBefore;   // Current state
    RHIResourceState stateAfter;    // Desired state
};

// Single barrier
RHIBarrierDesc barrier{};
barrier.texture     = renderTarget;
barrier.stateBefore = RHIResourceState::RenderTarget;
barrier.stateAfter  = RHIResourceState::ShaderResource;
cmdList->ResourceBarrier(&barrier, 1);

// Batched barriers (preferred — fewer GPU stalls)
RHIBarrierDesc barriers[3] = {
    { .texture = gbufferAlbedo, .stateBefore = RenderTarget, .stateAfter = ShaderResource },
    { .texture = gbufferNormal, .stateBefore = RenderTarget, .stateAfter = ShaderResource },
    { .texture = depthBuffer,   .stateBefore = DepthStencilWrite, .stateAfter = DepthStencilRead },
};
cmdList->ResourceBarrier(barriers, 3);
```

## Common Transition Patterns

### Render Target → Shader Resource (GBuffer → Lighting)

```cpp
// After GBuffer pass, before Lighting pass
barrier.texture     = gbufferAlbedo;
barrier.stateBefore = RHIResourceState::RenderTarget;
barrier.stateAfter  = RHIResourceState::ShaderResource;
```

### Shader Resource → Render Target (reuse RT for next pass)

```cpp
barrier.texture     = hdrTarget;
barrier.stateBefore = RHIResourceState::ShaderResource;
barrier.stateAfter  = RHIResourceState::RenderTarget;
```

### Present → Render Target → Present (swap chain back buffer)

```cpp
// Start of frame: Present → RenderTarget
barrier.texture     = backBuffer;
barrier.stateBefore = RHIResourceState::Present;
barrier.stateAfter  = RHIResourceState::RenderTarget;
cmdList->ResourceBarrier(&barrier, 1);

// ... render to back buffer ...

// End of frame: RenderTarget → Present
barrier.stateBefore = RHIResourceState::RenderTarget;
barrier.stateAfter  = RHIResourceState::Present;
cmdList->ResourceBarrier(&barrier, 1);
```

### Copy Upload → Shader Resource (texture upload)

```cpp
// Upload data from staging buffer to texture (CopyDst)
// Then transition to ShaderResource for rendering
barrier.texture     = texture;
barrier.stateBefore = RHIResourceState::CopyDst;
barrier.stateAfter  = RHIResourceState::ShaderResource;
```

### UAV Barrier (compute read-after-write)

When a compute shader writes to a UAV and the next dispatch reads it, a UAV barrier is needed:

```cpp
barrier.buffer      = computeOutput;
barrier.stateBefore = RHIResourceState::UnorderedAccess;
barrier.stateAfter  = RHIResourceState::UnorderedAccess;  // Same state = UAV barrier
```

## DX12 Mapping

The DX12 backend maps `RHIResourceState` to `D3D12_RESOURCE_STATES`:

| RHI State | D3D12 State |
|-----------|-------------|
| `Common` | `D3D12_RESOURCE_STATE_COMMON` |
| `VertexBuffer` | `D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER` |
| `IndexBuffer` | `D3D12_RESOURCE_STATE_INDEX_BUFFER` |
| `ConstantBuffer` | `D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER` |
| `ShaderResource` | `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE \| NON_PIXEL_SHADER_RESOURCE` |
| `UnorderedAccess` | `D3D12_RESOURCE_STATE_UNORDERED_ACCESS` |
| `RenderTarget` | `D3D12_RESOURCE_STATE_RENDER_TARGET` |
| `DepthStencilWrite` | `D3D12_RESOURCE_STATE_DEPTH_WRITE` |
| `DepthStencilRead` | `D3D12_RESOURCE_STATE_DEPTH_READ` |
| `CopySrc` | `D3D12_RESOURCE_STATE_COPY_SOURCE` |
| `CopyDst` | `D3D12_RESOURCE_STATE_COPY_DEST` |
| `Present` | `D3D12_RESOURCE_STATE_PRESENT` |

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Barrier model | Automatic via `FRDGBuilder` (render graph tracks states) | Manual `RenderingDevice::barrier()` calls | Automatic via render graph (RgBuilder, section 12.4) |
| State tracking | Render graph + per-resource state cache | Per-resource `VkImageLayout` tracking | Render graph tracks state, automatic inference |
| Batching | Automatic — render graph batches barriers between passes | Manual | Automatic — render graph batches barriers between passes |
| Validation | D3D12 debug layer in dev builds | Vulkan validation layers | D3D12 debug layer + DRED breadcrumbs |

**Automatic barrier insertion**: UzEngine's render graph (section 12.4) automatically infers barriers from resource dependencies declared in pass Read/Write operations. The compiler schedules barriers at optimal points between passes — similar to UE5's `FRDGBuilder`. Application code declares resource states, and the graph handles insertion automatically.

## Rules

1. **ALWAYS** transition resources to the correct state before use — missing barriers are the #1 cause of GPU bugs
2. **ALWAYS** batch barriers when transitioning multiple resources between the same passes — fewer GPU stalls
3. **ALWAYS** transition back buffers: `Present → RenderTarget` at frame start, `RenderTarget → Present` before `Present()`
4. **NEVER** assume a resource's current state — track it explicitly or use the render graph (when available)
5. **ALWAYS** use UAV barriers between consecutive compute dispatches that read/write the same resource
