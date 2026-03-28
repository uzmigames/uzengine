#pragma once

/// DX12 resource creation and destruction functions.
///
/// Separated from dx12_device.cpp to keep files under 1500 lines.
/// These functions are called by DX12Device::CreateBuffer/CreateTexture/etc.
/// and operate on the device's resource pools and descriptor allocators.

#include "dx12_resources.h"
#include "dx12_descriptor_allocator.h"
#include "../rhi_config.h"

#include <d3d12.h>

namespace D3D12MA { class Allocator; }

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Format Conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Converts `RHIFormat` to the corresponding `DXGI_FORMAT`.
///
/// # Returns
/// The mapped format, or `DXGI_FORMAT_UNKNOWN` for `RHIFormat::Unknown`.
[[nodiscard]] DXGI_FORMAT ToDxgiFormat(RHIFormat format);

/// Returns true if the format is a depth or depth-stencil format.
///
/// # Returns
/// `true` for `D16Unorm`, `D32Float`, `D24UnormS8Uint`, `D32FloatS8Uint`.
[[nodiscard]] bool IsDepthFormat(RHIFormat format);

/// Returns the typeless equivalent of a depth format.
///
/// Required when creating a texture that needs both DSV and SRV views —
/// the resource must use a typeless format, then each view specifies the typed format.
///
/// # Returns
/// The typeless format (e.g., `D32Float` -> `R32_TYPELESS`), or `DXGI_FORMAT_UNKNOWN`.
[[nodiscard]] DXGI_FORMAT ToTypelessDepthFormat(RHIFormat format);

/// Returns the SRV-compatible format for reading a depth texture in shaders.
///
/// # Returns
/// The readable format (e.g., `D32Float` -> `R32_FLOAT`), or `DXGI_FORMAT_UNKNOWN`.
[[nodiscard]] DXGI_FORMAT ToDepthSrvFormat(RHIFormat format);

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a D3D12MA buffer allocation with optional SRV/UAV descriptors and CPU mapping.
///
/// Handles constant buffer 256-byte alignment, initial state based on heap type,
/// persistent CPU mapping for Upload/Readback buffers, and bindless SRV/UAV creation.
///
/// # Arguments
/// * `device` — D3D12 device for descriptor creation.
/// * `allocator` — D3D12MA allocator for memory allocation.
/// * `srvUavHeap` — Descriptor allocator for SRV/UAV indices.
/// * `desc` — Buffer descriptor.
/// * `outBuffer` — Output struct filled on success.
///
/// # Returns
/// `true` on success, `false` on failure (logs error).
bool CreateDX12Buffer(ID3D12Device* device,
                      D3D12MA::Allocator* allocator,
                      DescriptorAllocator& srvUavHeap,
                      const RHIBufferDesc& desc,
                      DX12Buffer& outBuffer);

/// Releases a buffer's D3D12MA allocation, unmaps CPU pointer, and frees SRV/UAV indices.
///
/// # Arguments
/// * `buffer` — Buffer to destroy (zeroed after this call).
/// * `srvUavHeap` — Descriptor allocator to return SRV/UAV indices to.
void DestroyDX12Buffer(DX12Buffer& buffer,
                       DescriptorAllocator& srvUavHeap);

// ─────────────────────────────────────────────────────────────────────────────
// Texture Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a D3D12MA texture allocation with SRV and optional RTV/DSV/UAV descriptors.
///
/// Handles typeless formats for depth+SRV combos, optimized clear values,
/// cube map detection (arraySize==6), and 2D array SRV dimension selection.
///
/// # Arguments
/// * `device` — D3D12 device for descriptor creation.
/// * `allocator` — D3D12MA allocator for memory allocation.
/// * `srvUavHeap` — Descriptor allocator for SRV/UAV indices.
/// * `rtvHeap` — Descriptor allocator for RTV indices.
/// * `dsvHeap` — Descriptor allocator for DSV indices.
/// * `desc` — Texture descriptor.
/// * `outTexture` — Output struct filled on success.
///
/// # Returns
/// `true` on success, `false` on failure (logs error).
bool CreateDX12Texture(ID3D12Device* device,
                       D3D12MA::Allocator* allocator,
                       DescriptorAllocator& srvUavHeap,
                       DescriptorAllocator& rtvHeap,
                       DescriptorAllocator& dsvHeap,
                       const RHITextureDesc& desc,
                       DX12Texture& outTexture);

