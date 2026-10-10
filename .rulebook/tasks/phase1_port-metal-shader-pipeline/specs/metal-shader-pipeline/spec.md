# Metal Shader Pipeline — Spec Delta

## ADDED Requirements

### Requirement: Slang Compiler Runtime Availability
The engine SHALL load the vendored Slang shared library
(`thirdparty/slang/bin/libslang.dylib` on macOS) at runtime via dlopen. When the
library is absent, `SlangCompiler::Init()` MUST return false and log a clear error.

#### Scenario: In-process HLSL to MSL compilation
Given the vendored libslang.dylib present
When compiling a trivial HLSL vertex shader with entry `main`
Then the result SHALL succeed and contain MSL text with a `[[vertex]]` function
And the emitted entry function SHALL be named `main_0`

### Requirement: ShaderPipeline Compiles HLSL to MSL on macOS
On `__APPLE__`, `ShaderPipeline::LoadBytecodeFromSource` SHALL compile HLSL to
MSL source text via Slang and return it as bytes. The pipeline MUST NOT define
`UZ_BINDLESS` for the Metal target (SM5 register-bound shader path). Identical
requests within a session MUST be served from a cache without recompiling.

#### Scenario: Embedded engine shader compiles
Given the pipeline initialized with a Metal device
When `LoadBytecodeFromSource("fullscreen_vs.hlsl", source)` is called
Then the returned bytes SHALL contain valid MSL with a `[[vertex]]` entry function

#### Scenario: Invalid source reports diagnostics
Given an initialized pipeline
When invalid HLSL is compiled
Then the result SHALL be empty and the Slang diagnostic output SHALL be logged

### Requirement: ShaderManager Creates Metal Shaders via Slang
On `__APPLE__`, `ShaderManager::Init` SHALL succeed with a Metal device, and
`CreateShader`/`CreateShaderWithDefines` SHALL compile the embedded engine HLSL
to MSL and create the RHI shader object with the Slang-emitted entry name
(`main` → `main_0`).

#### Scenario: MeshVS creation
Given an initialized ShaderManager on a Metal device
When `CreateShader(EngineShader::MeshVS, Vertex)` is called
Then a valid RHIShaderHandle SHALL be returned

### Requirement: Metal Compute Pipeline Creation
`MetalDevice::CreateComputePipeline` SHALL create a valid
MTLComputePipelineState from a compute shader handle and return a valid
`RHIPipelineHandle`. An invalid or non-compute shader handle MUST return an
invalid pipeline handle with an error log.

#### Scenario: Trivial kernel pipeline
Given a Metal device and an MSL compute kernel shader handle
When CreateComputePipeline is called
Then a valid pipeline handle SHALL be returned and the kernel SHALL dispatch
successfully
