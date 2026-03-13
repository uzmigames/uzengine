# Specification: Engine API Consolidation

## Scenario 1: DemoApplication lifecycle

**Given** a subclass of DemoApplication with OnInit/OnFrame/OnShutdown overrides
**When** Run() is called
**Then** window + device + swapchain + fences are created automatically
**And** OnInit() is called once
**And** OnFrame(dt) is called each frame with correct delta time
**And** resize is handled automatically (OnResize called, swapchain resized)
**And** OnShutdown() is called before cleanup

## Scenario 2: TextureLoader one-call loading

**Given** a JPEG file path and an initialized device
**When** TextureLoader::Load(device, cmdList, "assets/albedo.jpg") is called
**Then** the texture is loaded via stbi, uploaded to GPU, barriers applied, staging released
**And** a valid RHITextureHandle + SRV index are returned

## Scenario 3: GPUMesh upload

**Given** a loaded Mesh from .uzmesh file
**When** UploadMesh(device, mesh, bindless=true) is called
**Then** vertex data is uploaded as StructuredBuffer with SRV index
**And** index data is uploaded as IndexBuffer
**And** a GPUMesh struct with all handles is returned

## Scenario 4: VertexLayout::Standard()

**Given** the standard PBR Vertex struct (position, normal, uv0, tangent)
**When** VertexLayout::Standard() is called
**Then** it returns 4 RHIInputElementDesc entries with correct semantics, formats, and offsets
**And** stride equals sizeof(Vertex) == 48

## Scenario 5: Mat4::NormalMatrix()

**Given** a model matrix with non-uniform scale (1, 2, 1)
**When** Mat4::NormalMatrix() is called
**Then** it returns transpose(inverse(model))
**And** normals transformed by this matrix remain perpendicular to the surface

## Scenario 6: Camera::SetHorizontalFOV()

**Given** a camera and hFOV=90° with aspect=16:9
**When** SetHorizontalFOV(90.f, 16.f/9.f) is called
**Then** the vertical FOV is correctly computed as 2*atan(tan(45°) / (16/9))
**And** the projection matrix uses this vFOV

## Scenario 7: Demo migration — pbr_test line count

**Given** the current pbr_test (~800 lines)
**When** migrated to DemoApplication + all new APIs
**Then** the demo-specific logic is < 150 lines
**And** rendering output is visually identical

## Scenario 8: ShaderManager auto-detection

**Given** a device with IsBindless() == true
**When** ShaderManager::Init(device) is called
**Then** target backend is set to "dx12_sm66"
**And** ShaderPipeline is configured with bindless=true
**And** no FXC compiler is initialized (DXC only)

## Scenario 9: PipelineLibrary caching

**Given** PipelineLibrary initialized with a device
**When** GetForwardPBR(device, bindless) is called twice
**Then** the same RHIPipelineHandle is returned both times (cached, not recreated)

## Scenario 10: GBuffer resize

**Given** a GBuffer created at 1280x720
**When** Resize(device, 1920, 1080) is called
**Then** old textures are destroyed
**And** new textures are created at 1920x1080
**And** SRV indices are updated
