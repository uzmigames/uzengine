# Shader Parameter System — Spec Delta

## ADDED Requirements

### Requirement: Shader Parameter Metadata
The system MUST provide a `ShaderParameterMetadata` struct that describes the layout of a shader parameter struct, distinguishing scalar members (CBV data) from resource members (textures, samplers, UAVs).

#### Scenario: Metadata separates scalar and resource members
Given a shader parameter struct with 3 scalar members and 2 texture members
When `GetMetadata()` is called
Then the metadata MUST report 3 scalar members with correct offsets/sizes
And 2 resource members with correct types (Texture2D, TextureCube)
And the CB size MUST equal the sum of scalar member sizes with 16-byte alignment padding

#### Scenario: Empty struct produces valid metadata
Given a shader parameter struct with zero members
When `GetMetadata()` is called
Then the metadata MUST report 0 scalar members, 0 resource members, and CB size 0

### Requirement: Macro-Based Struct Declaration
The system MUST provide `UZ_BEGIN_SHADER_PARAMETERS`, `UZ_SHADER_PARAMETER`, `UZ_SHADER_PARAMETER_TEXTURE`, `UZ_SHADER_PARAMETER_SAMPLER`, and `UZ_END_SHADER_PARAMETERS` macros that generate both the struct layout and its metadata.

#### Scenario: Macro generates matching C++ struct and metadata
Given a struct declared with UZ_BEGIN_SHADER_PARAMETERS containing a Mat4 and a Vec4
When the struct is instantiated and its metadata queried
Then `sizeof(StructType)` MUST match the metadata's reported total size
And member offsets in metadata MUST match `offsetof()` for each scalar member

#### Scenario: Texture members stored as RHITextureHandle
Given a struct with `UZ_SHADER_PARAMETER_TEXTURE(Texture2D, Albedo)`
When the struct is instantiated
Then `params.Albedo` MUST be assignable as `RHITextureHandle`
And the metadata MUST report a resource member named "Albedo" with type Texture2D

### Requirement: Shader Parameter Binder
The system MUST provide `ShaderParameterBinder::Bind()` that takes a filled struct, its metadata, a command list, and a CB allocator, and performs: (a) CB upload for scalar members, (b) texture binding per resource member, (c) sampler binding per sampler member.

#### Scenario: Binder uploads scalar data to CBV
Given a struct with ViewProj (Mat4) and CameraPos (Vec4) filled with known values
When `Bind()` is called with a CBRingAllocator
Then the allocator MUST receive a single allocation of size equal to the CB portion
And the allocated bytes MUST match the scalar members byte-for-byte

#### Scenario: Binder binds textures on DX11
Given a struct with IrradianceCubemap (TextureCube) set to a valid handle
When `Bind()` is called on a DX11 command list
Then `SetFragmentTexture(slot, handle)` MUST be called with the correct slot index
And the slot index MUST correspond to the resource member's declaration order

#### Scenario: Binder binds textures on DX12 bindless
Given a struct with IrradianceCubemap set to a valid handle on a DX12 bindless device
When `Bind()` is called
Then `SetDescriptorIndex(slot, srvHeapIndex)` MUST be called with the texture's SRV heap index
And the slot MUST be the root constants slot

#### Scenario: Binder skips invalid texture handles
Given a struct with a texture member set to `RHITextureHandle::Invalid()`
When `Bind()` is called
Then no texture binding call MUST be made for that member
And no error MUST be logged (invalid = intentionally unset)

### Requirement: HLSL Layout Compatibility
The CB portion of the shader parameter struct MUST produce a byte layout identical to the corresponding HLSL cbuffer, accounting for HLSL packing rules (float4 alignment for arrays, row_major for matrices).

#### Scenario: Mat4 members align to 16 bytes
Given a struct with a uint32_t followed by a Mat4
When the CB data is uploaded
Then the Mat4 MUST start at a 16-byte aligned offset
And the HLSL cbuffer `float4x4` MUST read the same values

#### Scenario: Float arrays pack as float4 groups
Given a struct with `float cascadeSplits[8]`
When the CB data is uploaded
Then the 8 floats MUST occupy 32 contiguous bytes in the CB
And the HLSL side MUST access them via `float4` pairs (not `float[8]` which pads to 128 bytes)

### Requirement: Backend Transparency
The same shader parameter struct definition MUST work across DX12 (bindless), DX11 (register-based), and Vulkan (descriptor set) without any `#ifdef` in the render pass code.

#### Scenario: Lighting pass works on DX12
Given DeferredLightingParams with IBL textures filled
When the pass executes on DX12
Then IBL textures MUST be accessible in the shader via bindless descriptor heap

#### Scenario: Same lighting pass works on DX11
Given the same DeferredLightingParams
When the pass executes on DX11
Then IBL textures MUST be accessible in the shader via register-bound SRVs (t5, t6, t7)
And the visual result MUST match DX12 within PSNR > 30dB
