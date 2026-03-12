# Proposal: 1.3.0 — Triangle to Screen

## Why

Milestone 1.2 (RHI) is complete — device, swapchain, command lists, fences, shaders all work. But we can only clear the screen to a solid color. To prove the RHI works end-to-end, we need to render actual geometry with textures and camera transforms.

This is the **first visual milestone** — a textured 3D mesh on screen with a controllable camera. Every future renderer feature (deferred, PBR, shadows, post-processing) builds on these foundations.

## What Changes

### 1. Vertex/Index Buffer Upload Pipeline
- Upload heap staging → GPU-only default heap copy
- Vertex layout: position (float3), normal (float3), texcoord (float2), tangent (float4)
- Index buffer (uint16/uint32)
- Draw calls: `DrawIndexed()` through RHI command list

### 2. Constant Buffer System
- Per-frame CB: view matrix, projection matrix, camera position, time
- Per-object CB: model matrix, normal matrix, material index
- Ring-buffer allocation for per-frame constants (no stalls)

### 3. Texture Pipeline
- Load PNG/JPG via stb_image → staging upload → default heap texture
- Create SRV descriptor in bindless heap
- SM6.6 access: `ResourceDescriptorHeap[index]` in shaders
- Sampler creation in sampler heap (linear, point, anisotropic)

### 4. Camera System
- Perspective projection (configurable FOV, aspect, near/far)
- Orthographic projection for editor views
- View matrix from position + orientation (quaternion)
- WASD + mouse look FPS-style controls
- Smooth movement with delta-time

### 5. Mesh Loading
- `.uzmesh` format: JSON header + binary vertex/index data
- Header: vertex layout, material refs, bounding box, index count
- Binary: packed vertex data, index data
- Load Stanford bunny or similar test mesh

### 6. Basic Shaders
- Vertex shader: transform position by MVP, pass through UVs/normals
- Pixel shader: sample albedo texture, basic directional light (N·L)
- Root signature auto-generated from SPIRV-Reflect (existing pipeline)

## Blockers

- **D3D12MA heap flags panic** must be fixed first — buffer/texture creation crashes under Zig safety checks
- stb_image dependency needed (vcpkg or header-only)

## Success Criteria

1. Colored triangle renders on screen
2. Textured quad renders correctly with UV mapping
3. 3D mesh (bunny or cube) renders with correct transforms
4. Camera orbits/flies around the mesh with WASD+mouse
5. All new code has 95%+ test coverage
6. Zero validation errors from DX12 debug layer
