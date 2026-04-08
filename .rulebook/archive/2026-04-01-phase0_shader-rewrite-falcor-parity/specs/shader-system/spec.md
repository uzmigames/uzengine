# Shader System Rewrite — Spec Delta

## ADDED Requirements

### Requirement: Falcor BRDF Parity
Every specular BRDF function (D_GGX, V_SmithGGXCorrelated, F_Schlick) MUST produce identical output to Falcor's IsotropicGGX.slang and Fresnel.slang within 1e-4 tolerance for stable formulas, 1e-3 for chained transcendentals.

#### Scenario: GGX NDF matches Falcor
Given alpha=0.5 and NdotH=0.8
When D_GGX is evaluated in both UzEngine HLSL and Falcor Slang
Then the results MUST differ by less than 1e-4

#### Scenario: Frostbite diffuse energy conservation
Given roughness=0.5, NdotL=0.7, NdotV=0.6, LdotH=0.8
When EvalFrostbiteDiffuse is computed
Then energyFactor MUST equal lerp(1.0, 1.0/1.51, roughness) exactly
And the result MUST match Falcor FrostbiteDiffuseBRDF.slang evalWeight * M_1_PI

### Requirement: Root Signature Compliance
Every shader MUST declare cbuffer registers matching the engine root signature: b0=root constants, b1=PerFrameCB, b2=PerObjectCB or LightingCB, b4=ShadowDataCB. No other register bindings SHALL be used for cbuffers.

#### Scenario: Shader uses unregistered cbuffer
Given a shader declares `cbuffer Foo : register(b3)`
When the pipeline is created
Then CreatePipelineState SHALL fail with hr=0x80070057
And the shader MUST be fixed to use b0/b1/b2/b4 only

#### Scenario: Forward shader register conflict
Given mesh_ps.hlsl needs PerObjectCB at b2
When it also needs light type structs from light_types.hlsli
Then it MUST define `UZ_LIGHT_TYPES_NO_CB` before including light_types.hlsli
And LightingCB at b2 SHALL NOT be declared

### Requirement: SM5 Bindless Compatibility
Every usage of `ResourceDescriptorHeap[]` MUST be guarded by `#ifdef UZ_BINDLESS` with a register-bound SM5 fallback in the `#else` branch.

#### Scenario: Shader compiled without UZ_BINDLESS
Given UZ_BINDLESS is not defined
When the shader is compiled for SM5
Then it MUST compile without errors
And all textures MUST be declared with explicit register(tN) bindings

### Requirement: HLSL Keyword Safety
No shader parameter or local variable SHALL use HLSL reserved keywords as identifiers. Known violations: `linear`, `point`, `line`, `triangle`.

#### Scenario: Parameter named 'linear'
Given a function parameter is named `linear`
When compiled by DXC
Then DXC SHALL emit "modifiers must appear before type" error
And the parameter MUST be renamed (e.g., `linearColor`)

### Requirement: Vector Select for Conditional Assignment
All vector conditional assignments MUST use `select()` instead of ternary `?:` when the condition is a vector type (float2/float3/float4 comparison).

#### Scenario: Octahedral normal wrap-around
Given OctEncode needs `n.xy >= 0.0 ? 1.0 : -1.0`
When compiled for SM6.8
Then DXC SHALL emit "condition must be scalar" error for ternary
And the code MUST use `select(n.xy >= 0.0, 1.0, -1.0)` instead

### Requirement: Vertex Fetch via ConstantBuffer Push Constants
The vertex shader MUST use `ConstantBuffer<PushConstants> push : register(b0)` for bindless vertex fetch, matching the C++ `SetDescriptorIndex(kSlotRootConstants, vbSrvIndex)` pattern which sets exactly 1 DWORD.

#### Scenario: C++ sets 1 DWORD root constant
Given C++ calls `SetDescriptorIndex(slot0, mesh.vbSrvIndex)`
When the VS reads `push.vertexBufferIndex`
Then it SHALL read the correct SRV index
And index buffer access SHALL be via IA (SetIndexBuffer), NOT via root constants

#### Scenario: Instanced draw path
Given C++ calls `SetRootConstants(slot0, {vbSrv, instanceSrv}, 2)`
When the VS is compiled with UZ_INSTANCED defined
Then PushConstants SHALL contain both vertexBufferIndex and instanceBufferIndex
And instance transforms SHALL be read from `StructuredBuffer<InstanceData>`

### Requirement: PerObjectCB Texture Sentinel Convention
PerObjectCB texture SRV indices MUST use `0` as "no texture" sentinel (index 0 = white fallback). PerFrameCB SRV indices MUST use `UINT32_MAX` (0xFFFFFFFF) as sentinel. These two conventions SHALL NOT be mixed.

#### Scenario: GBuffer PS checks for albedo texture
Given albedoIndex is 0 (no texture assigned)
When gbuffer_ps.hlsl evaluates `albedoIndex > 0`
Then the condition SHALL be false
And the shader SHALL use baseColorTint as albedo without sampling

### Requirement: Groupshared at File Scope
All `groupshared` variable declarations MUST be at file scope (outside any function). DXC rejects groupshared locals inside functions.

#### Scenario: Radiance cache SH reduction
Given the shader needs groupshared arrays for parallel SH reduction
When `groupshared float gs_shR[9][64]` is declared inside main()
Then DXC SHALL emit "'groupshared' is not a valid modifier for a local variable"
And the declaration MUST be moved above the main() function

### Requirement: Incremental Pass Validation
Each rendering pass MUST be tested individually before enabling the next. The test protocol: uncomment the pass in scene_render_frame.cpp, build, run `--frames 30`, check for DEVICE_REMOVED. If it crashes, fix before proceeding.

#### Scenario: Lighting pass crashes GPU
Given RegisterDeferredLightingPass is uncommented
When the engine runs for 30 frames
Then if DEVICE_REMOVED occurs, the lighting shader MUST be debugged
And no additional passes SHALL be uncommented until the crash is resolved

### Requirement: Citation Trail
Every shader function implementing a published algorithm MUST include a `/// @ref Author Year "Title"` citation in its doc comment. Every numeric constant MUST be named and cited.

#### Scenario: GGX NDF citation
Given D_GGX implements the Trowbridge-Reitz distribution
When the function is written
Then it MUST contain `/// @ref Walter et al. 2007 "Microfacet Models for Refraction"` or equivalent
And the kMinGGXAlpha constant MUST cite Falcor BSDFConfig.slangh
