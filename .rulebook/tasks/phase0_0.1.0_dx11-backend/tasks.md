## 1. Implementation

### Device & Factory
- [ ] 1.1 Create `runtime/core/rhi/dx11/` directory structure
- [ ] 1.2 Implement `DX11Device` — DXGI adapter enumeration, D3D11CreateDevice (feature level 11_0+), debug layer
- [ ] 1.3 Implement D3D11 descriptor/resource management (SRV/UAV/RTV/DSV creation and tracking)
- [ ] 1.4 Register DX11 backend in `rhi_backend.h` factory (`CreateRHIDevice` with `UZ_BACKEND_DX11`)
- [ ] 1.5 Add `UZ_BACKEND_DX11` compile flag to build.zig, add DX11 source files

### IRHIDevice (29 methods)
- [ ] 1.6 Implement buffer operations: CreateBuffer, DestroyBuffer, MapBuffer, GetBufferSRVIndex, GetBufferGPUAddress
- [ ] 1.7 Implement texture operations: CreateTexture, DestroyTexture, GetTextureSRVIndex, GetTextureDesc
- [ ] 1.8 Implement shader operations: CreateShader (from DXBC bytecode), DestroyShader
- [ ] 1.9 Implement pipeline operations: CreateGraphicsPipeline (input layout + VS/PS + state objects), DestroyPipeline
- [ ] 1.10 Implement sampler operations: CreateSampler, DestroySampler, GetSamplerIndex
- [ ] 1.11 Implement swapchain: CreateSwapChain, DestroySwapChain
- [ ] 1.12 Implement fence: CreateFence (emulated via D3D11 queries), DestroyFence
- [ ] 1.13 Implement command list: CreateCommandList (deferred context), DestroyCommandList
- [ ] 1.14 Implement WaitIdle (flush immediate context), GetAdapterName, GetFeatureLevel, GetMemoryBudget

### IRHICommandList (28 methods)
- [ ] 1.15 Implement Begin/End/Execute (deferred context record → immediate context execute command list)
- [ ] 1.16 Implement SetPipeline, SetVertexBuffer, SetIndexBuffer, SetViewport, SetScissor
- [ ] 1.17 Implement SM5 binding: SetDescriptorIndex → slot-based SRV, SetRootConstants → constant buffer update, SetRootCBV, SetDescriptorTable
- [ ] 1.18 Implement DrawIndexed, Draw, Dispatch
- [ ] 1.19 Implement ResourceBarrier (no-op on DX11, implicit state management)
- [ ] 1.20 Implement ClearRenderTarget, ClearDepthStencil
- [ ] 1.21 Implement CopyBuffer, CopyTexture, CopyBufferToTexture
- [ ] 1.22 Implement BeginRenderPass (OMSetRenderTargets), EndRenderPass

### IRHISwapChain (7 methods)
- [ ] 1.23 Implement DX11SwapChain: DXGI swap chain creation (FLIP_DISCARD or FLIP_SEQUENTIAL fallback)
- [ ] 1.24 Implement Present (vsync + tearing), Resize, GetWidth/Height/Format, GetCurrentBackBuffer/Index

### IRHIFence (4 methods)
- [ ] 1.25 Implement DX11Fence: emulated timeline fence via D3D11 event queries + CPU-side monotonic counter
- [ ] 1.26 Implement Signal, WaitCPU (poll loop with sleep), IsComplete, GetCompletedValue

### Integration
- [ ] 1.27 Verify all 5 existing demos work with DX11 backend (clear_screen, triangle, textured_quad, camera_test, mesh_test)
- [ ] 1.28 Add `--rhi dx11` CLI flag to select backend at runtime

## 2. Testing

- [ ] 2.1 Port test_dx12_device.cpp → test_dx11_device.cpp (all 20 tests adapted for DX11)
- [ ] 2.2 Port test_dx12_render_loop.cpp → test_dx11_render_loop.cpp (3 tests: 10-frame cycle, fence sync, timing)
- [ ] 2.3 Port test_dx12_resources.cpp → test_dx11_resources.cpp (12 tests: resource pools, handle validation)
- [ ] 2.4 Test DX11-specific: SM5 input layout creation, slot-based SRV binding, deferred context execution
- [ ] 2.5 Test DX11Fence emulation: monotonic values, WaitCPU timeout, IsComplete accuracy
- [ ] 2.6 Test format conversion: all 22 RHIFormat → DXGI_FORMAT mappings
- [ ] 2.7 Cross-backend comparison: same draw calls produce equivalent results on DX12 and DX11
- [ ] 2.8 Verify 95%+ coverage on all new DX11 code

## 3. Documentation

- [ ] 3.1 Add `docs/engine/03-rhi/dx11-backend.md` documenting DX11-specific behaviors (no barriers, emulated fences, SM5 binding)
- [ ] 3.2 Update `docs/engine/03-rhi/01-overview.md` with multi-backend architecture and selection mechanism
