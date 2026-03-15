/// UzEngine — DX12 Texture Creation
///
/// Format conversion utilities and texture resource creation/destruction.
/// Creates D3D12MA textures with SRV, RTV, DSV, and UAV descriptors as needed.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_resource_creation.h"
#include "dx12_guids.h"
#include "D3D12MemAlloc.h"
#include "d3dx12.h"
#include "log.h"
#include "profiler.h"

#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Format Conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Converts RHIFormat enum to DXGI_FORMAT for DX12 API calls.
///
/// Maps: R/RG/RGB/RGBA channels × unsigned/signed/float × 8/16/32-bit
/// Also handles special formats like Depth24Stencil8, R11G11B10Float.
///
/// # Panics
/// Returns DXGI_FORMAT_UNKNOWN for unmapped formats (should never happen if spec is complete).
DXGI_FORMAT ToDxgiFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::R8Unorm:        return DXGI_FORMAT_R8_UNORM;
        case RHIFormat::RG8Unorm:       return DXGI_FORMAT_R8G8_UNORM;
        case RHIFormat::RGBA8Unorm:     return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::RGBA8Srgb:      return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHIFormat::BGRA8Unorm:     return DXGI_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::BGRA8Srgb:      return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case RHIFormat::R16Float:       return DXGI_FORMAT_R16_FLOAT;
        case RHIFormat::RG16Float:      return DXGI_FORMAT_R16G16_FLOAT;
        case RHIFormat::RGBA16Float:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case RHIFormat::R32Float:       return DXGI_FORMAT_R32_FLOAT;
        case RHIFormat::RG32Float:      return DXGI_FORMAT_R32G32_FLOAT;
        case RHIFormat::RGB32Float:     return DXGI_FORMAT_R32G32B32_FLOAT;
        case RHIFormat::RGBA32Float:    return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHIFormat::R32Uint:        return DXGI_FORMAT_R32_UINT;
        case RHIFormat::RG32Uint:       return DXGI_FORMAT_R32G32_UINT;
        case RHIFormat::RGBA32Uint:     return DXGI_FORMAT_R32G32B32A32_UINT;
        case RHIFormat::D16Unorm:       return DXGI_FORMAT_D16_UNORM;
        case RHIFormat::D32Float:       return DXGI_FORMAT_D32_FLOAT;
        case RHIFormat::D24UnormS8Uint: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case RHIFormat::D32FloatS8Uint: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        default:                        return DXGI_FORMAT_UNKNOWN;
    }
}

/// Returns true if the format is a depth or depth-stencil format.
bool IsDepthFormat(RHIFormat format) {
    return format == RHIFormat::D16Unorm
        || format == RHIFormat::D32Float
        || format == RHIFormat::D24UnormS8Uint
        || format == RHIFormat::D32FloatS8Uint;
}

/// Returns the typeless equivalent for SRV+DSV combo (e.g. D32Float → R32_TYPELESS).
DXGI_FORMAT ToTypelessDepthFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::D16Unorm:       return DXGI_FORMAT_R16_TYPELESS;
        case RHIFormat::D32Float:       return DXGI_FORMAT_R32_TYPELESS;
        case RHIFormat::D24UnormS8Uint: return DXGI_FORMAT_R24G8_TYPELESS;
        case RHIFormat::D32FloatS8Uint: return DXGI_FORMAT_R32G8X24_TYPELESS;
        default:                        return DXGI_FORMAT_UNKNOWN;
    }
}

/// Returns the SRV-readable format for a depth texture (e.g. D32Float → R32_FLOAT).
DXGI_FORMAT ToDepthSrvFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::D16Unorm:       return DXGI_FORMAT_R16_UNORM;
        case RHIFormat::D32Float:       return DXGI_FORMAT_R32_FLOAT;
        case RHIFormat::D24UnormS8Uint: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case RHIFormat::D32FloatS8Uint: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        default:                        return DXGI_FORMAT_UNKNOWN;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a D3D12MA texture with SRV and optional RTV/DSV/UAV descriptors.
