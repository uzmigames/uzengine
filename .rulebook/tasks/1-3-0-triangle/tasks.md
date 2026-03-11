# 1.3.0 — Triangle to Screen

## Phase 1: Prerequisites & D3D12MA Fix ✅
- [x] 1.1 Fix D3D12MA heap flags panic (`-fno-sanitize=undefined` on D3D12MA TU)
- [x] 1.2 Re-enable 10 DISABLED buffer/texture creation tests
- [x] 1.3 Verify all tests pass (445 pass, 0 fail)

## Phase 2: Vertex/Index Buffer Upload
- [x] 2.1 Implement upload heap → default heap copy utility (`UploadContext`)
- [x] 2.2 Define vertex layout struct (Vertex, SimpleVertex in `vertex.h`)
- [x] 2.3 Create vertex buffer from CPU data (staging + copy via UploadContext)
- [x] 2.4 Create index buffer (uint16/uint32 via UploadContext)
- [x] 2.5 Wire `SetVertexBuffer` / `SetIndexBuffer` / `DrawIndexed` in DX12 command list
- [ ] 2.6 Render hardcoded colored triangle (no texture, no camera)
- [ ] 2.7 Tests: buffer upload, vertex layout validation, draw call smoke test

## Phase 3: Constant Buffers
- [ ] 3.1 Per-frame constant buffer struct (ViewProj, CameraPos, Time)
- [ ] 3.2 Per-object constant buffer struct (Model, NormalMatrix, MaterialIndex)
- [ ] 3.3 Ring-buffer allocator for per-frame CB uploads (triple-buffered)
- [ ] 3.4 Bind constant buffers via root signature (CBV in root descriptor or bindless)
- [ ] 3.5 Tests: CB creation, ring-buffer wrap-around, data upload verification

## Phase 4: Texture Pipeline
- [ ] 4.1 Integrate stb_image (header-only, add to thirdparty/)
- [ ] 4.2 Implement texture upload (staging → default heap → generate mips if needed)
- [ ] 4.3 Create SRV descriptor in bindless heap for loaded textures
- [ ] 4.4 Create sampler objects (linear, point, anisotropic) in sampler heap
- [ ] 4.5 Render textured quad (SM6.6 `ResourceDescriptorHeap[index]`)
- [ ] 4.6 Tests: texture load/upload, SRV allocation, sampler creation

## Phase 5: Camera System
- [ ] 5.1 Perspective projection (FOV, aspect, near, far)
- [ ] 5.2 Orthographic projection
- [ ] 5.3 View matrix from position + quaternion orientation
- [ ] 5.4 FPS camera controller (WASD + mouse look, delta-time smoothing)
- [ ] 5.5 Camera feeds per-frame CB (ViewProj matrix)
- [ ] 5.6 Tests: projection math, view matrix, camera transforms

## Phase 6: Mesh Loading
- [ ] 6.1 Define `.uzmesh` format (JSON header + binary blob)
- [ ] 6.2 Implement mesh loader (parse header, read vertex/index binary)
- [ ] 6.3 Create test mesh (cube or export Stanford bunny to .uzmesh)
- [ ] 6.4 Render loaded mesh with transforms + camera
- [ ] 6.5 Tests: mesh format parsing, vertex/index data integrity

## Phase 7: Shaders & Integration
- [ ] 7.1 Vertex shader: MVP transform, pass UVs/normals to PS
- [ ] 7.2 Pixel shader: sample albedo texture, basic N·L directional light
- [ ] 7.3 Root signature auto-generated from SPIRV-Reflect
- [ ] 7.4 PSO creation for the basic mesh pipeline
- [ ] 7.5 Full integration test: textured mesh + camera + lighting
- [ ] 7.6 Tests: shader compilation, PSO creation, end-to-end render

## Success Criteria
- [ ] Colored triangle on screen
- [ ] Textured quad with correct UV mapping
- [ ] 3D mesh renders with camera controls
- [ ] Zero DX12 validation errors
- [ ] 95%+ test coverage on new code
