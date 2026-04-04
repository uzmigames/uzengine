# RHI Resource State Management — First-Frame Initialization

## ADDED Requirements

### Requirement: First-Frame Common State Import
The render graph ImportTexture calls SHALL use RHIResourceState::Common as the initial state for all freshly created textures on frame 0, since DX12 CreateTexture returns resources in D3D12_RESOURCE_STATE_COMMON.

#### Scenario: HDR SceneColor first frame
Given the SceneRenderer starts its first frame (frameIndex == 0)
When SetupFrameData imports HDR_SceneColor into the render graph
Then the import state MUST be RHIResourceState::Common

#### Scenario: HDR SceneColor subsequent frames
Given the SceneRenderer starts frame N where N > 0
When SetupFrameData imports HDR_SceneColor into the render graph
Then the import state MUST be RHIResourceState::RenderTarget

#### Scenario: GBuffer Depth first frame
Given the SceneRenderer starts its first frame (frameIndex == 0)
When SetupFrameData imports GBuffer_Depth into the render graph
Then the import state MUST be RHIResourceState::Common

### Requirement: IBL Bake Explicit Barriers
The IBL BRDF LUT bake SHALL emit an explicit Common→UAV barrier before the first compute dispatch, rather than relying on implicit DX12 state promotion.

#### Scenario: BRDF LUT bake barriers
Given BakeBRDFLut creates a fresh texture in Common state
When the compute dispatch writes to the BRDF LUT
Then a Common→UnorderedAccess barrier MUST be emitted before the dispatch
And a UnorderedAccess→ShaderResource barrier MUST be emitted after the dispatch

### Requirement: Atmosphere Precompute First-Frame States
The atmosphere renderer SHALL import precompute textures (TransmittanceLUT, MultiScatterLUT, ScatteringDensity3D, DeltaIrradiance) as Common state on the first precompute pass.

#### Scenario: TransmittanceLUT first precompute
Given transmittanceLutFilled is false (first atmosphere precompute)
When atmosphere_renderer imports TransmittanceLUT into the render graph
Then the import state MUST be RHIResourceState::Common

### Requirement: VFX AliveCount Reset Barriers
The VFX inline dispatch SHALL emit UAV→CopyDst barrier before the per-frame aliveCount CopyBuffer reset, and CopyDst→UAV barrier after.

#### Scenario: Per-frame aliveCount reset
Given the VFX inline dispatch resets aliveCount to 0 via CopyBuffer
When the aliveCount buffer is in UAV state from the previous frame
Then a UAV→CopyDst barrier MUST be emitted before CopyBuffer
And a CopyDst→UAV barrier MUST be emitted after CopyBuffer

### Requirement: Staging Buffer Lifetime
Staging buffers used for CopyBuffer commands SHALL remain alive until the command list completes GPU execution. Immediate destruction after recording CopyBuffer causes DX12 device removal.

#### Scenario: VFX zero staging buffer lifetime
Given InitializeData creates a staging buffer and records CopyBuffer commands
When the command list has not yet been submitted to the GPU
Then the staging buffer MUST NOT be destroyed
And the staging buffer SHALL be destroyed after GPU completion via DestroyStaging()
