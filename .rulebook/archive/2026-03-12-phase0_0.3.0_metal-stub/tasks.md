## 1. Implementation

### Stub Classes
- [ ] 1.1 Create `runtime/core/rhi/metal/` directory structure
- [ ] 1.2 Implement MetalDevice stub — all 29 IRHIDevice methods return safe defaults with UZ_LOG error
- [ ] 1.3 Implement MetalCommandList stub — all 28 IRHICommandList methods as no-ops with UZ_LOG error
- [ ] 1.4 Implement MetalSwapChain stub — all 7 IRHISwapChain methods return safe defaults
- [ ] 1.5 Implement MetalFence stub — all 4 IRHIFence methods return safe defaults
- [ ] 1.6 Add Metal API mapping comments in each stub method (e.g., `// Metal: id<MTLBuffer> = [device newBufferWithLength:options:]`)

### Build System
- [ ] 1.7 Add `UZ_BACKEND_METAL` compile flag to build.zig (no Metal SDK link — pure C++ stubs)
- [ ] 1.8 Register Metal backend in rhi_backend.h factory (returns error on non-Apple platforms)
- [ ] 1.9 Verify Metal stub compiles on Windows without errors

### Factory Update
- [ ] 1.10 Update backend factory: `--rhi metal` rejects with clear error on Windows/Linux
- [ ] 1.11 Add `--rhi auto` logic: DX12 → Vulkan → DX11 → Metal (platform-dependent priority)

## 2. Testing

- [ ] 2.1 Test Metal stub compiles without Metal SDK or Objective-C
- [ ] 2.2 Test all stub methods return safe defaults (no crash, no UB)
- [ ] 2.3 Test factory rejects `--rhi metal` on Windows with descriptive error message
- [ ] 2.4 Test auto-detection priority: DX12 preferred on Windows, Vulkan as first fallback, DX11 as second
- [ ] 2.5 Verify 95%+ coverage on stub code (all methods covered by "returns default" tests)

## 3. Documentation

- [ ] 3.1 Add `docs/engine/03-rhi/metal-backend.md` with Metal API mapping table (RHI method → Metal equivalent)
- [ ] 3.2 Update `docs/engine/03-rhi/01-overview.md` with complete 4-backend architecture (DX12, DX11, Vulkan, Metal)
- [ ] 3.3 Document backend auto-detection priority per platform
