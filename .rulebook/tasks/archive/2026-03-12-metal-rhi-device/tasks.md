## 1. Implementation

### Device Initialization
- [ ] 1.1 Create `metal_device.h` — `MetalDevice : public IRHIDevice` class declaration
- [ ] 1.2 Implement `Init()` — acquire `MTL::CreateSystemDefaultDevice()`
- [ ] 1.3 Detect GPU family — `device->supportsFamily()` for Apple/Mac/Common families
- [ ] 1.4 Map GPU family to `ShaderModel` enum (Apple7+ → SM6.6 equivalent)
- [ ] 1.5 Detect argument buffer tier — require Tier 2 for bindless, fallback to Tier 1
- [ ] 1.6 Create primary `MTL::CommandQueue` (graphics + compute)
- [ ] 1.7 Create secondary `MTL::CommandQueue` (transfer/copy)
- [ ] 1.8 Log device info — adapter name, GPU family, memory budget, argument buffer tier

### Argument Buffer (Bindless Descriptors)
- [ ] 1.9 Create argument buffer for texture descriptors (equivalent to DX12 CBV_SRV_UAV heap)
- [ ] 1.10 Create argument buffer for sampler descriptors
- [ ] 1.11 Implement descriptor index allocation/deallocation
- [ ] 1.12 Implement `GetBufferSRVIndex()` / `GetTextureSRVIndex()` / `GetSamplerIndex()`

### Resource Creation (IRHIDevice methods)
- [ ] 1.13 Implement `CreateBuffer()` — `device->newBuffer()` with storage mode mapping
- [ ] 1.14 Implement `DestroyBuffer()` — release + reclaim descriptor index
- [ ] 1.15 Implement `MapBuffer()` — return `buffer->contents()` for shared/managed buffers
- [ ] 1.16 Implement `GetBufferGPUAddress()` — return `buffer->gpuAddress()` (Metal 3+)
- [ ] 1.17 Implement `CreateTexture()` — `device->newTexture()` with format/usage mapping
- [ ] 1.18 Implement `DestroyTexture()` — release + reclaim descriptor index
- [ ] 1.19 Implement `GetTextureDesc()` — reconstruct RHITextureDesc from MTL::Texture properties
- [ ] 1.20 Implement `CreateShader()` — compile MSL source via `device->newLibrary()` then `library->newFunction()`
- [ ] 1.21 Implement `DestroyShader()` — release function and library
- [ ] 1.22 Implement `CreateGraphicsPipeline()` — build `MTL::RenderPipelineDescriptor` and create state
- [ ] 1.23 Implement `DestroyPipeline()` — release pipeline state
- [ ] 1.24 Implement `CreateSampler()` — build `MTL::SamplerDescriptor` and create state
- [ ] 1.25 Implement `DestroySampler()` — release + reclaim descriptor index

### Device Queries
- [ ] 1.26 Implement `GetAdapterName()` — return `device->name()->utf8String()`
- [ ] 1.27 Implement `GetFeatureLevel()` — return GPU family string
- [ ] 1.28 Implement `GetMemoryBudget()` — `device->recommendedMaxWorkingSetSize()` + `device->currentAllocatedSize()`
- [ ] 1.29 Implement `GetShaderModel()` — return mapped shader model from GPU family
- [ ] 1.30 Implement `GetBackend()` — return `RHIBackend::Metal`
- [ ] 1.31 Implement `IsBindless()` — return argument buffers tier >= 2
- [ ] 1.32 Implement `WaitIdle()` — submit empty command buffer + `waitUntilCompleted()`

### Delegation
- [ ] 1.33 Implement `CreateSwapChain()` — construct `MetalSwapChain`
- [ ] 1.34 Implement `CreateFence()` — construct `MetalFence` via `device->newSharedEvent()`
- [ ] 1.35 Implement `CreateCommandList()` — construct `MetalCommandList`
- [ ] 1.36 Implement `DestroySwapChain/Fence/CommandList()` — delete instances

## 2. Testing

- [ ] 2.1 Test device creation acquires system default device
- [ ] 2.2 Test GPU family detection returns valid family
- [ ] 2.3 Test shader model mapping for known GPU families
- [ ] 2.4 Test argument buffer tier detection
- [ ] 2.5 Test buffer creation with all storage modes (Default, Upload, Readback)
- [ ] 2.6 Test buffer map/unmap for shared storage mode
- [ ] 2.7 Test texture creation with common formats (RGBA8, BGRA8, D32Float)
- [ ] 2.8 Test shader creation from valid MSL source
- [ ] 2.9 Test shader creation from invalid MSL source returns invalid handle
- [ ] 2.10 Test pipeline creation with valid vertex + fragment shaders
- [ ] 2.11 Test sampler creation with all filter/address modes
- [ ] 2.12 Test descriptor index allocation and deallocation (no leaks)
- [ ] 2.13 Test `WaitIdle()` completes without hanging
- [ ] 2.14 Test `GetAdapterName()` returns non-empty string
- [ ] 2.15 Test `GetMemoryBudget()` returns positive values
- [ ] 2.16 Verify 95%+ coverage

## 3. Documentation

- [ ] 3.1 Document MetalDevice architecture in `docs/engine/03-rhi/15-metal-backend.md`
- [ ] 3.2 Document argument buffer descriptor system (Metal's bindless equivalent)
- [ ] 3.3 Document GPU family → ShaderModel mapping table
- [ ] 3.4 Document initialization sequence with "Why This Order" rationale
