# Tasks: Engine API Consolidation

## Layer 1: Math & Utility Foundations
- [x] Add `Mat4::NormalMatrix()` — transpose(inverse(model)) helper in engine_math.h
- [x] Add `Mat4::TRS(Vec3 pos, float uniformScale)` — translation + uniform scale
- [x] Add `Mat4::TRS(Vec3 pos, Vec3 scale)` — translation + non-uniform scale
- [x] Add `Mat4::TRS(Vec3 pos, Quat rot, Vec3 scale)` — full TRS construction
- [x] Unit tests for all new Mat4 methods (26 tests)

## Layer 2: Vertex Layout System
- [x] Define `VertexFormatFlags` bitmask enum (HasPosition, HasNormal, HasTangent, HasUV0, HasUV1, HasUV2, HasColor)
- [x] Implement `VertexLayout::Standard()` — returns RHIInputElementDesc[] + stride for standard PBR vertex (48 bytes)
- [x] Implement `VertexLayout::FromFlags(VertexFormatFlags)` — dynamic layout builder
- [x] Implement `GetVertexStride(VertexFormatFlags)` helper
- [x] Unit tests for vertex layout generation (45 tests)

## Layer 3: Camera Convenience Methods
- [x] Add `Camera::SetHorizontalFOV(float hFovDeg, float aspect)` — hFOV to vFOV conversion
- [x] Add `Camera::SetPerspectiveDegrees(float vFovDeg, float aspect, float near, float far)`
- [x] Add `Camera::SetLookFromAngles(Vec3 pos, float yawDeg, float pitchDeg)`
- [ ] Move `FPSCameraController` from `demos/common/` to `engine/camera/` (or `engine/app/`)
- [ ] Update all demo build.zig files to link from new engine location
- [x] Unit tests for camera convenience methods (20 tests)

## Layer 4: TextureLoader
- [ ] Implement `TextureLoader::Load(IRHIDevice*, IRHICommandList*, const std::string& path) → TextureHandle`
- [ ] Wraps: stbi_load → CreateTexture → UploadTexture → barrier → WaitIdle → free
- [ ] Returns SRV index alongside texture handle
- [ ] Support for common formats: JPEG, PNG, TGA, HDR
- [ ] Error logging via UZ_LOG on load failure
- [ ] Unit tests for TextureLoader

## Layer 5: GPUMesh Uploader
- [ ] Define `GPUMesh` struct (vertexBuffer, indexBuffer, vbSrvIndex, indexFormat, indexCount)
- [ ] Implement `UploadMesh(IRHIDevice*, const Mesh&, bool bindless) → GPUMesh`
- [ ] Implement `DestroyGPUMesh(IRHIDevice*, GPUMesh&)`
- [ ] Handle both SM6.6 (StructuredBuffer) and SM5 (VertexBuffer) paths
- [ ] Unit tests for GPUMesh upload

## Layer 6: GBuffer Class
- [ ] Extract `GBuffer` class from demos into `engine/renderer/gbuffer.h/.cpp`
- [ ] `GBuffer::Create(IRHIDevice*, uint32_t width, uint32_t height)`
- [ ] `GBuffer::Resize(IRHIDevice*, uint32_t width, uint32_t height)` — destroy + recreate
- [ ] `GBuffer::Destroy(IRHIDevice*)`
- [ ] Store texture handles + SRV indices for albedo, normal, metalRough, depth
- [ ] Unit tests for GBuffer lifecycle

## Layer 7: ShaderManager
- [x] Implement `ShaderManager` — auto-detects bindless from device, sets up ShaderPipeline + compilers
- [x] `ShaderManager::Init(IRHIDevice*)` — configures cache dir, target backend, FXC fallback
- [x] `ShaderManager::GetBytecode(EngineShader) → span<uint8_t>` — single call per shader
- [x] `ShaderManager::CreateShader(EngineShader) → RHIShaderHandle` — compile + create in one call
- [x] `ShaderManager::Shutdown()`
- [ ] Unit tests for ShaderManager

## Layer 8: PerFrameCB Builder & SceneLighting
- [x] Define `SceneLighting` struct (directional dir/color, ambient color)
- [x] Implement `BuildPerFrameCB(Camera, float totalTime, float dt, uint32_t frame, SceneLighting) → PerFrameCB`
- [x] Include `ComputeInvViewProj` internally
- [x] Unit tests for PerFrameCB builder (22 tests)

## Layer 9: PipelineLibrary (Standard PSOs)
- [ ] Implement `PipelineLibrary` — caches standard PSOs per device
- [ ] `PipelineLibrary::GetForwardPBR(IRHIDevice*, bool bindless) → RHIPipelineHandle`
- [ ] `PipelineLibrary::GetGBuffer(IRHIDevice*, bool bindless) → RHIPipelineHandle`
- [ ] `PipelineLibrary::GetLighting(IRHIDevice*) → RHIPipelineHandle`
- [ ] `PipelineLibrary::GetGBufferDebug(IRHIDevice*) → RHIPipelineHandle`
- [ ] Uses `VertexLayout::Standard()` internally — no hardcoded input layouts
- [ ] Uses `ShaderManager` internally — no manual shader compilation
- [ ] Unit tests for PipelineLibrary

## Layer 10: DemoApplication Base Class
- [ ] Implement `DemoApplication` base class in `engine/app/demo_application.h/.cpp`
- [ ] Encapsulates: PlatformWindow, IRHIDevice, IRHISwapChain, IRHICommandList, fences, frame sync
- [ ] Virtual hooks: `OnInit()`, `OnFrame(float dt)`, `OnResize(uint32_t w, uint32_t h)`, `OnShutdown()`
- [ ] Auto handles: minimized window idle, resize detection, FPS cap, fence management
- [ ] Exposes: `GetDevice()`, `GetCommandList()`, `GetSwapChain()`, `GetFrameIndex()`
- [ ] Optional FPS stats logging on shutdown
- [ ] Integration test with minimal demo

## Layer 11: Demo Migration
- [ ] Migrate `textured_quad` demo to use DemoApplication + new APIs
- [ ] Migrate `pbr_test` demo to use DemoApplication + TextureLoader + GPUMesh + GBuffer + ShaderManager + PipelineLibrary + Camera helpers + PerFrameCB builder
- [ ] Migrate `deferred_test` demo to use same stack
- [ ] Verify all demos still compile and produce identical rendering output
- [ ] Each migrated demo should be < 150 lines of actual demo logic

## Documentation
- [x] Create docs/engine/12-rendering/12-vertex-layout.md
- [x] Create docs/engine/14-camera/02-camera-utilities.md (01a-camera-utilities.md)
- [x] Update docs/engine/08-resource-system/07-texture-pipeline.md with runtime loading API
- [x] Update docs/engine/02-math/02-transform.md with Mat4 convenience methods
- [x] Update docs/engine/12-rendering/03-frame-pipeline.md with PerFrameCB builder
- [ ] Update docs/engine/09-application-lifecycle/06-demo-guide.md with DemoApplication usage
