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

/// Converts RHIFormat to DXGI_FORMAT.
DXGI_FORMAT ToDxgiFormat(RHIFormat format);

/// Returns true if the format is a depth/stencil format.
bool IsDepthFormat(RHIFormat format);

/// Returns the typeless equivalent of a depth format (for SRV creation).
DXGI_FORMAT ToTypelessDepthFormat(RHIFormat format);

/// Returns the SRV-compatible format for a depth format.
DXGI_FORMAT ToDepthSrvFormat(RHIFormat format);

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a D3D12MA buffer allocation and optional SRV/UAV descriptors.
/// Returns true on success, fills the DX12Buffer struct.
bool CreateDX12Buffer(ID3D12Device* device,
                      D3D12MA::Allocator* allocator,
                      DescriptorAllocator& srvUavHeap,
                      const RHIBufferDesc& desc,
                      DX12Buffer& outBuffer);

/// Releases a D3D12MA buffer allocation and its descriptors.
void DestroyDX12Buffer(DX12Buffer& buffer,
                       DescriptorAllocator& srvUavHeap);

// ─────────────────────────────────────────────────────────────────────────────
// Texture Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a D3D12MA texture allocation with SRV and optional RTV/DSV descriptors.
/// Returns true on success, fills the DX12Texture struct.
bool CreateDX12Texture(ID3D12Device* device,
                       D3D12MA::Allocator* allocator,
                       DescriptorAllocator& srvUavHeap,
                       DescriptorAllocator& rtvHeap,
                       DescriptorAllocator& dsvHeap,
                       const RHITextureDesc& desc,
                       DX12Texture& outTexture);

/// Releases a D3D12MA texture allocation and its descriptors.
void DestroyDX12Texture(DX12Texture& texture,
                        DescriptorAllocator& srvUavHeap,
                        DescriptorAllocator& rtvHeap,
                        DescriptorAllocator& dsvHeap);

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a sampler descriptor in the global sampler heap.
bool CreateDX12Sampler(ID3D12Device* device,
                       DescriptorAllocator& samplerHeap,
                       const RHISamplerDesc& desc,
                       DX12Sampler& outSampler);

/// Frees a sampler descriptor index.
void DestroyDX12Sampler(DX12Sampler& sampler,
                        DescriptorAllocator& samplerHeap);

// ─────────────────────────────────────────────────────────────────────────────
// Root Signature
// ─────────────────────────────────────────────────────────────────────────────

/// Creates the global root signature, adapting to the negotiated shader model.
///
/// SM6.6: DIRECTLY_INDEXED flags for bindless heap access.
/// SM < 6.6: Standard root signature without bindless flags.
/// Layout: one root constant (uint descriptorIndex at b0).
ID3D12RootSignature* CreateBindlessRootSignature(ID3D12Device* device, ShaderModel sm);

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a DX12 graphics pipeline state object.
/// Requires a pre-created root signature and resolved shader bytecodes.
bool CreateDX12GraphicsPipeline(ID3D12Device* device,
                                 ID3D12RootSignature* rootSignature,
                                 const RHIGraphicsPipelineDesc& desc,
                                 const ResourcePool<DX12Shader>& shaderPool,
                                 DX12Pipeline& outPipeline);

/// Releases a DX12 pipeline state object.
void DestroyDX12Pipeline(DX12Pipeline& pipeline);

} // namespace uz
