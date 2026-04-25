# particle-mesh-bindless

## ADDED Requirements

### Requirement: Bindless branch in ParticleMesh VS

`particle_mesh_vs.hlsl` SHALL have a `#ifdef UZ_BINDLESS` branch that
reads `g_instanceTransforms` via `ResourceDescriptorHeap[cbvIndex]`
instead of the legacy `register(t0)` binding.

#### Scenario: Bindless build compiles against SM6.6 root sig

Given a runtime negotiated at SM6.6
When `ShaderManager::CreateShader(ParticleMeshVS)` runs
Then DXC compiles with `UZ_BINDLESS` defined and the resulting bytecode
exposes no SRV root parameter at t0.

### Requirement: ParticleMesh PSOs succeed on launch

Both `ParticleMeshOpaque` and `ParticleMeshTransparent` SHALL succeed
`CreateGraphicsPipelineState` during engine init.

#### Scenario: No permanent-failure log for ParticleMesh PSOs

Given a fresh demo startup
When the pipeline library creates the mesh PSOs
Then the runtime log contains no `"CreateCachedPipeline: permanently
failed for 'ParticleMesh*'"` line.

### Requirement: D3D12MA named-allocation tracking

The D3D12MA wrapper SHALL emit a per-allocation log line at shutdown
for every allocation still alive when the allocator destructor runs.

#### Scenario: Leak reveals owner name

Given a run that leaks at least one named allocation
When the allocator destructor executes
Then the runtime log contains a `[RHI] [error] D3D12MA live allocation:
name=...` line for each live allocation BEFORE the assertion trips.

### Requirement: Clean shutdown

`vfx_viewport_app.exe --headless --frames 60` SHALL exit with status 0
and no `Assertion failed` message.

#### Scenario: 60-frame headless clean exit

Given a fresh demo build
When the process runs 60 frames then exits via `Quit()`
Then the exit code is 0 and stderr contains no "Assertion failed" or
"leak" lines.