/// Releases a texture's D3D12MA allocation and frees all descriptor indices.
///
/// # Arguments
/// * `texture` — Texture to destroy (zeroed after this call).
/// * `srvUavHeap` — Descriptor allocator to return SRV/UAV indices to.
/// * `rtvHeap` — Descriptor allocator to return RTV index to.
/// * `dsvHeap` — Descriptor allocator to return DSV index to.
void DestroyDX12Texture(DX12Texture& texture,
                        DescriptorAllocator& srvUavHeap,
                        DescriptorAllocator& rtvHeap,
                        DescriptorAllocator& dsvHeap);

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a sampler descriptor in the global sampler heap.
///
/// # Arguments
/// * `device` — D3D12 device for sampler creation.
/// * `samplerHeap` — Descriptor allocator for sampler indices.
/// * `desc` — Sampler descriptor (filter, addressing, LOD).
/// * `outSampler` — Output struct filled on success.
///
/// # Returns
/// `true` on success, `false` if the sampler heap is full.
bool CreateDX12Sampler(ID3D12Device* device,
                       DescriptorAllocator& samplerHeap,
                       const RHISamplerDesc& desc,
                       DX12Sampler& outSampler);

/// Frees a sampler's descriptor index in the global sampler heap.
void DestroyDX12Sampler(DX12Sampler& sampler,
                        DescriptorAllocator& samplerHeap);

// ─────────────────────────────────────────────────────────────────────────────
// Root Signature
// ─────────────────────────────────────────────────────────────────────────────

/// Function pointer type for D3D12SerializeVersionedRootSignature.
///
/// When the Agility SDK is loaded via CreateDeviceFactory, the system d3d12.dll's
/// serialize function does not support SM6.6 flags. The caller can provide the
/// Agility SDK's version from D3D12Core.dll instead.
using PFN_SerializeVersionedRootSignature = HRESULT(WINAPI*)(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*,
    ID3DBlob**, ID3DBlob**);

/// Creates the global root signature, adapting to the negotiated shader model.
///
/// - SM6.6+: Uses `DIRECTLY_INDEXED` flags for bindless `ResourceDescriptorHeap[]` access.
/// - SM < 6.6: Standard root signature without bindless flags.
///
/// Layout: one root constant (`uint descriptorIndex` at register `b0`, space 0).
///
/// # Arguments
/// * `device` — D3D12 device to create the root signature on.
/// * `sm` — Negotiated shader model (determines root signature flags).
/// * `serializeFn` — Optional function pointer to D3D12SerializeVersionedRootSignature
///   from D3D12Core.dll. When `nullptr`, falls back to the system d3d12.dll version.
///
/// # Returns
/// A valid `ID3D12RootSignature*` on success (caller owns the COM reference),
/// or `nullptr` on failure.
[[nodiscard]] ID3D12RootSignature* CreateBindlessRootSignature(
    ID3D12Device* device, ShaderModel sm,
    PFN_SerializeVersionedRootSignature serializeFn = nullptr);

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a DX12 graphics pipeline state object (PSO).
///
/// Resolves shader bytecodes from handles via the shader pool, then creates
/// a `D3D12_GRAPHICS_PIPELINE_STATE_DESC` with no input layout (SM6.6 bindless
/// pulls vertex data from SRV buffers in the shader).
///
/// # Arguments
/// * `device` — D3D12 device for PSO creation.
/// * `rootSignature` — Global bindless root signature.
/// * `desc` — RHI pipeline descriptor (shaders, rasterizer, blend, depth state).
/// * `shaderPool` — Pool for resolving shader handles to bytecode.
/// * `outPipeline` — Output struct filled on success.
///
/// # Returns
/// `true` on success, `false` on failure (invalid shader handles, PSO creation error).
bool CreateDX12GraphicsPipeline(ID3D12Device* device,
                                 ID3D12RootSignature* rootSignature,
                                 const RHIGraphicsPipelineDesc& desc,
                                 const ResourcePool<DX12Shader>& shaderPool,
                                 DX12Pipeline& outPipeline);

/// Creates a DX12 compute pipeline state object (PSO).
///
/// Resolves the compute shader bytecode from its handle via the shader pool,
/// then creates a `D3D12_COMPUTE_PIPELINE_STATE_DESC` with the global root signature.
///
/// # Arguments
/// * `device` — D3D12 device for PSO creation.
/// * `rootSignature` — Global bindless root signature.
/// * `desc` — RHI compute pipeline descriptor (shader handle, debug name, cached blob).
/// * `shaderPool` — Pool for resolving shader handles to bytecode.
/// * `outPipeline` — Output struct filled on success.
///
/// # Returns
/// `true` on success, `false` on failure (invalid shader handle, wrong stage, PSO creation error).
bool CreateDX12ComputePipeline(ID3D12Device* device,
                                ID3D12RootSignature* rootSignature,
                                const RHIComputePipelineDesc& desc,
                                const ResourcePool<DX12Shader>& shaderPool,
                                DX12Pipeline& outPipeline);

/// Releases a pipeline state object's COM reference.
void DestroyDX12Pipeline(DX12Pipeline& pipeline);

} // namespace uz
