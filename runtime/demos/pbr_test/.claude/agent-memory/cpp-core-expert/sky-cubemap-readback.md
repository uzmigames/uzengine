---
name: Sky cubemap readback and UAV dimension bug
description: Findings from implementing GPU cubemap readback for SH projection, including DX12 UAV view dimension bug fix
type: project
---

## GPU Readback for SH Projection — 2026-03-15

Implemented `ProjectCubemapToSH(IRHIDevice*, IRHICommandList*, RHITextureHandle, uint32_t)` in `sh_projection.cpp`.

### Changes Made
1. **RHI `CopyTextureToBuffer` extended** with `arraySlice` parameter (default 0) across all 4 backends (DX12, DX11, Vulkan, Metal). Backward compatible.
2. **DX12 UAV view dimension bug fixed** in `dx12_texture_creation.cpp`: textures with `depthOrArraySize > 1` now get `D3D12_UAV_DIMENSION_TEXTURE2DARRAY` instead of `TEXTURE2D`. This was causing `RWTexture2DArray` shader writes to be undefined behavior.
3. **Sky cubemap texture** now includes `CopySrc` usage flag for readback support.
4. **Half-float to float conversion** implemented in `sh_projection.cpp` (portable, no DirectXMath dependency).

### Known Issue
The sky cubemap readback returns all-black pixels (RGB=0, A=1.0) despite the cubemap being valid when read via SRV by the IBL baker. The `float4(0,0,0,1)` pattern matches the compute shader's early-exit path (atmosphere ray miss), suggesting either:
- The compute shader's atmosphere ray-march is not producing visible scattering (possible root constant / parameter issue)
- OR DX12 state tracking issue between the render graph's command list and the readback command list

**Why:** The IBL baker creates its OWN command list and reads the cubemap via SRV implicit promotion. The readback uses the CALLER's command list which was End/Execute/WaitIdle'd before the IBL bake.

**How to apply:** This needs further investigation. Possible next steps:
1. Enable DX12 debug layer and check for state validation errors
2. Try readback from within the render graph (as a pass, not after)
3. Verify the compute shader actually executes by writing a known test pattern
