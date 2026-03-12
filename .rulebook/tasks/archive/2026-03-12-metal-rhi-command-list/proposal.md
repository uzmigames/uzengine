# Proposal: Metal RHI Command List (MetalCommandList)

## Why

`MetalCommandList` translates all GPU command recording — draw calls, dispatches, barriers, copies, render pass management — into Metal's encoder-based command model. Metal's command recording differs significantly from DX12/Vulkan: instead of a single command list, Metal uses typed encoders (`MTLRenderCommandEncoder`, `MTLComputeCommandEncoder`, `MTLBlitCommandEncoder`) that are created and finalized within a `MTLCommandBuffer`. This is the most architecturally different component from the DX12/Vulkan backends.

## What Changes

### New Files

1. **`metal_command_list.h`** — `MetalCommandList : public IRHICommandList` declaration
2. **`metal_command_list.cpp`** — Implementation (~1000 lines)

### Metal Command Model vs DX12/Vulkan

```
DX12/Vulkan Model:                    Metal Model:
┌─────────────────────┐               ┌─────────────────────┐
│ CommandList/Buffer   │               │ MTLCommandBuffer     │
│   SetPipeline()     │               │   ├── RenderEncoder   │
│   SetVertexBuffer() │               │   │   SetPipeline()   │
│   Draw()            │               │   │   SetVB()         │
│   ResourceBarrier() │               │   │   Draw()          │
│   Draw()            │               │   │   endEncoding()   │
│   ...               │               │   ├── BlitEncoder     │
│ End()               │               │   │   CopyBuffer()    │
└─────────────────────┘               │   │   endEncoding()   │
                                       │   └── ...             │
                                       └─────────────────────┘
```

### Key Design Decisions

- **Automatic encoder management**: The command list tracks the current encoder type. When a draw command is issued, it ensures a render encoder is active. When a copy is issued, it switches to a blit encoder. Encoder transitions are handled transparently.
- **ResourceBarrier → no-op**: Metal tracks resource states automatically. `ResourceBarrier()` is a no-op but logs a trace message for debugging. The only exception is `MTLResource::makeAliasable()` for transient resources.
- **Triple-buffered command buffers**: Like DX12/Vulkan, command buffers rotate per frame to avoid reuse while GPU is rendering.

### Metal API Mapping (IRHICommandList methods)

| IRHICommandList Method | Metal API |
|---|---|
| `Begin()` | `commandQueue->commandBuffer()` |
| `End()` | End active encoder |
| `Execute()` | `commandBuffer->commit()` |
| `SetPipeline()` | `renderEncoder->setRenderPipelineState()` |
| `SetVertexBuffer()` | `renderEncoder->setVertexBuffer(buf, offset, index)` |
| `SetIndexBuffer()` | Store for `drawIndexedPrimitives()` call |
| `SetRootConstants()` | `renderEncoder->setVertexBytes()` + `setFragmentBytes()` |
| `SetDescriptorIndex()` | Write to argument buffer, `useResource()` |
| `Draw()` | `renderEncoder->drawPrimitives()` |
| `DrawIndexed()` | `renderEncoder->drawIndexedPrimitives()` |
| `Dispatch()` | `computeEncoder->dispatchThreadgroups()` |
| `ResourceBarrier()` | No-op (Metal manages transitions) |
| `ClearRenderTarget()` | Set loadAction = `.clear` in render pass descriptor |
| `ClearDepthStencil()` | Set depthLoadAction = `.clear` in render pass descriptor |
| `CopyBuffer()` | `blitEncoder->copyFromBuffer()` |
| `CopyTexture()` | `blitEncoder->copyFromTexture()` |
| `CopyBufferToTexture()` | `blitEncoder->copyFromBuffer(toTexture:)` |
| `BeginRenderPass()` | Create `MTLRenderPassDescriptor`, begin render encoder |
| `EndRenderPass()` | `renderEncoder->endEncoding()` |

## Impact

- Affected specs: `docs/engine/03-rhi/15-metal-backend.md`
- Affected code: `runtime/engine/rhi/metal/metal_command_list.h/.cpp` (new)
- Breaking change: NO
- User benefit: GPU command recording on macOS/iOS
- Dependencies: `metal-rhi-device`, `metal-rhi-resources`
