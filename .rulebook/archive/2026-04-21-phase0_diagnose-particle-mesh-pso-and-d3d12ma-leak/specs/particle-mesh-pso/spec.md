# particle-mesh-pso

## ADDED Requirements

### Requirement: DX12 InfoQueue callback wired

The DX12 RHI backend SHALL register an `ID3D12InfoQueue::PushStorageFilter`
callback that forwards every debug-layer error/warning to the runtime
log (category `RHI`) with its full text.

#### Scenario: PSO failure prints the debug-layer reason

Given a PSO creation that the debug layer rejects
When `CreateGraphicsPipelineState` returns an error HRESULT
Then the runtime log contains both the generic `hr=0x...` line AND
a subsequent `[RHI] [error] DebugLayer: ...` line with the specific
invariant that was violated.

### Requirement: ParticleMesh PSOs succeed on launch

`ParticleMeshOpaque` and `ParticleMeshTransparent` SHALL both succeed
`CreateGraphicsPipelineState` during engine initialisation.

#### Scenario: No ParticleMesh* in the permanently-failed set

Given a clean demo startup
When the pipeline library's warmup / on-demand paths run
Then neither `ParticleMeshOpaque` nor `ParticleMeshTransparent`
appears in the runtime log's "CreateCachedPipeline: permanently
failed" line.

### Requirement: Clean D3D12MA shutdown

The demo SHALL shut down after 60 frames without the D3D12MA
`m_pMetadata->IsEmpty()` assertion firing.

#### Scenario: `vfx_viewport_app.exe --headless --frames 60` exits 0

Given a fresh build of the demo
When it runs headless for 60 frames and exits via `Quit()`
Then the process exit code is 0 and stderr contains no "Assertion
failed" or "leak" lines.
