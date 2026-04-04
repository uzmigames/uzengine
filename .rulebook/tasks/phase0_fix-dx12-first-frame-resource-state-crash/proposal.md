# Proposal: Fix DX12 First-Frame Resource State Crash

## Why

After merging origin/main into feat/vfx-application-integration, ALL Application-based demos crash on the first frame with CommandList::Close returning E_FAIL (0x80004005). The DX12 debug layer reports multiple resource state mismatches where the render graph assumes textures are in UAV/RenderTarget/ShaderResource state, but they are actually in Common state (freshly created). This is a P0 blocker — no Application-based rendering works after the merge.

## What Changes

### 1. Render Graph First-Frame Texture Import States (frame_setup.cpp)
Import HDR_SceneColor, GBuffer textures, and ShadowAtlas as Common on frame 0.

### 2. IBL BRDF LUT Bake — Missing Common→UAV Barrier (ibl_baker.cpp)
Add explicit Common→UAV barrier before compute dispatch in BakeBRDFLut(). Add error logging if bake pipeline fails silently.

### 3. Atmosphere Precompute — Wrong Import States (atmosphere_renderer.cpp)
Import TransmittanceLUT, MultiScatterLUT, DeltaIrradiance, ScatteringDensity3D as Common on first precompute. Complete the partial fix already applied.

### 4. VFX aliveCount Per-Frame Reset — Missing UAV→CopyDst Barrier (frame_vfx_inline.cpp)
Add UAV→CopyDst barrier before CopyBuffer, CopyDst→UAV after.

### 5. VFX Zero Staging Buffer — Use-After-Free (gpu_particle_buffer.cpp)
Store staging buffer handle for later cleanup instead of immediate destruction.

## Impact

- Affected specs: RHI barrier management, render graph resource import, atmosphere precompute
- Affected code: frame_setup.cpp, ibl_baker.cpp, atmosphere_renderer.cpp, frame_vfx_inline.cpp, gpu_particle_buffer.cpp
- Breaking change: NO
- User benefit: Application-based demos render correctly after main merge
