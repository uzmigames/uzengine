## 1. Implementation

### Test Infrastructure
- [ ] 1.1 Create `tests/engine/rhi/metal/` directory
- [ ] 1.2 Add Metal test files to build.zig (macOS-only conditional)
- [ ] 1.3 Create `MetalTestFixture` — initializes MetalDevice for test use

### Device Tests (test_metal_device.cpp)
- [ ] 1.4 Test device creation acquires valid MTLDevice
- [ ] 1.5 Test `GetAdapterName()` returns non-empty GPU name
- [ ] 1.6 Test `GetBackend()` returns `RHIBackend::Metal`
- [ ] 1.7 Test `GetShaderModel()` returns valid shader model
- [ ] 1.8 Test `IsBindless()` returns true on Argument Buffer Tier 2 GPUs
- [ ] 1.9 Test `GetMemoryBudget()` returns positive budget and usage
- [ ] 1.10 Test `CreateBuffer()` with vertex, index, constant, structured usage
- [ ] 1.11 Test `CreateBuffer()` with all memory types (Default, Upload, Readback)
- [ ] 1.12 Test `MapBuffer()` returns valid pointer for Upload/Readback buffers
- [ ] 1.13 Test `MapBuffer()` fails for Default (private) buffers
- [ ] 1.14 Test `CreateTexture()` with RGBA8, BGRA8, D32Float, RGBA16Float formats
- [ ] 1.15 Test `CreateTexture()` with RenderTarget, DepthStencil, ShaderResource usage
- [ ] 1.16 Test `CreateShader()` from valid MSL source
- [ ] 1.17 Test `CreateShader()` from invalid source returns invalid handle
- [ ] 1.18 Test `CreateGraphicsPipeline()` with valid vertex + fragment shaders
- [ ] 1.19 Test `CreateSampler()` with all filter/address combinations
- [ ] 1.20 Test `DestroyBuffer/Texture/Shader/Pipeline/Sampler` — no crash, handle invalidated
- [ ] 1.21 Test `WaitIdle()` completes without timeout
- [ ] 1.22 Test generational handles — destroyed handle's generation is rejected

### Command List Tests (test_metal_command_list.cpp)
- [ ] 1.23 Test Begin/End/Execute lifecycle
- [ ] 1.24 Test `BeginRenderPass()` with single color attachment
- [ ] 1.25 Test `BeginRenderPass()` with color + depth attachment
- [ ] 1.26 Test `EndRenderPass()` finalizes encoder
- [ ] 1.27 Test `SetPipeline()` applies render pipeline state
- [ ] 1.28 Test `SetVertexBuffer()` binds buffer at index
- [ ] 1.29 Test `Draw()` with triangle list topology
- [ ] 1.30 Test `DrawIndexed()` with 16-bit and 32-bit indices
- [ ] 1.31 Test `CopyBuffer()` copies data correctly
- [ ] 1.32 Test `CopyBufferToTexture()` uploads texture data
- [ ] 1.33 Test encoder auto-switching (render → blit → render)
- [ ] 1.34 Test `ResourceBarrier()` is a no-op (doesn't crash)

### SwapChain Tests (test_metal_swapchain.cpp)
- [ ] 1.35 Test swap chain creation from NSWindow
- [ ] 1.36 Test `GetCurrentBackBuffer()` returns valid texture handle
- [ ] 1.37 Test `GetCurrentBackBufferIndex()` cycles through 0-2
- [ ] 1.38 Test `Resize()` updates dimensions
- [ ] 1.39 Test `GetFormat()` returns BGRA8Unorm
- [ ] 1.40 Test `Present()` with vsync on and off

### Fence Tests (test_metal_fence.cpp)
- [ ] 1.41 Test fence creation — initial value is 0
- [ ] 1.42 Test `IsComplete(0)` returns true immediately
- [ ] 1.43 Test `GetCompletedValue()` returns 0 initially
- [ ] 1.44 Test `Signal()` + `WaitCPU()` round-trip
- [ ] 1.45 Test `WaitCPU()` with already-completed value returns immediately
- [ ] 1.46 Test monotonic signaling (1, 2, 3)

### Resource Conversion Tests (test_metal_resources.cpp)
- [ ] 1.47 Test `ToMTLPixelFormat()` for all RHIFormat values
- [ ] 1.48 Test `ToMTLStorageMode()` for all RHIMemoryType values
- [ ] 1.49 Test `ToMTLSamplerAddressMode()` for all address modes
- [ ] 1.50 Test `ToMTLSamplerMinMagFilter()` for all filter modes

### Shader Compilation Tests (test_metal_shader_slang.cpp)
- [ ] 1.51 Test HLSL vertex shader → MSL compilation
- [ ] 1.52 Test HLSL pixel shader → MSL compilation
- [ ] 1.53 Test HLSL compute shader → MSL compilation
- [ ] 1.54 Test register binding translation (t0→texture, s0→sampler, b0→buffer)
- [ ] 1.55 Test compilation error diagnostics include line numbers
- [ ] 1.56 Test compiled MSL loads as valid MTLLibrary

## 2. Coverage

- [ ] 2.1 Verify 95%+ coverage on `metal_device.cpp`
- [ ] 2.2 Verify 95%+ coverage on `metal_command_list.cpp`
- [ ] 2.3 Verify 95%+ coverage on `metal_swapchain.mm`
- [ ] 2.4 Verify 95%+ coverage on `metal_fence.cpp`
- [ ] 2.5 Verify 95%+ coverage on `metal_resources.h` (conversion functions)
- [ ] 2.6 Verify 95%+ coverage on `slang_compiler.cpp`
- [ ] 2.7 Generate coverage report and add to CI

## 3. Documentation

- [ ] 3.1 Document test setup requirements (macOS with Metal-capable GPU)
- [ ] 3.2 Document how to run Metal tests (`zig build test --target-os macos`)
- [ ] 3.3 Add Metal test count to RHI overview doc
