/// DX12 Resource Creation Implementation
///
/// Format conversion, buffer creation, texture creation, and sampler descriptors.
/// Uses D3D12MemoryAllocator (D3D12MA) for GPU memory management.
/// Auto-creates SRV/UAV descriptors for bindless access and RTVs/DSVs for rendering.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_resource_creation.h"
#include "dx12_guids.h"
#include "D3D12MemAlloc.h"
#include "d3dx12.h"
#include "log.h"

#include <vector>

namespace uz {

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
// D3D12MA Heap Type Mapping
// ─────────────────────────────────────────────────────────────────────────────

/// Converts RHIMemoryType to D3D12_HEAP_TYPE.
static D3D12_HEAP_TYPE ToD3D12HeapType(RHIMemoryType memory) {
    switch (memory) {
        case RHIMemoryType::Upload:   return D3D12_HEAP_TYPE_UPLOAD;
        case RHIMemoryType::Readback: return D3D12_HEAP_TYPE_READBACK;
        default:                      return D3D12_HEAP_TYPE_DEFAULT;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a D3D12MA buffer with optional SRV/UAV descriptors and CPU mapping.
bool CreateDX12Buffer(ID3D12Device* device,
                      D3D12MA::Allocator* allocator,
                      DescriptorAllocator& srvUavHeap,
                      const RHIBufferDesc& desc,
                      DX12Buffer& outBuffer) {
    if (desc.sizeBytes == 0) {
        UZ_LOG(RHI, Error, "CreateBuffer: sizeBytes is 0 (name='{}')",
               desc.debugName ? desc.debugName : "");
        return false;
    }

    // Constant buffers must be 256-byte aligned
    uint64_t allocSize = desc.sizeBytes;
    if (HasFlag(desc.usage, RHIBufferUsage::ConstantBuffer)) {
        allocSize = (allocSize + 255) & ~255ULL;
    }

    // Resource desc
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = allocSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (HasFlag(desc.usage, RHIBufferUsage::UnorderedAccess)) {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    // D3D12MA allocation
    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = ToD3D12HeapType(desc.memory);

    // Initial state depends on heap type
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    if (desc.memory == RHIMemoryType::Upload) {
        initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
    } else if (desc.memory == RHIMemoryType::Readback) {
        initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    D3D12MA::Allocation* allocation = nullptr;
    ID3D12Resource* resource = nullptr;
    HRESULT hr = allocator->CreateResource(
        &allocDesc, &resourceDesc, initialState,
        nullptr, &allocation, IID_PPV_ARGS(&resource));

    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateBuffer: D3D12MA failed, hr=0x{:08X}, size={} (name='{}')",
               static_cast<uint32_t>(hr), allocSize, desc.debugName ? desc.debugName : "");
        return false;
    }

    // Set debug name
    if (desc.debugName) {
        wchar_t wideName[256] = {};
        MultiByteToWideChar(CP_UTF8, 0, desc.debugName, -1, wideName, 256);
        resource->SetName(wideName);
    }

    outBuffer.allocation = allocation;
    outBuffer.resource = resource;
    outBuffer.desc = desc;

    // Persistent CPU mapping for Upload/Readback buffers
    if (desc.memory == RHIMemoryType::Upload || desc.memory == RHIMemoryType::Readback) {
        void* mapped = nullptr;
        D3D12_RANGE readRange = {};
        if (desc.memory == RHIMemoryType::Upload) {
            readRange = {0, 0}; // CPU won't read from upload buffers
        }
        hr = resource->Map(0, &readRange, &mapped);
        if (SUCCEEDED(hr)) {
            outBuffer.mappedPtr = mapped;
        } else {
            UZ_LOG(RHI, Warning, "CreateBuffer: Map failed for upload/readback buffer, hr=0x{:08X}",
                   static_cast<uint32_t>(hr));
        }
    }

    // Create SRV for structured/typed buffers (bindless)
    if (HasFlag(desc.usage, RHIBufferUsage::StructuredBuffer) && desc.stride > 0) {
        uint32_t srvIdx = srvUavHeap.Allocate();
        if (srvIdx != DescriptorAllocator::kInvalidIndex) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.NumElements = static_cast<UINT>(desc.sizeBytes / desc.stride);
            srvDesc.Buffer.StructureByteStride = desc.stride;
            device->CreateShaderResourceView(resource, &srvDesc, srvUavHeap.CpuHandle(srvIdx));
            outBuffer.srvIndex = srvIdx;
        }
    }

    // Create UAV if requested
    if (HasFlag(desc.usage, RHIBufferUsage::UnorderedAccess)) {
        uint32_t uavIdx = srvUavHeap.Allocate();
        if (uavIdx != DescriptorAllocator::kInvalidIndex) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            if (desc.stride > 0) {
                uavDesc.Buffer.NumElements = static_cast<UINT>(desc.sizeBytes / desc.stride);
                uavDesc.Buffer.StructureByteStride = desc.stride;
            } else {
                uavDesc.Buffer.NumElements = static_cast<UINT>(desc.sizeBytes / 4);
            }
            device->CreateUnorderedAccessView(resource, nullptr, &uavDesc,
                                               srvUavHeap.CpuHandle(uavIdx));
            outBuffer.uavIndex = uavIdx;
        }
    }

    UZ_LOG(RHI, Debug, "CreateBuffer: size={}, memory={}, srv={}, uav={} (name='{}')",
           allocSize, static_cast<int>(desc.memory),
           outBuffer.srvIndex, outBuffer.uavIndex,
           desc.debugName ? desc.debugName : "");

    return true;
}

/// Releases a buffer's D3D12MA allocation and frees its SRV/UAV descriptor indices.
void DestroyDX12Buffer(DX12Buffer& buffer, DescriptorAllocator& srvUavHeap) {
    // Unmap persistent mapping
    if (buffer.mappedPtr && buffer.resource) {
        buffer.resource->Unmap(0, nullptr);
        buffer.mappedPtr = nullptr;
    }

    // Free descriptor indices
    if (buffer.srvIndex != UINT32_MAX) {
        srvUavHeap.Free(buffer.srvIndex);
        buffer.srvIndex = UINT32_MAX;
    }
    if (buffer.uavIndex != UINT32_MAX) {
        srvUavHeap.Free(buffer.uavIndex);
        buffer.uavIndex = UINT32_MAX;
    }

    // Release resource (must release before allocation)
    if (buffer.resource) {
        buffer.resource->Release();
        buffer.resource = nullptr;
    }

    // Release D3D12MA allocation
    if (buffer.allocation) {
        buffer.allocation->Release();
        buffer.allocation = nullptr;
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
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = 0;
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

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Converts RHIFilterMode to D3D12_FILTER.
static D3D12_FILTER ToD3D12Filter(RHIFilterMode filter) {
    switch (filter) {
        case RHIFilterMode::Point:       return D3D12_FILTER_MIN_MAG_MIP_POINT;
        case RHIFilterMode::Linear:      return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        case RHIFilterMode::Anisotropic: return D3D12_FILTER_ANISOTROPIC;
        default:                         return D3D12_FILTER_MIN_MAG_MIP_POINT;
    }
}

/// Converts RHIAddressMode to D3D12_TEXTURE_ADDRESS_MODE.
static D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(RHIAddressMode mode) {
    switch (mode) {
        case RHIAddressMode::Wrap:   return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case RHIAddressMode::Clamp:  return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case RHIAddressMode::Mirror: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case RHIAddressMode::Border: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        default:                     return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
}

/// Converts RHICompareFunc to D3D12_COMPARISON_FUNC (sampler section).
static D3D12_COMPARISON_FUNC ToD3D12CompareFunc(RHICompareFunc func) {
    switch (func) {
        case RHICompareFunc::Never:        return D3D12_COMPARISON_FUNC_NEVER;
        case RHICompareFunc::Less:         return D3D12_COMPARISON_FUNC_LESS;
        case RHICompareFunc::Equal:        return D3D12_COMPARISON_FUNC_EQUAL;
        case RHICompareFunc::LessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case RHICompareFunc::Greater:      return D3D12_COMPARISON_FUNC_GREATER;
        case RHICompareFunc::NotEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case RHICompareFunc::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case RHICompareFunc::Always:       return D3D12_COMPARISON_FUNC_ALWAYS;
        default:                           return D3D12_COMPARISON_FUNC_NEVER;
    }
}

/// Creates a sampler descriptor in the global sampler heap.
bool CreateDX12Sampler(ID3D12Device* device,
                       DescriptorAllocator& samplerHeap,
                       const RHISamplerDesc& desc,
                       DX12Sampler& outSampler) {
    uint32_t idx = samplerHeap.Allocate();
    if (idx == DescriptorAllocator::kInvalidIndex) {
        UZ_LOG(RHI, Error, "CreateSampler: sampler heap full (name='{}')",
               desc.debugName ? desc.debugName : "");
        return false;
    }

    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = ToD3D12Filter(desc.filter);
    samplerDesc.AddressU = ToD3D12AddressMode(desc.addressU);
    samplerDesc.AddressV = ToD3D12AddressMode(desc.addressV);
    samplerDesc.AddressW = ToD3D12AddressMode(desc.addressW);
    samplerDesc.MipLODBias = desc.mipLodBias;
    samplerDesc.MaxAnisotropy = desc.maxAnisotropy;
    samplerDesc.ComparisonFunc = ToD3D12CompareFunc(desc.compareFunc);
    samplerDesc.MinLOD = desc.minLod;
    samplerDesc.MaxLOD = desc.maxLod;
    // BorderColor defaults to {0,0,0,0}

    device->CreateSampler(&samplerDesc, samplerHeap.CpuHandle(idx));
    outSampler.samplerIndex = idx;

    UZ_LOG(RHI, Debug, "CreateSampler: idx={}, filter={} (name='{}')",
           idx, static_cast<int>(desc.filter), desc.debugName ? desc.debugName : "");

    return true;
}

/// Frees a sampler's descriptor index in the global sampler heap.
void DestroyDX12Sampler(DX12Sampler& sampler, DescriptorAllocator& samplerHeap) {
    if (sampler.samplerIndex != UINT32_MAX) {
        samplerHeap.Free(sampler.samplerIndex);
        sampler.samplerIndex = UINT32_MAX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Root Signature — Global (adapts to shader model)
// ─────────────────────────────────────────────────────────────────────────────

/// Creates the global root signature, adapting layout to the negotiated shader model.
///
/// SM6.6+: uses DIRECTLY_INDEXED flags for bindless heap access. Root layout:
///   [0] 4x uint32 root constants at b0 (descriptor indices)
///   [1] Root CBV at b1 (per-frame data)
///   [2] Root CBV at b2 (per-object data)
///
/// SM5.x: uses Input Assembler + descriptor table for SRV/texture binding. Root layout:
///   [0] 4x uint32 root constants at b0
///   [1] Root CBV at b1 (per-frame data)
///   [2] Root CBV at b2 (per-object data)
///   [3] SRV descriptor table at t0, space0, unbounded (textures + structured buffers)
ID3D12RootSignature* CreateBindlessRootSignature(ID3D12Device* device, ShaderModel sm) {
    const bool bindless = (sm >= ShaderModel::SM6_6);

    // SM6.6: 3 root params. SM5: 4 root params (extra SRV table).
    D3D12_ROOT_PARAMETER1 rootParams[4] = {};
    uint32_t numParams = 3;

    // Root parameter 0: root constants (uint32 descriptorIndex) at b0
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;   // b0
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 4;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root parameter 1: root CBV for per-frame data at b1
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 1;  // b1 — per-frame
    rootParams[1].Descriptor.RegisterSpace = 0;
    rootParams[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root parameter 2: root CBV for per-object data at b2
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[2].Descriptor.ShaderRegister = 2;  // b2 — per-object
    rootParams[2].Descriptor.RegisterSpace = 0;
    rootParams[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // SM5 only: Root parameter 3 — SRV descriptor table for textures/structured buffers
    D3D12_DESCRIPTOR_RANGE1 srvRange = {};
    if (!bindless) {
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = UINT_MAX; // unbounded
        srvRange.BaseShaderRegister = 0;    // t0
        srvRange.RegisterSpace = 0;
        srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[3].DescriptorTable.pDescriptorRanges = &srvRange;
        rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        numParams = 4;
    }

    D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    if (bindless) {
        flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    } else {
        // SM5: allow Input Assembler vertex/index buffer binding
        flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    }

    // Static sampler s0: linear wrap (used by mesh_ps and any textured shader)
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].MipLODBias = 0.f;
    staticSamplers[0].MaxAnisotropy = 1;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    staticSamplers[0].MinLOD = 0.f;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;  // s0
    staticSamplers[0].RegisterSpace = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = {};
    versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versionedDesc.Desc_1_1.NumParameters = numParams;
    versionedDesc.Desc_1_1.pParameters = rootParams;
    versionedDesc.Desc_1_1.NumStaticSamplers = 1;
    versionedDesc.Desc_1_1.pStaticSamplers = staticSamplers;
    versionedDesc.Desc_1_1.Flags = flags;

    ID3DBlob* serialized = nullptr;
    ID3DBlob* error = nullptr;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&versionedDesc, &serialized, &error);
    if (FAILED(hr)) {
        const char* errMsg = error ? static_cast<const char*>(error->GetBufferPointer()) : "unknown";
        UZ_LOG(RHI, Error, "Failed to serialize root signature: {}", errMsg);
        if (error) error->Release();
        if (serialized) serialized->Release();
        return nullptr;
    }

    ID3D12RootSignature* rootSig = nullptr;
    hr = device->CreateRootSignature(0,
        serialized->GetBufferPointer(), serialized->GetBufferSize(),
        IID_ID3D12RootSignature, reinterpret_cast<void**>(&rootSig));

    serialized->Release();
    if (error) error->Release();

    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to create root signature, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return nullptr;
    }

    const char* mode = (sm >= ShaderModel::SM6_6) ? "SM6.6 DIRECTLY_INDEXED" : "standard";
    UZ_LOG(RHI, Info, "Root signature created ({})", mode);
    return rootSig;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline State Object Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Converts RHICullMode to D3D12_CULL_MODE.
static D3D12_CULL_MODE ToDx12CullMode(RHICullMode mode) {
    switch (mode) {
        case RHICullMode::None:  return D3D12_CULL_MODE_NONE;
        case RHICullMode::Front: return D3D12_CULL_MODE_FRONT;
        case RHICullMode::Back:  return D3D12_CULL_MODE_BACK;
    }
    return D3D12_CULL_MODE_BACK;
}

/// Converts RHIFillMode to D3D12_FILL_MODE.
static D3D12_FILL_MODE ToDx12FillMode(RHIFillMode mode) {
    switch (mode) {
        case RHIFillMode::Solid:     return D3D12_FILL_MODE_SOLID;
        case RHIFillMode::Wireframe: return D3D12_FILL_MODE_WIREFRAME;
    }
    return D3D12_FILL_MODE_SOLID;
}

/// Converts RHICompareFunc to D3D12_COMPARISON_FUNC.
static D3D12_COMPARISON_FUNC ToDx12CompareFunc(RHICompareFunc func) {
    switch (func) {
        case RHICompareFunc::Never:        return D3D12_COMPARISON_FUNC_NEVER;
        case RHICompareFunc::Less:         return D3D12_COMPARISON_FUNC_LESS;
        case RHICompareFunc::Equal:        return D3D12_COMPARISON_FUNC_EQUAL;
        case RHICompareFunc::LessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case RHICompareFunc::Greater:      return D3D12_COMPARISON_FUNC_GREATER;
        case RHICompareFunc::NotEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case RHICompareFunc::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case RHICompareFunc::Always:       return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_LESS;
}

/// Converts RHIBlendFactor to D3D12_BLEND.
static D3D12_BLEND ToDx12Blend(RHIBlendFactor factor) {
    switch (factor) {
        case RHIBlendFactor::Zero:         return D3D12_BLEND_ZERO;
        case RHIBlendFactor::One:          return D3D12_BLEND_ONE;
        case RHIBlendFactor::SrcColor:     return D3D12_BLEND_SRC_COLOR;
        case RHIBlendFactor::InvSrcColor:  return D3D12_BLEND_INV_SRC_COLOR;
        case RHIBlendFactor::SrcAlpha:     return D3D12_BLEND_SRC_ALPHA;
        case RHIBlendFactor::InvSrcAlpha:  return D3D12_BLEND_INV_SRC_ALPHA;
        case RHIBlendFactor::DstColor:     return D3D12_BLEND_DEST_COLOR;
        case RHIBlendFactor::InvDstColor:  return D3D12_BLEND_INV_DEST_COLOR;
        case RHIBlendFactor::DstAlpha:     return D3D12_BLEND_DEST_ALPHA;
        case RHIBlendFactor::InvDstAlpha:  return D3D12_BLEND_INV_DEST_ALPHA;
    }
    return D3D12_BLEND_ONE;
}

/// Converts RHIBlendOp to D3D12_BLEND_OP.
static D3D12_BLEND_OP ToDx12BlendOp(RHIBlendOp op) {
    switch (op) {
        case RHIBlendOp::Add:         return D3D12_BLEND_OP_ADD;
        case RHIBlendOp::Subtract:    return D3D12_BLEND_OP_SUBTRACT;
        case RHIBlendOp::RevSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
        case RHIBlendOp::Min:         return D3D12_BLEND_OP_MIN;
        case RHIBlendOp::Max:         return D3D12_BLEND_OP_MAX;
    }
    return D3D12_BLEND_OP_ADD;
}

/// Converts RHIPrimitiveTopology to D3D12_PRIMITIVE_TOPOLOGY_TYPE.
static D3D12_PRIMITIVE_TOPOLOGY_TYPE ToDx12TopologyType(RHIPrimitiveTopology topo) {
    switch (topo) {
        case RHIPrimitiveTopology::TriangleList:
        case RHIPrimitiveTopology::TriangleStrip:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        case RHIPrimitiveTopology::LineList:
        case RHIPrimitiveTopology::LineStrip:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case RHIPrimitiveTopology::PointList:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    }
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}

/// Creates a graphics PSO from RHIGraphicsPipelineDesc, resolving shader handles from the pool.
bool CreateDX12GraphicsPipeline(ID3D12Device* device,
                                 ID3D12RootSignature* rootSignature,
                                 const RHIGraphicsPipelineDesc& desc,
                                 const ResourcePool<DX12Shader>& shaderPool,
                                 DX12Pipeline& outPipeline) {
    // Resolve shader bytecodes
    const DX12Shader* vs = shaderPool.Get(desc.vertexShader.index, desc.vertexShader.generation);
    if (!vs || vs->bytecode.empty()) {
        UZ_LOG(RHI, Error, "CreateGraphicsPipeline: invalid vertex shader (name='{}')",
               desc.debugName ? desc.debugName : "");
        return false;
    }

    const DX12Shader* ps = nullptr;
    if (desc.pixelShader.index != UINT32_MAX && desc.pixelShader.generation != 0) {
        ps = shaderPool.Get(desc.pixelShader.index, desc.pixelShader.generation);
        if (!ps || ps->bytecode.empty()) {
            UZ_LOG(RHI, Error, "CreateGraphicsPipeline: invalid pixel shader (name='{}')",
                   desc.debugName ? desc.debugName : "");
            return false;
        }
    }

    // Build D3D12 PSO descriptor
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;

    // Shaders
    psoDesc.VS.pShaderBytecode = vs->bytecode.data();
    psoDesc.VS.BytecodeLength = vs->bytecode.size();
    if (ps) {
        psoDesc.PS.pShaderBytecode = ps->bytecode.data();
        psoDesc.PS.BytecodeLength = ps->bytecode.size();
    }

    // Input layout: SM5 uses Input Assembler, SM6.6 uses bindless SRV fetch
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
    if (desc.inputElementCount > 0 && desc.inputElements != nullptr) {
        inputElements.reserve(desc.inputElementCount);
        for (uint32_t i = 0; i < desc.inputElementCount; ++i) {
            const auto& elem = desc.inputElements[i];
            D3D12_INPUT_ELEMENT_DESC d3dElem = {};
            d3dElem.SemanticName = InputSemanticToString(elem.semantic);
            d3dElem.SemanticIndex = elem.semanticIndex;
            d3dElem.Format = ToDxgiFormat(elem.format);
            d3dElem.InputSlot = elem.inputSlot;
            d3dElem.AlignedByteOffset = elem.byteOffset;
            d3dElem.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            d3dElem.InstanceDataStepRate = 0;
            inputElements.push_back(d3dElem);
        }
        psoDesc.InputLayout.pInputElementDescs = inputElements.data();
        psoDesc.InputLayout.NumElements = static_cast<UINT>(inputElements.size());
    } else {
        psoDesc.InputLayout.pInputElementDescs = nullptr;
        psoDesc.InputLayout.NumElements = 0;
    }

    // Rasterizer
    psoDesc.RasterizerState.FillMode = ToDx12FillMode(desc.rasterizer.fillMode);
    psoDesc.RasterizerState.CullMode = ToDx12CullMode(desc.rasterizer.cullMode);
    psoDesc.RasterizerState.FrontCounterClockwise = desc.rasterizer.frontCounterClockwise;
    psoDesc.RasterizerState.DepthBias = static_cast<INT>(desc.rasterizer.depthBias);
    psoDesc.RasterizerState.SlopeScaledDepthBias = desc.rasterizer.slopeScaledDepthBias;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Depth-stencil
    psoDesc.DepthStencilState.DepthEnable = desc.depthStencil.depthEnable;
    psoDesc.DepthStencilState.DepthWriteMask = desc.depthStencil.depthWrite
        ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = ToDx12CompareFunc(desc.depthStencil.depthFunc);
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.DSVFormat = ToDxgiFormat(desc.depthStencilFormat);

    // Blend state
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = (desc.renderTargetCount > 1) ? TRUE : FALSE;
    for (uint32_t i = 0; i < desc.renderTargetCount && i < 8; ++i) {
        auto& src = desc.renderTargetBlend[i];
        auto& dst = psoDesc.BlendState.RenderTarget[i];
        dst.BlendEnable = src.blendEnable;
        dst.SrcBlend = ToDx12Blend(src.srcColor);
        dst.DestBlend = ToDx12Blend(src.dstColor);
        dst.BlendOp = ToDx12BlendOp(src.colorOp);
        dst.SrcBlendAlpha = ToDx12Blend(src.srcAlpha);
        dst.DestBlendAlpha = ToDx12Blend(src.dstAlpha);
        dst.BlendOpAlpha = ToDx12BlendOp(src.alphaOp);
        dst.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        dst.LogicOpEnable = FALSE;
    }

    // Render target formats
    psoDesc.NumRenderTargets = desc.renderTargetCount;
    for (uint32_t i = 0; i < desc.renderTargetCount && i < 8; ++i) {
        psoDesc.RTVFormats[i] = ToDxgiFormat(desc.renderTargetFormats[i]);
    }

    // Topology
    psoDesc.PrimitiveTopologyType = ToDx12TopologyType(desc.topology);

    // Sample desc (no MSAA)
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.SampleMask = UINT_MAX;

    // Create PSO
    ID3D12PipelineState* pso = nullptr;
    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc,
        IID_ID3D12PipelineState, reinterpret_cast<void**>(&pso));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateGraphicsPipelineState failed: hr=0x{:08X} (name='{}')",
               static_cast<uint32_t>(hr), desc.debugName ? desc.debugName : "");
        return false;
    }

    outPipeline.pso = pso;
    outPipeline.desc = desc;

    UZ_LOG(RHI, Info, "Graphics pipeline created (name='{}')",
           desc.debugName ? desc.debugName : "");
    return true;
}

/// Releases a pipeline state object's COM reference.
void DestroyDX12Pipeline(DX12Pipeline& pipeline) {
    if (pipeline.pso) {
        pipeline.pso->Release();
        pipeline.pso = nullptr;
    }
}

} // namespace uz
