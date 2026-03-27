# Proposal: DX11 Render Pipeline Fixes (Phase 2)

## Why
DX11 backend compiles all shaders (FXC/SM5.0) and creates resources correctly, but the rendered scene is BLACK — no sky, no lighting, no textures on meshes. Stats overlay works, 9 draw calls emit 29184 triangles, ground plane geometry is visible but unlit. The DX11 RHI layer (command list, resource creation, CB binding, SRV-RTV conflicts) was investigated and found CORRECT. The problem is in the rendering pipeline layer above the RHI.

## What Changes

### Diagnostic Phase
1. Enable DX11 debug layer to capture SRV/RTV warnings during rendering
2. Add diagnostic logging to verify GBuffer texture handles are valid on DX11
3. Test with flat-color lighting PS to isolate GBuffer read vs pipeline issue
4. Verify m_hdrSceneColor has both RTV and SRV on DX11

### Fixes (based on diagnostics)
5. Fix GBuffer data not reaching deferred lighting (likely SRV-RTV aliasing or resource resolution)
6. Fix HDR scene color creation/binding on DX11 if missing RTV
7. Fix any FXC-specific shader compilation issues (SM5 path differences)
8. Fix tonemap reading correct HDR texture on DX11

## Impact
- Affected code: runtime/engine/renderer/frame/, runtime/engine/renderer/passes/
- Breaking change: NO
- User benefit: DX11 produces identical visual output to DX12 SM6.8
