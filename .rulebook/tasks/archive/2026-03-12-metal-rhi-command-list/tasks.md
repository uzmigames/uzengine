## 1. Implementation

### Core Command Buffer
- [ ] 1.1 Create `metal_command_list.h` — `MetalCommandList : public IRHICommandList` declaration
- [ ] 1.2 Store `MTL::CommandBuffer*`, current encoder pointer, and encoder type enum (render/compute/blit/none)
- [ ] 1.3 Implement `Begin()` — acquire new `MTL::CommandBuffer` from command queue
- [ ] 1.4 Implement `End()` — end active encoder if any
- [ ] 1.5 Implement `Execute()` — `commandBuffer->commit()`
- [ ] 1.6 Implement triple-buffered command buffer rotation (3 frames in flight)

### Encoder Management
- [ ] 1.7 Implement `EnsureRenderEncoder()` — create/reuse render encoder, end other encoder types
- [ ] 1.8 Implement `EnsureComputeEncoder()` — create compute encoder, end other encoder types
- [ ] 1.9 Implement `EnsureBlitEncoder()` — create blit encoder, end other encoder types
- [ ] 1.10 Implement `EndActiveEncoder()` — call `endEncoding()` on current encoder

### Render Pass
- [ ] 1.11 Implement `BeginRenderPass()` — build `MTL::RenderPassDescriptor`, set color/depth attachments, load/store actions
- [ ] 1.12 Implement `EndRenderPass()` — end render encoder
- [ ] 1.13 Implement `ClearRenderTarget()` — configure loadAction = `.clear` with clear color
- [ ] 1.14 Implement `ClearDepthStencil()` — configure depthLoadAction = `.clear` with clear value

### Pipeline & Resource Binding
- [ ] 1.15 Implement `SetPipeline()` — `renderEncoder->setRenderPipelineState()` or `computeEncoder->setComputePipelineState()`
- [ ] 1.16 Implement `SetVertexBuffer()` — `renderEncoder->setVertexBuffer(buffer, offset, index)`
- [ ] 1.17 Implement `SetIndexBuffer()` — store index buffer reference + type for draw calls
- [ ] 1.18 Implement `SetRootConstants()` — `setVertexBytes()` + `setFragmentBytes()` (push constants)
- [ ] 1.19 Implement `SetDescriptorIndex()` — write index to argument buffer, `useResource()` for residency
- [ ] 1.20 Implement `SetDescriptorTable()` — bind argument buffer at specified index
- [ ] 1.21 Implement `SetRootCBV()` — `setVertexBuffer()` with GPU address

### Draw & Dispatch
- [ ] 1.22 Implement `Draw()` — `renderEncoder->drawPrimitives(type, start, count)`
- [ ] 1.23 Implement `DrawIndexed()` — `renderEncoder->drawIndexedPrimitives(type, count, indexType, indexBuffer, offset)`
- [ ] 1.24 Implement `Dispatch()` — `computeEncoder->dispatchThreadgroups(groupCount, threadsPerGroup)`
- [ ] 1.25 Map `RHIPrimitiveTopology` to `MTL::PrimitiveType` (triangles, lines, points)

### Barriers & Copies
- [ ] 1.26 Implement `ResourceBarrier()` — no-op with trace log (Metal manages transitions automatically)
- [ ] 1.27 Implement `CopyBuffer()` — `blitEncoder->copyFromBuffer(src, srcOffset, dst, dstOffset, size)`
- [ ] 1.28 Implement `CopyTexture()` — `blitEncoder->copyFromTexture(src, dst)`
- [ ] 1.29 Implement `CopyBufferToTexture()` — `blitEncoder->copyFromBuffer(toTexture:)`

### Viewport & Scissor
- [ ] 1.30 Implement viewport setting — `renderEncoder->setViewport()` (no Y-flip needed, Metal matches DX12 NDC)
- [ ] 1.31 Implement scissor rect — `renderEncoder->setScissorRect()`

## 2. Testing

- [ ] 2.1 Test Begin/End/Execute lifecycle
- [ ] 2.2 Test encoder auto-switching (render → blit → render)
- [ ] 2.3 Test BeginRenderPass creates render encoder with correct attachments
- [ ] 2.4 Test EndRenderPass finalizes render encoder
- [ ] 2.5 Test SetPipeline applies pipeline state
- [ ] 2.6 Test SetVertexBuffer binds buffer at correct index
- [ ] 2.7 Test Draw emits drawPrimitives call
- [ ] 2.8 Test DrawIndexed with 16-bit and 32-bit index buffers
- [ ] 2.9 Test CopyBuffer produces correct blit encoder commands
- [ ] 2.10 Test ResourceBarrier is a no-op (doesn't crash, logs trace)
- [ ] 2.11 Test viewport and scissor rect setting
- [ ] 2.12 Verify 95%+ coverage

## 3. Documentation

- [ ] 3.1 Document encoder-based command model in `docs/engine/03-rhi/15-metal-backend.md`
- [ ] 3.2 Document automatic encoder switching design
- [ ] 3.3 Document why ResourceBarrier is a no-op on Metal
- [ ] 3.4 Add Metal command model vs DX12/Vulkan comparison diagram
