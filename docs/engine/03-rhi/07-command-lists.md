# RHI — Command Lists & Synchronization

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/core/rhi/rhi.h` (IRHICommandList, IRHIFence), `dx12/dx12_command_list.h`, `dx12/dx12_fence.h`
> **Depends on**: RHI types, device

## Purpose

Command lists record GPU work (draws, dispatches, copies, barriers) for deferred submission. Fences synchronize CPU and GPU timelines — ensuring the CPU doesn't overwrite data the GPU is still reading, and vice versa.

## Command List Lifecycle

```
Begin()    ──→ Record commands ──→ End()    ──→ Execute()
   │                                                │
   │  Reset allocator, bind descriptor heaps        │  Submit to GPU queue
   │  (CBV_SRV_UAV + Sampler for bindless)          │  Advance frame index
   │                                                │
   ▼                                                ▼
 Recording state                              GPU executing
   │                                                │
   └── SetPipeline, SetVertexBuffer,                └── Fence signal
       SetIndexBuffer, SetViewport,                     Fence wait (CPU blocks)
       SetScissor, SetDescriptorIndex,                  │
       Draw, DrawIndexed, Dispatch,                     ▼
       ResourceBarrier, ClearRenderTarget,         Reuse: Begin() again
       BeginRenderPass, EndRenderPass,
       CopyBuffer, CopyTexture, ...
```

### Triple-Buffered Allocators

Each `DX12CommandList` owns 3 command allocators (one per frame-in-flight). When `Begin()` is called, it resets the allocator for the current frame index. This allows:

- Frame N: GPU executing allocator 0's commands
- Frame N+1: GPU executing allocator 1's commands
- Frame N+2: CPU recording into allocator 2

The caller must fence-synchronize to ensure the allocator being reset is no longer in use by the GPU.

```cpp
auto* cmdList = device->CreateCommandList(RHIQueueType::Graphics);
auto* fence = device->CreateFence();
uint64_t fenceValue = 0;

// Render loop
while (running) {
    // Wait for frame N-2 to complete (frees allocator for reuse)
    fence->WaitCPU(fenceValue - 2);

    cmdList->Begin();
    // ... record commands ...
    cmdList->End();
    cmdList->Execute();

    fenceValue++;
    fence->Signal(fenceValue);
}
```

## Queue Types

| Queue | Capabilities | DX12 Type | Use Case |
|-------|-------------|-----------|----------|
| `Graphics` | Draw + Compute + Copy | `D3D12_COMMAND_LIST_TYPE_DIRECT` | Main rendering |
| `Compute` | Compute + Copy only | `D3D12_COMMAND_LIST_TYPE_COMPUTE` | Async compute (SSAO, culling) |
| `Copy` | DMA copy only | `D3D12_COMMAND_LIST_TYPE_COPY` | Background texture uploads |

The DX12 backend creates two queues: Direct (graphics) and Copy. Compute queue is supported but not yet used for async compute.

## Render Pass Scope

The `BeginRenderPass`/`EndRenderPass` pair sets render targets and optionally clears them:

```cpp
RHIRenderPassDesc pass{};
pass.colorAttachments[0].texture   = renderTarget;
pass.colorAttachments[0].clear     = true;
pass.colorAttachments[0].clearColor[0] = 0.1f;  // R
pass.colorAttachments[0].clearColor[1] = 0.1f;  // G
pass.colorAttachments[0].clearColor[2] = 0.1f;  // B
pass.colorAttachments[0].clearColor[3] = 1.0f;  // A
pass.colorAttachmentCount = 1;

pass.depthAttachment.texture    = depthBuffer;
pass.depthAttachment.clear      = true;
pass.depthAttachment.clearDepth = 1.0f;
pass.hasDepth = true;

cmdList->BeginRenderPass(pass);
// ... draw calls ...
cmdList->EndRenderPass();
```

## Fence Synchronization

Fences use monotonically increasing uint64 values:

```cpp
auto* fence = device->CreateFence();  // Starts at value 0

// GPU signals after command execution
fence->Signal(1);          // Queue enqueues signal to value 1
fence->WaitCPU(1);         // CPU blocks until GPU completes value 1

// Non-blocking check
if (fence->IsComplete(1)) {
    // GPU finished — safe to reuse resources
}

uint64_t completed = fence->GetCompletedValue();  // Highest completed value
```

### WaitIdle

`device->WaitIdle()` is a full pipeline stall — it flushes all queues and blocks until all GPU work completes. Use sparingly (shutdown, resize, resource destruction):

```cpp
// Before resize — GPU must be idle
device->WaitIdle();
swapChain->Resize(newWidth, newHeight);
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Command recording | `FRHICommandList` with deferred immediate mode | `RenderingDevice::draw_list_begin/end` | `IRHICommandList::Begin/End/Execute` |
| Allocator strategy | Ring of allocator pools per frame | Per-frame command pool reset | Triple-buffered (3 allocators per cmd list) |
| Multi-threaded recording | Yes (`FParallelCommandListSet`) | No (single-threaded) | One thread per cmd list (parallel recording possible) |
| Sync model | GPU fence + parallel translate | Semaphores (Vulkan) | `IRHIFence` (monotonic uint64 values) |

## Rules

1. **ALWAYS** call `End()` before `Execute()` — submitting an open command list is undefined behavior
2. **ALWAYS** fence-wait before `Begin()` on a previously submitted command list — ensures the allocator is free
3. **NEVER** record into a command list from multiple threads — one thread per list
4. **ALWAYS** bind descriptor heaps on `Begin()` — the DX12 backend does this automatically
5. **NEVER** call `WaitIdle()` in the render loop — it's a full stall. Use per-frame fences instead