bool CreateDX12Texture(ID3D12Device* device,
                       D3D12MA::Allocator* allocator,
                       DescriptorAllocator& srvUavHeap,
                       DescriptorAllocator& rtvHeap,
                       DescriptorAllocator& dsvHeap,
                       const RHITextureDesc& desc,
                       DX12Texture& outTexture) {
    UZ_PROFILE_FUNCTION();
    bool isDepth = IsDepthFormat(desc.format);
    bool isRT = HasFlag(desc.usage, RHITextureUsage::RenderTarget);
    bool isDS = HasFlag(desc.usage, RHITextureUsage::DepthStencil);
    bool hasSRV = HasFlag(desc.usage, RHITextureUsage::ShaderResource);
    bool hasUAV = HasFlag(desc.usage, RHITextureUsage::UnorderedAccess);

    // Determine dimension
    D3D12_RESOURCE_DIMENSION dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    if (desc.depthOrArraySize > 1 && desc.height > 1) {
        // Could be 3D or array — arrays use TEXTURE2D with ArraySize > 1
        // True 3D textures would need a separate flag; for now treat as 2D array
    }

    // Use typeless format for depth textures that also need SRV
    DXGI_FORMAT resourceFormat = ToDxgiFormat(desc.format);
    if (isDepth && hasSRV) {
        resourceFormat = ToTypelessDepthFormat(desc.format);
    }

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = dimension;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = desc.depthOrArraySize;
    resourceDesc.MipLevels = desc.mipLevels;
    resourceDesc.Format = resourceFormat;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (isRT) resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (isDS) resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (hasUAV) resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // If depth-only (no SRV), deny shader resource
    if (isDS && !hasSRV) {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }

    // Optimized clear value for RT/DS
    D3D12_CLEAR_VALUE clearValue = {};
    D3D12_CLEAR_VALUE* clearPtr = nullptr;
    if (isRT) {
        clearValue.Format = ToDxgiFormat(desc.format);
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 1.0f;
        clearPtr = &clearValue;
    } else if (isDS) {
        clearValue.Format = ToDxgiFormat(desc.format);
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;
        clearPtr = &clearValue;
    }

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    // Initial state
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    if (isDS) initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    D3D12MA::Allocation* allocation = nullptr;
    ID3D12Resource* resource = nullptr;
    HRESULT hr = allocator->CreateResource(
        &allocDesc, &resourceDesc, initialState,
        clearPtr, &allocation, IID_PPV_ARGS(&resource));

    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateTexture: D3D12MA failed, hr=0x{:08X}, {}x{} fmt={} (name='{}')",
               static_cast<uint32_t>(hr), desc.width, desc.height,
               static_cast<int>(desc.format), desc.debugName ? desc.debugName : "");
        return false;
    }

    // Set debug name
    if (desc.debugName) {
        wchar_t wideName[256] = {};
        MultiByteToWideChar(CP_UTF8, 0, desc.debugName, -1, wideName, 256);
        resource->SetName(wideName);
    }

    outTexture.allocation = allocation;
    outTexture.resource = resource;
    outTexture.desc = desc;

    // Create SRV (bindless) if shader resource usage
    if (hasSRV) {
        uint32_t srvIdx = srvUavHeap.Allocate();
        if (srvIdx != DescriptorAllocator::kInvalidIndex) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

            if (isDepth) {
                srvDesc.Format = ToDepthSrvFormat(desc.format);
            } else {
                srvDesc.Format = ToDxgiFormat(desc.format);
            }

            if (desc.depthOrArraySize == 6) {
                // Cube map
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                srvDesc.TextureCube.MipLevels = desc.mipLevels;
            } else if (desc.depthOrArraySize > 1) {
                // 2D array
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                srvDesc.Texture2DArray.MipLevels = desc.mipLevels;
                srvDesc.Texture2DArray.ArraySize = desc.depthOrArraySize;
            } else {
                // Standard 2D
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = desc.mipLevels;
            }

            device->CreateShaderResourceView(resource, &srvDesc, srvUavHeap.CpuHandle(srvIdx));
            outTexture.srvIndex = srvIdx;
        }
    }

    // Create UAV if requested
    if (hasUAV) {
        uint32_t uavIdx = srvUavHeap.Allocate();
        if (uavIdx != DescriptorAllocator::kInvalidIndex) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = ToDxgiFormat(desc.format);

            if (desc.depthOrArraySize > 1) {
                // Array texture or cubemap — HLSL RWTexture2DArray requires TEXTURE2DARRAY
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                uavDesc.Texture2DArray.MipSlice = 0;
                uavDesc.Texture2DArray.FirstArraySlice = 0;
                uavDesc.Texture2DArray.ArraySize = desc.depthOrArraySize;
                uavDesc.Texture2DArray.PlaneSlice = 0;
            } else {
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                uavDesc.Texture2D.MipSlice = 0;
            }

            device->CreateUnorderedAccessView(resource, nullptr, &uavDesc,
                                               srvUavHeap.CpuHandle(uavIdx));
            outTexture.uavIndex = uavIdx;
        }
    }

    // Create RTV for render targets
    if (isRT) {
        uint32_t rtvIdx = rtvHeap.Allocate();
        if (rtvIdx != DescriptorAllocator::kInvalidIndex) {
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = ToDxgiFormat(desc.format);
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            device->CreateRenderTargetView(resource, &rtvDesc, rtvHeap.CpuHandle(rtvIdx));
            outTexture.rtvIndex = rtvIdx;
        }
    }

    // Create DSV for depth stencils
    if (isDS) {
        uint32_t dsvIdx = dsvHeap.Allocate();
        if (dsvIdx != DescriptorAllocator::kInvalidIndex) {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Format = ToDxgiFormat(desc.format);
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device->CreateDepthStencilView(resource, &dsvDesc, dsvHeap.CpuHandle(dsvIdx));
            outTexture.dsvIndex = dsvIdx;
        }
    }

    UZ_LOG(RHI, Debug, "CreateTexture: {}x{} fmt={}, srv={}, rtv={}, dsv={} (name='{}')",
           desc.width, desc.height, static_cast<int>(desc.format),
           outTexture.srvIndex, outTexture.rtvIndex, outTexture.dsvIndex,
           desc.debugName ? desc.debugName : "");

    return true;
}

/// Releases a texture's D3D12MA allocation and frees all descriptor indices.
void DestroyDX12Texture(DX12Texture& texture,
                        DescriptorAllocator& srvUavHeap,
                        DescriptorAllocator& rtvHeap,
                        DescriptorAllocator& dsvHeap) {
    UZ_PROFILE_FUNCTION();
    if (texture.srvIndex != UINT32_MAX) {
        srvUavHeap.Free(texture.srvIndex);
        texture.srvIndex = UINT32_MAX;
    }
    if (texture.uavIndex != UINT32_MAX) {
        srvUavHeap.Free(texture.uavIndex);
        texture.uavIndex = UINT32_MAX;
    }
    if (texture.rtvIndex != UINT32_MAX) {
        rtvHeap.Free(texture.rtvIndex);
        texture.rtvIndex = UINT32_MAX;
    }
    if (texture.dsvIndex != UINT32_MAX) {
        dsvHeap.Free(texture.dsvIndex);
        texture.dsvIndex = UINT32_MAX;
    }

    if (texture.resource) {
        texture.resource->Release();
        texture.resource = nullptr;
    }
    if (texture.allocation) {
        texture.allocation->Release();
        texture.allocation = nullptr;
    }
}

} // namespace uz
