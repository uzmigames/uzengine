## 1. Implementation

### Thirdparty Setup
- [ ] 1.1 Add `metal-cpp` headers to `thirdparty/metal-cpp/` (Apple's official C++ wrapper for Metal)
- [ ] 1.2 Create `runtime/engine/rhi/metal/` directory

### Resource Wrappers
- [ ] 1.3 Define `MetalBuffer` struct — `MTL::Buffer*`, size, stride, storage mode, mapped pointer, SRV index, debug name
- [ ] 1.4 Define `MetalTexture` struct — `MTL::Texture*`, format, width/height/depth, mips, array size, usage, SRV index, debug name
- [ ] 1.5 Define `MetalShader` struct — `MTL::Function*`, `MTL::Library*`, stage (vertex/fragment/compute), entry point name
- [ ] 1.6 Define `MetalPipeline` struct — `MTL::RenderPipelineState*` or `MTL::ComputePipelineState*`, pipeline type enum, topology, blend/depth state cache
- [ ] 1.7 Define `MetalSampler` struct — `MTL::SamplerState*`, descriptor index, filter/address mode cache

### Utility
- [ ] 1.8 Implement `ToMTLPixelFormat(RHIFormat)` — map all `RHIFormat` values to `MTL::PixelFormat`
- [ ] 1.9 Implement `ToMTLStorageMode(RHIMemoryType)` — map `Default/Upload/Readback` to `Private/Shared/Shared`
- [ ] 1.10 Implement `ToMTLResourceUsage(RHIBufferUsage/RHITextureUsage)` — map usage flags
- [ ] 1.11 Implement `ToMTLSamplerAddressMode`, `ToMTLSamplerMinMagFilter`, `ToMTLCompareFunction`
- [ ] 1.12 Add `UZ_DECLARE_LOG_CATEGORY(MetalRHI)` for Metal-specific logging

## 2. Testing

- [ ] 2.1 Test `ToMTLPixelFormat` maps all RHIFormat values correctly (compile-time static_assert where possible)
- [ ] 2.2 Test `ToMTLStorageMode` maps all RHIMemoryType values
- [ ] 2.3 Test `ToMTLSamplerAddressMode` and filter mappings
- [ ] 2.4 Verify all resource structs are trivially destructible (no hidden ownership)
- [ ] 2.5 Verify 95%+ coverage on conversion functions

## 3. Documentation

- [ ] 3.1 Document Metal resource types in `docs/engine/03-rhi/15-metal-backend.md`
- [ ] 3.2 Add Metal format mapping table (RHIFormat → MTLPixelFormat)
- [ ] 3.3 Document storage mode mapping rationale (why Shared for Upload/Readback)
