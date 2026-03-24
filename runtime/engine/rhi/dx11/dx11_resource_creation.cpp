/// DX11 Resource Creation — Buffer, Texture, Shader, Pipeline, Sampler
///
/// Implements all Create*/Destroy* and accessor methods on DX11Device for
/// GPU resources. Split from dx11_device.cpp to keep each file under ~800
/// lines. Both files implement methods of the same DX11Device class.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx11_device.h"

#include "log.h"
#include "profiler.h"

#include <windows.h>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Format / State Conversion Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Maps RHIFormat to DXGI_FORMAT for resource creation.
static DXGI_FORMAT ToDxgiFormat(RHIFormat format) {
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
        case RHIFormat::BC1Unorm:       return DXGI_FORMAT_BC1_UNORM;
        case RHIFormat::BC1Srgb:        return DXGI_FORMAT_BC1_UNORM_SRGB;
        case RHIFormat::BC3Unorm:       return DXGI_FORMAT_BC3_UNORM;
        case RHIFormat::BC3Srgb:        return DXGI_FORMAT_BC3_UNORM_SRGB;
        case RHIFormat::BC4Unorm:       return DXGI_FORMAT_BC4_UNORM;
        case RHIFormat::BC5Unorm:       return DXGI_FORMAT_BC5_UNORM;
        case RHIFormat::BC7Unorm:       return DXGI_FORMAT_BC7_UNORM;
        case RHIFormat::BC7Srgb:        return DXGI_FORMAT_BC7_UNORM_SRGB;
        default:                        return DXGI_FORMAT_UNKNOWN;
    }
}

/// Returns true if the format is a depth or depth-stencil format.
static bool IsDepthFormat(RHIFormat format) {
    return format == RHIFormat::D16Unorm || format == RHIFormat::D32Float
        || format == RHIFormat::D24UnormS8Uint || format == RHIFormat::D32FloatS8Uint;
}

/// Returns a typeless DXGI format for depth textures that need both DSV and SRV.
static DXGI_FORMAT ToTypelessDepthFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::D16Unorm:       return DXGI_FORMAT_R16_TYPELESS;
        case RHIFormat::D32Float:       return DXGI_FORMAT_R32_TYPELESS;
        case RHIFormat::D24UnormS8Uint: return DXGI_FORMAT_R24G8_TYPELESS;
        case RHIFormat::D32FloatS8Uint: return DXGI_FORMAT_R32G8X24_TYPELESS;
        default:                        return DXGI_FORMAT_UNKNOWN;
    }
}

/// Returns the SRV-compatible format for reading a depth texture in shaders.
static DXGI_FORMAT ToDepthSrvFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::D16Unorm:       return DXGI_FORMAT_R16_UNORM;
        case RHIFormat::D32Float:       return DXGI_FORMAT_R32_FLOAT;
        case RHIFormat::D24UnormS8Uint: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case RHIFormat::D32FloatS8Uint: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        default:                        return DXGI_FORMAT_UNKNOWN;
    }
}

/// Maps RHICompareFunc to D3D11_COMPARISON_FUNC.
static D3D11_COMPARISON_FUNC ToD3D11CompareFunc(RHICompareFunc func) {
    switch (func) {
        case RHICompareFunc::Never:        return D3D11_COMPARISON_NEVER;
        case RHICompareFunc::Less:         return D3D11_COMPARISON_LESS;
        case RHICompareFunc::Equal:        return D3D11_COMPARISON_EQUAL;
        case RHICompareFunc::LessEqual:    return D3D11_COMPARISON_LESS_EQUAL;
        case RHICompareFunc::Greater:      return D3D11_COMPARISON_GREATER;
        case RHICompareFunc::NotEqual:     return D3D11_COMPARISON_NOT_EQUAL;
        case RHICompareFunc::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
        case RHICompareFunc::Always:       return D3D11_COMPARISON_ALWAYS;
        default:                           return D3D11_COMPARISON_LESS;
    }
}

/// Maps RHIBlendFactor to D3D11_BLEND.
static D3D11_BLEND ToD3D11Blend(RHIBlendFactor factor) {
    switch (factor) {
        case RHIBlendFactor::Zero:       return D3D11_BLEND_ZERO;
        case RHIBlendFactor::One:        return D3D11_BLEND_ONE;
        case RHIBlendFactor::SrcColor:   return D3D11_BLEND_SRC_COLOR;
        case RHIBlendFactor::InvSrcColor:return D3D11_BLEND_INV_SRC_COLOR;
        case RHIBlendFactor::SrcAlpha:   return D3D11_BLEND_SRC_ALPHA;
        case RHIBlendFactor::InvSrcAlpha:return D3D11_BLEND_INV_SRC_ALPHA;
        case RHIBlendFactor::DstColor:   return D3D11_BLEND_DEST_COLOR;
        case RHIBlendFactor::InvDstColor:return D3D11_BLEND_INV_DEST_COLOR;
        case RHIBlendFactor::DstAlpha:   return D3D11_BLEND_DEST_ALPHA;
        case RHIBlendFactor::InvDstAlpha:return D3D11_BLEND_INV_DEST_ALPHA;
        default:                         return D3D11_BLEND_ONE;
    }
}

/// Maps RHIBlendOp to D3D11_BLEND_OP.
static D3D11_BLEND_OP ToD3D11BlendOp(RHIBlendOp op) {
    switch (op) {
        case RHIBlendOp::Add:         return D3D11_BLEND_OP_ADD;
        case RHIBlendOp::Subtract:    return D3D11_BLEND_OP_SUBTRACT;
        case RHIBlendOp::RevSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
        case RHIBlendOp::Min:         return D3D11_BLEND_OP_MIN;
        case RHIBlendOp::Max:         return D3D11_BLEND_OP_MAX;
        default:                      return D3D11_BLEND_OP_ADD;
    }
}

/// Maps RHIAddressMode to D3D11_TEXTURE_ADDRESS_MODE.
static D3D11_TEXTURE_ADDRESS_MODE ToD3D11AddressMode(RHIAddressMode mode) {
    switch (mode) {
        case RHIAddressMode::Wrap:   return D3D11_TEXTURE_ADDRESS_WRAP;
        case RHIAddressMode::Clamp:  return D3D11_TEXTURE_ADDRESS_CLAMP;
        case RHIAddressMode::Mirror: return D3D11_TEXTURE_ADDRESS_MIRROR;
        case RHIAddressMode::Border: return D3D11_TEXTURE_ADDRESS_BORDER;
        default:                     return D3D11_TEXTURE_ADDRESS_WRAP;
    }
}

/// Maps RHIPrimitiveTopology to D3D11_PRIMITIVE_TOPOLOGY.
static D3D11_PRIMITIVE_TOPOLOGY ToD3D11Topology(RHIPrimitiveTopology topo) {
    switch (topo) {
        case RHIPrimitiveTopology::TriangleList:  return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case RHIPrimitiveTopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case RHIPrimitiveTopology::LineList:       return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        case RHIPrimitiveTopology::LineStrip:      return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case RHIPrimitiveTopology::PointList:      return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
        default:                                   return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Operations
// ─────────────────────────────────────────────────────────────────────────────

RHIBufferHandle DX11Device::CreateBuffer(const RHIBufferDesc& desc) {
    UZ_PROFILE_FUNCTION();
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = static_cast<UINT>(desc.sizeBytes);

    // Constant buffers must be 16-byte aligned in DX11
    if (HasFlag(desc.usage, RHIBufferUsage::ConstantBuffer)) {
        bd.ByteWidth = (bd.ByteWidth + 15u) & ~15u;
    }

    // DX11 DYNAMIC requires at least one bind flag. Buffers used only for
    // copy-source (staging) have no bind flag, so fall back to STAGING usage.
    const bool isStagingUpload = (desc.memory == RHIMemoryType::Upload) &&
        (desc.usage == RHIBufferUsage::CopySrc);

    // Map memory type to D3D11 usage
    switch (desc.memory) {
        case RHIMemoryType::Default:
            bd.Usage = D3D11_USAGE_DEFAULT;
            break;
        case RHIMemoryType::Upload:
            if (isStagingUpload) {
                bd.Usage = D3D11_USAGE_STAGING;
                bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
            } else {
                bd.Usage = D3D11_USAGE_DYNAMIC;
                bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            }
            break;
        case RHIMemoryType::Readback:
            bd.Usage = D3D11_USAGE_STAGING;
            // READ for MapBuffer readback, WRITE for CopyTextureToBuffer staging path
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
            break;
    }

    // Map usage flags to bind flags (staging buffers cannot have bind flags)
    if (desc.memory != RHIMemoryType::Readback) {
        if (HasFlag(desc.usage, RHIBufferUsage::VertexBuffer))     bd.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
        if (HasFlag(desc.usage, RHIBufferUsage::IndexBuffer))      bd.BindFlags |= D3D11_BIND_INDEX_BUFFER;
        if (HasFlag(desc.usage, RHIBufferUsage::ConstantBuffer))   bd.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
        if (HasFlag(desc.usage, RHIBufferUsage::StructuredBuffer)) bd.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        if (HasFlag(desc.usage, RHIBufferUsage::UnorderedAccess))  bd.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }

    // Structured buffer misc flags
    if (HasFlag(desc.usage, RHIBufferUsage::StructuredBuffer) && desc.stride > 0) {
        bd.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = desc.stride;
    }

    // Raw byte-address buffer: required for UAV access on non-structured buffers
    if (HasFlag(desc.usage, RHIBufferUsage::UnorderedAccess) &&
        !HasFlag(desc.usage, RHIBufferUsage::StructuredBuffer)) {
        bd.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    }

    // Indirect args flag
    if (HasFlag(desc.usage, RHIBufferUsage::IndirectArgs)) {
        bd.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    }

    ID3D11Buffer* buffer = nullptr;
    HRESULT hr = m_device->CreateBuffer(&bd, nullptr, &buffer);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "DX11 CreateBuffer failed: size={}, hr=0x{:08X}",
               desc.sizeBytes, static_cast<uint32_t>(hr));
        return RHIBufferHandle::Invalid();
    }

    // Debug name
    if (desc.debugName) {
        buffer->SetPrivateData(WKPDID_D3DDebugObjectName,
                               static_cast<UINT>(strlen(desc.debugName)), desc.debugName);
    }

    uint32_t generation = 0;
    uint32_t index = m_bufferPool.Allocate(generation);
    DX11Buffer* slot = m_bufferPool.Get(index, generation);
    if (!slot) {
        buffer->Release();
        UZ_LOG(RHI, Error, "CreateBuffer: pool allocation failed");
        return RHIBufferHandle::Invalid();
    }

    slot->buffer = buffer;
    slot->desc = desc;
    slot->generation = generation;

    // Create SRV for structured buffers
    if (HasFlag(desc.usage, RHIBufferUsage::StructuredBuffer) && desc.stride > 0) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = static_cast<UINT>(desc.sizeBytes / desc.stride);
        hr = m_device->CreateShaderResourceView(buffer, &srvDesc, &slot->srv);
        if (SUCCEEDED(hr)) {
            slot->srvIndex = m_nextSrvIndex++;
        } else {
            UZ_LOG(RHI, Warning, "DX11 CreateSRV for buffer failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        }
    }

    // Create UAV if requested
    if (HasFlag(desc.usage, RHIBufferUsage::UnorderedAccess)) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;

        if (HasFlag(desc.usage, RHIBufferUsage::StructuredBuffer) && desc.stride > 0) {
            // Structured buffer UAV — format must be UNKNOWN, element count = size/stride.
            // Buffer was created with D3D11_RESOURCE_MISC_BUFFER_STRUCTURED.
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.Buffer.NumElements = static_cast<UINT>(desc.sizeBytes / desc.stride);
        } else {
            // Raw byte-address buffer UAV — R32_TYPELESS + RAW flag.
            // Buffer was created with D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS.
            uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            uavDesc.Buffer.NumElements = static_cast<UINT>(desc.sizeBytes / 4);
            uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        }

        hr = m_device->CreateUnorderedAccessView(buffer, &uavDesc, &slot->uav);
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "DX11 CreateUAV for buffer failed (size={}, stride={}, hr=0x{:08X})",
                   desc.sizeBytes, desc.stride, static_cast<uint32_t>(hr));
        }
    }

    UZ_LOG(RHI, Debug, "CreateBuffer: size={}, stride={} (name='{}')",
           desc.sizeBytes, desc.stride, desc.debugName ? desc.debugName : "");

    return {index, generation};
}

void DX11Device::DestroyBuffer(RHIBufferHandle handle) {
    UZ_PROFILE_FUNCTION();
    DX11Buffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) {
        UZ_LOG(RHI, Warning, "DestroyBuffer: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (buf->uav) { buf->uav->Release(); buf->uav = nullptr; }
    if (buf->srv) { buf->srv->Release(); buf->srv = nullptr; }
    if (buf->buffer) { buf->buffer->Release(); buf->buffer = nullptr; }
    if (buf->shadowCopy) { delete[] buf->shadowCopy; buf->shadowCopy = nullptr; buf->shadowCopySize = 0; }
    buf->mappedPtr = nullptr;
    m_bufferPool.Free(handle.index);
}

void* DX11Device::MapBuffer(RHIBufferHandle handle) {
    DX11Buffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf || !buf->buffer) return nullptr;

    // DX11 dynamic/staging buffers require Map/Unmap each time
    if (buf->desc.memory == RHIMemoryType::Upload) {
        // Staging buffers (CopySrc only) use MAP_WRITE; dynamic buffers use WRITE_DISCARD
        const bool isStaging = (buf->desc.usage == RHIBufferUsage::CopySrc);

        // Vertex/index/structured buffers: map directly into the GPU buffer.
        // The GPU reads from this buffer in Draw calls, so data must be written
        // directly — a shadow copy would never reach the GPU.
        //
        // Constant buffer ring: uses a CPU-side shadow buffer because the CB ring
        // allocator persistently maps and later reads back from the pointer in
        // SetRootCBV. WC memory is write-only so reads return stale/zero data.
        const bool needsShadow = !isStaging &&
            HasFlag(buf->desc.usage, RHIBufferUsage::ConstantBuffer);

        if (needsShadow) {
            if (!buf->shadowCopy) {
                buf->shadowCopy = new uint8_t[buf->desc.sizeBytes];
                buf->shadowCopySize = buf->desc.sizeBytes;
                std::memset(buf->shadowCopy, 0, buf->desc.sizeBytes);
            }
            return buf->shadowCopy;
        }

        D3D11_MAP mapType = isStaging ? D3D11_MAP_WRITE : D3D11_MAP_WRITE_DISCARD;
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_immediateContext->Map(buf->buffer, 0, mapType, 0, &mapped);
        if (SUCCEEDED(hr)) {
            buf->mappedPtr = mapped.pData;
            return mapped.pData;
        }
        UZ_LOG(RHI, Error, "MapBuffer: Map failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return nullptr;
    }

    if (buf->desc.memory == RHIMemoryType::Readback) {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_immediateContext->Map(buf->buffer, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr)) {
            return mapped.pData;
        }
        UZ_LOG(RHI, Error, "MapBuffer: Map failed for readback, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return nullptr;
    }

    return nullptr;
}

void DX11Device::UnmapBuffer(RHIBufferHandle handle) {
    DX11Buffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf || !buf->buffer) return;

    if (buf->desc.memory == RHIMemoryType::Upload) {
        // Shadow-copied constant buffers: nothing to unmap — the shadow buffer
        // is CPU-only and will be consumed by SetRootCBV.
        if (buf->shadowCopy) return;

        // Directly-mapped buffers (vertex, index, structured): unmap the GPU buffer.
        if (buf->mappedPtr) {
            m_immediateContext->Unmap(buf->buffer, 0);
            buf->mappedPtr = nullptr;
            return;
        }

        m_immediateContext->Unmap(buf->buffer, 0);
        return;
    }

    if (buf->desc.memory == RHIMemoryType::Readback) {
        m_immediateContext->Unmap(buf->buffer, 0);
    }
}

uint32_t DX11Device::GetBufferSRVIndex(RHIBufferHandle handle) const {
    const DX11Buffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) return UINT32_MAX;
    return buf->srvIndex;
}

uint32_t DX11Device::GetBufferUAVIndex(RHIBufferHandle handle) const {
    const auto* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf || !buf->uav) return UINT32_MAX;
    return handle.index; // sentinel: UAV exists
}

uint64_t DX11Device::GetBufferGPUAddress(RHIBufferHandle handle) const {
    // DX11 does not expose GPU virtual addresses
    (void)handle;
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Operations
// ─────────────────────────────────────────────────────────────────────────────

RHITextureHandle DX11Device::CreateTexture(const RHITextureDesc& desc) {
    UZ_PROFILE_FUNCTION();

    // DX11 backend does not support 3D (volume) textures — they require
    // ID3D11Texture3D which is a separate creation path. The color grading
    // LUT and other 3D textures are only used on the DX12 bindless path.
    if (desc.is3D) {
        UZ_LOG(RHI, Warning, "DX11: 3D textures not supported, skipping '{}' ({}x{}x{})",
               desc.debugName ? desc.debugName : "<unnamed>",
               desc.width, desc.height, desc.depthOrArraySize);
        return RHITextureHandle::Invalid();
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = desc.width;
    td.Height = desc.height;
    td.MipLevels = desc.mipLevels;
    td.ArraySize = desc.depthOrArraySize;
    td.SampleDesc.Count = 1;
    td.SampleDesc.Quality = 0;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.CPUAccessFlags = 0;

    // Determine format — use typeless if depth+SRV combo needed
    bool needsTypeless = IsDepthFormat(desc.format)
                      && HasFlag(desc.usage, RHITextureUsage::DepthStencil)
                      && HasFlag(desc.usage, RHITextureUsage::ShaderResource);
    td.Format = needsTypeless ? ToTypelessDepthFormat(desc.format) : ToDxgiFormat(desc.format);

    // Bind flags
    if (HasFlag(desc.usage, RHITextureUsage::ShaderResource))  td.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (HasFlag(desc.usage, RHITextureUsage::RenderTarget))    td.BindFlags |= D3D11_BIND_RENDER_TARGET;
    if (HasFlag(desc.usage, RHITextureUsage::DepthStencil))    td.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    if (HasFlag(desc.usage, RHITextureUsage::UnorderedAccess)) td.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    // Cubemap flag — required for TextureCube SRV creation (6-face array = cubemap)
    if (desc.depthOrArraySize == 6) {
        td.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
    }

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&td, nullptr, &texture);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "DX11 CreateTexture2D failed: {}x{}, hr=0x{:08X}",
               desc.width, desc.height, static_cast<uint32_t>(hr));
        return RHITextureHandle::Invalid();
    }

    if (desc.debugName) {
        texture->SetPrivateData(WKPDID_D3DDebugObjectName,
                                static_cast<UINT>(strlen(desc.debugName)), desc.debugName);
    }

    uint32_t generation = 0;
    uint32_t index = m_texturePool.Allocate(generation);
    DX11Texture* slot = m_texturePool.Get(index, generation);
    if (!slot) {
        texture->Release();
        UZ_LOG(RHI, Error, "CreateTexture: pool allocation failed");
        return RHITextureHandle::Invalid();
    }

    slot->texture = texture;
    slot->desc = desc;
    slot->generation = generation;

    // Create SRV
    if (HasFlag(desc.usage, RHITextureUsage::ShaderResource)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = IsDepthFormat(desc.format) ? ToDepthSrvFormat(desc.format) : ToDxgiFormat(desc.format);

        if (desc.depthOrArraySize == 6) {
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDesc.TextureCube.MipLevels = desc.mipLevels;
        } else if (desc.depthOrArraySize > 1) {
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            srvDesc.Texture2DArray.MostDetailedMip = 0;
            srvDesc.Texture2DArray.MipLevels = desc.mipLevels;
            srvDesc.Texture2DArray.FirstArraySlice = 0;
            srvDesc.Texture2DArray.ArraySize = desc.depthOrArraySize;
        } else {
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = desc.mipLevels;
        }

        hr = m_device->CreateShaderResourceView(texture, &srvDesc, &slot->srv);
        if (SUCCEEDED(hr)) {
            slot->srvIndex = m_nextSrvIndex++;
        } else {
            UZ_LOG(RHI, Warning, "DX11 CreateSRV for texture failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        }
    }

    // Create RTV
    if (HasFlag(desc.usage, RHITextureUsage::RenderTarget)) {
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = ToDxgiFormat(desc.format);
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateRenderTargetView(texture, &rtvDesc, &slot->rtv);
        if (FAILED(hr)) {
            UZ_LOG(RHI, Warning, "DX11 CreateRTV failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        }
    }

    // Create DSV
    if (HasFlag(desc.usage, RHITextureUsage::DepthStencil)) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = ToDxgiFormat(desc.format);
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateDepthStencilView(texture, &dsvDesc, &slot->dsv);
        if (FAILED(hr)) {
            UZ_LOG(RHI, Warning, "DX11 CreateDSV failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        }
    }

    // Create UAV
    if (HasFlag(desc.usage, RHITextureUsage::UnorderedAccess)) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = ToDxgiFormat(desc.format);
        if (desc.depthOrArraySize > 1) {
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            uavDesc.Texture2DArray.MipSlice        = 0;
            uavDesc.Texture2DArray.FirstArraySlice  = 0;
            uavDesc.Texture2DArray.ArraySize        = desc.depthOrArraySize;
        } else {
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = 0;
        }
        hr = m_device->CreateUnorderedAccessView(texture, &uavDesc, &slot->uav);
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "DX11 CreateUAV for texture failed ({}x{}, arraySize={}, fmt={}, hr=0x{:08X})",
                   desc.width, desc.height, desc.depthOrArraySize,
                   static_cast<int>(desc.format), static_cast<uint32_t>(hr));
        }
    }

    UZ_LOG(RHI, Debug, "CreateTexture: {}x{}, mips={} (name='{}')",
           desc.width, desc.height, desc.mipLevels,
           desc.debugName ? desc.debugName : "");

    return {index, generation};
}

void DX11Device::DestroyTexture(RHITextureHandle handle) {
    UZ_PROFILE_FUNCTION();
    DX11Texture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) {
        UZ_LOG(RHI, Warning, "DestroyTexture: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (tex->uav) { tex->uav->Release(); tex->uav = nullptr; }
    if (tex->dsv) { tex->dsv->Release(); tex->dsv = nullptr; }
    if (tex->rtv) { tex->rtv->Release(); tex->rtv = nullptr; }
    if (tex->srv) { tex->srv->Release(); tex->srv = nullptr; }
    if (tex->texture) { tex->texture->Release(); tex->texture = nullptr; }
    m_texturePool.Free(handle.index);
}

uint32_t DX11Device::GetTextureSRVIndex(RHITextureHandle handle) const {
    const DX11Texture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) return UINT32_MAX;
    return tex->srvIndex;
}

uint32_t DX11Device::GetTextureUAVIndex(RHITextureHandle handle) const {
    const auto* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex || !tex->uav) return UINT32_MAX;
    // DX11 doesn't use bindless descriptor indices. Return the pool index
    // as a sentinel so that callers can detect "UAV exists" (index != UINT32_MAX).
    // Actual UAV binding uses the ID3D11UnorderedAccessView* directly.
    return handle.index;
}

ID3D11ShaderResourceView* DX11Device::FindTextureSRVByIndex(uint32_t srvIndex) {
    for (auto& slot : m_texturePool.Slots()) {
        if (slot.srvIndex == srvIndex && slot.srv) return slot.srv;
    }
    for (auto& slot : m_bufferPool.Slots()) {
        if (slot.srvIndex == srvIndex && slot.srv) return slot.srv;
    }
    return nullptr;
}

ID3D11UnorderedAccessView* DX11Device::FindTextureUAVByPoolIndex(uint32_t poolIndex) {
    auto& slots = m_texturePool.Slots();
    if (poolIndex < static_cast<uint32_t>(slots.size()) && slots[poolIndex].uav) {
        return slots[poolIndex].uav;
    }
    return nullptr;
}

ID3D11UnorderedAccessView* DX11Device::FindBufferUAVByPoolIndex(uint32_t poolIndex) {
    auto& slots = m_bufferPool.Slots();
    if (poolIndex < static_cast<uint32_t>(slots.size()) && slots[poolIndex].uav) {
        return slots[poolIndex].uav;
    }
    return nullptr;
}

ID3D11SamplerState* DX11Device::FindSamplerByIndex(uint32_t samplerIndex) {
    for (auto& slot : m_samplerPool.Slots()) {
        if (slot.samplerIndex == samplerIndex && slot.sampler) return slot.sampler;
    }
    return nullptr;
}

RHITextureDesc DX11Device::GetTextureDesc(RHITextureHandle handle) const {
    const DX11Texture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) return {};
    return tex->desc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader Operations
// ─────────────────────────────────────────────────────────────────────────────

RHIShaderHandle DX11Device::CreateShader(const RHIShaderDesc& desc) {
    UZ_PROFILE_FUNCTION();
    if (!desc.bytecode || desc.bytecodeSize == 0) {
        UZ_LOG(RHI, Error, "CreateShader: null/empty bytecode (name='{}')",
               desc.debugName ? desc.debugName : "");
        return RHIShaderHandle::Invalid();
    }

    uint32_t generation = 0;
    uint32_t index = m_shaderPool.Allocate(generation);
    DX11Shader* slot = m_shaderPool.Get(index, generation);
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateShader: pool allocation failed");
        return RHIShaderHandle::Invalid();
    }

    auto* bytes = static_cast<const uint8_t*>(desc.bytecode);
    slot->bytecode.assign(bytes, bytes + desc.bytecodeSize);
    slot->stage = desc.stage;
    slot->generation = generation;

    HRESULT hr = S_OK;
    switch (desc.stage) {
        case RHIShaderStage::Vertex:
            hr = m_device->CreateVertexShader(desc.bytecode, static_cast<SIZE_T>(desc.bytecodeSize),
                                              nullptr, &slot->vs);
            break;
        case RHIShaderStage::Pixel:
            hr = m_device->CreatePixelShader(desc.bytecode, static_cast<SIZE_T>(desc.bytecodeSize),
                                             nullptr, &slot->ps);
            break;
        case RHIShaderStage::Compute:
            hr = m_device->CreateComputeShader(desc.bytecode, static_cast<SIZE_T>(desc.bytecodeSize),
                                               nullptr, &slot->cs);
            break;
    }

    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "DX11 CreateShader failed: stage={}, hr=0x{:08X}",
               static_cast<int>(desc.stage), static_cast<uint32_t>(hr));
        slot->bytecode.clear();
        m_shaderPool.Free(index);
        return RHIShaderHandle::Invalid();
    }

    UZ_LOG(RHI, Debug, "CreateShader: stage={}, size={} (name='{}')",
           static_cast<int>(desc.stage), desc.bytecodeSize,
           desc.debugName ? desc.debugName : "");

    return {index, generation};
}

void DX11Device::DestroyShader(RHIShaderHandle handle) {
    UZ_PROFILE_FUNCTION();
    DX11Shader* shader = m_shaderPool.Get(handle.index, handle.generation);
    if (!shader) {
        UZ_LOG(RHI, Warning, "DestroyShader: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (shader->vs) { shader->vs->Release(); shader->vs = nullptr; }
    if (shader->ps) { shader->ps->Release(); shader->ps = nullptr; }
    if (shader->cs) { shader->cs->Release(); shader->cs = nullptr; }
    shader->bytecode.clear();
    shader->bytecode.shrink_to_fit();
    m_shaderPool.Free(handle.index);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Operations
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle DX11Device::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) {
    UZ_PROFILE_FUNCTION();
    // Resolve vertex shader
    const DX11Shader* vs = m_shaderPool.Get(desc.vertexShader.index, desc.vertexShader.generation);
    if (!vs || !vs->vs) {
        UZ_LOG(RHI, Error, "CreateGraphicsPipeline: invalid vertex shader handle");
        return RHIPipelineHandle::Invalid();
    }

    uint32_t generation = 0;
    uint32_t index = m_pipelinePool.Allocate(generation);
    DX11Pipeline* slot = m_pipelinePool.Get(index, generation);
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateGraphicsPipeline: pool allocation failed");
        return RHIPipelineHandle::Invalid();
    }

    slot->vertexShader = desc.vertexShader;
    slot->pixelShader = desc.pixelShader;
    slot->topology = ToD3D11Topology(desc.topology);
    slot->generation = generation;
    slot->desc = desc;

    // Create input layout from vertex shader bytecode + input elements
    if (desc.inputElementCount > 0 && desc.inputElements) {
        std::vector<D3D11_INPUT_ELEMENT_DESC> d3dElements(desc.inputElementCount);
        for (uint32_t i = 0; i < desc.inputElementCount; ++i) {
            const auto& elem = desc.inputElements[i];
            d3dElements[i].SemanticName = InputSemanticToString(elem.semantic);
            d3dElements[i].SemanticIndex = elem.semanticIndex;
            d3dElements[i].Format = ToDxgiFormat(elem.format);
            d3dElements[i].InputSlot = elem.inputSlot;
            d3dElements[i].AlignedByteOffset = elem.byteOffset;
            d3dElements[i].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            d3dElements[i].InstanceDataStepRate = 0;
        }

        HRESULT hr = m_device->CreateInputLayout(
            d3dElements.data(), static_cast<UINT>(d3dElements.size()),
            vs->bytecode.data(), vs->bytecode.size(),
            &slot->inputLayout);
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "DX11 CreateInputLayout failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
            m_pipelinePool.Free(index);
            return RHIPipelineHandle::Invalid();
        }
    }

    // Create rasterizer state
    {
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = (desc.rasterizer.fillMode == RHIFillMode::Wireframe)
                          ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
        switch (desc.rasterizer.cullMode) {
            case RHICullMode::None:  rd.CullMode = D3D11_CULL_NONE;  break;
            case RHICullMode::Front: rd.CullMode = D3D11_CULL_FRONT; break;
            case RHICullMode::Back:  rd.CullMode = D3D11_CULL_BACK;  break;
        }
        rd.FrontCounterClockwise = desc.rasterizer.frontCounterClockwise ? TRUE : FALSE;
        rd.DepthBias = static_cast<INT>(desc.rasterizer.depthBias);
        rd.SlopeScaledDepthBias = desc.rasterizer.slopeScaledDepthBias;
        rd.DepthClipEnable = TRUE;
        rd.ScissorEnable = TRUE;
        rd.MultisampleEnable = FALSE;
        rd.AntialiasedLineEnable = FALSE;

        HRESULT hr = m_device->CreateRasterizerState(&rd, &slot->rasterizer);
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "DX11 CreateRasterizerState failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
            if (slot->inputLayout) slot->inputLayout->Release();
            m_pipelinePool.Free(index);
            return RHIPipelineHandle::Invalid();
        }
    }

    // Create depth-stencil state
    {
        D3D11_DEPTH_STENCIL_DESC dsd = {};
        dsd.DepthEnable = desc.depthStencil.depthEnable ? TRUE : FALSE;
        dsd.DepthWriteMask = desc.depthStencil.depthWrite
                                 ? D3D11_DEPTH_WRITE_MASK_ALL
                                 : D3D11_DEPTH_WRITE_MASK_ZERO;
        dsd.DepthFunc = ToD3D11CompareFunc(desc.depthStencil.depthFunc);
        dsd.StencilEnable = FALSE;

        HRESULT hr = m_device->CreateDepthStencilState(&dsd, &slot->depthStencil);
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "DX11 CreateDepthStencilState failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
            if (slot->rasterizer) slot->rasterizer->Release();
            if (slot->inputLayout) slot->inputLayout->Release();
            m_pipelinePool.Free(index);
            return RHIPipelineHandle::Invalid();
        }
    }

    // Create blend state
    {
        D3D11_BLEND_DESC bd = {};
        bd.AlphaToCoverageEnable = FALSE;
        bd.IndependentBlendEnable = (desc.renderTargetCount > 1) ? TRUE : FALSE;

        for (uint32_t i = 0; i < desc.renderTargetCount && i < 8; ++i) {
            auto& rt = bd.RenderTarget[i];
            const auto& src = desc.renderTargetBlend[i];
            rt.BlendEnable = src.blendEnable ? TRUE : FALSE;
            rt.SrcBlend = ToD3D11Blend(src.srcColor);
            rt.DestBlend = ToD3D11Blend(src.dstColor);
            rt.BlendOp = ToD3D11BlendOp(src.colorOp);
            rt.SrcBlendAlpha = ToD3D11Blend(src.srcAlpha);
            rt.DestBlendAlpha = ToD3D11Blend(src.dstAlpha);
            rt.BlendOpAlpha = ToD3D11BlendOp(src.alphaOp);
            rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        }

        HRESULT hr = m_device->CreateBlendState(&bd, &slot->blend);
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "DX11 CreateBlendState failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
            if (slot->depthStencil) slot->depthStencil->Release();
            if (slot->rasterizer) slot->rasterizer->Release();
            if (slot->inputLayout) slot->inputLayout->Release();
            m_pipelinePool.Free(index);
            return RHIPipelineHandle::Invalid();
        }
    }

    UZ_LOG(RHI, Debug, "CreateGraphicsPipeline: RT count={} (name='{}')",
           desc.renderTargetCount, desc.debugName ? desc.debugName : "");

    return {index, generation};
}

RHIPipelineHandle DX11Device::CreateComputePipeline(const RHIComputePipelineDesc& desc) {
    UZ_PROFILE_FUNCTION();

    // Resolve compute shader from pool
    const DX11Shader* cs = m_shaderPool.Get(desc.computeShader.index, desc.computeShader.generation);
    if (!cs || !cs->cs) {
        UZ_LOG(RHI, Error, "CreateComputePipeline: invalid compute shader handle (name='{}')",
               desc.debugName ? desc.debugName : "");
        return RHIPipelineHandle::Invalid();
    }

    uint32_t generation = 0;
    uint32_t index = m_pipelinePool.Allocate(generation);
    DX11Pipeline* slot = m_pipelinePool.Get(index, generation);
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateComputePipeline: pool allocation failed (name='{}')",
               desc.debugName ? desc.debugName : "");
        return RHIPipelineHandle::Invalid();
    }

    // Compute pipelines have no graphics state objects — only the shader handle.
    // inputLayout, rasterizer, depthStencil, blend remain nullptr.
    slot->computeShader = desc.computeShader;
    slot->isCompute = true;
    slot->generation = generation;

    UZ_LOG(RHI, Debug, "CreateComputePipeline: created (name='{}')",
           desc.debugName ? desc.debugName : "");

    return RHIPipelineHandle{index, generation};
}

void DX11Device::DestroyPipeline(RHIPipelineHandle handle) {
    UZ_PROFILE_FUNCTION();
    DX11Pipeline* pipeline = m_pipelinePool.Get(handle.index, handle.generation);
    if (!pipeline) {
        UZ_LOG(RHI, Warning, "DestroyPipeline: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (pipeline->blend) { pipeline->blend->Release(); pipeline->blend = nullptr; }
    if (pipeline->depthStencil) { pipeline->depthStencil->Release(); pipeline->depthStencil = nullptr; }
    if (pipeline->rasterizer) { pipeline->rasterizer->Release(); pipeline->rasterizer = nullptr; }
    if (pipeline->inputLayout) { pipeline->inputLayout->Release(); pipeline->inputLayout = nullptr; }
    m_pipelinePool.Free(handle.index);
}

std::vector<uint8_t> DX11Device::GetPipelineCachedBlob(RHIPipelineHandle /*handle*/) {
    // DX11 does not support PSO blob extraction
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Operations
// ─────────────────────────────────────────────────────────────────────────────

RHISamplerHandle DX11Device::CreateSampler(const RHISamplerDesc& desc) {
    UZ_PROFILE_FUNCTION();
    D3D11_SAMPLER_DESC sd = {};

    switch (desc.filter) {
        case RHIFilterMode::Point:
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            break;
        case RHIFilterMode::Linear:
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            break;
        case RHIFilterMode::Anisotropic:
            sd.Filter = D3D11_FILTER_ANISOTROPIC;
            break;
    }

    sd.AddressU = ToD3D11AddressMode(desc.addressU);
    sd.AddressV = ToD3D11AddressMode(desc.addressV);
    sd.AddressW = ToD3D11AddressMode(desc.addressW);
    sd.MipLODBias = desc.mipLodBias;
    sd.MaxAnisotropy = desc.maxAnisotropy;
    sd.ComparisonFunc = ToD3D11CompareFunc(desc.compareFunc);
    sd.MinLOD = desc.minLod;
    sd.MaxLOD = desc.maxLod;
    // Border color defaults to transparent black (0,0,0,0)

    ID3D11SamplerState* sampler = nullptr;
    HRESULT hr = m_device->CreateSamplerState(&sd, &sampler);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "DX11 CreateSamplerState failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return RHISamplerHandle::Invalid();
    }

    uint32_t generation = 0;
    uint32_t index = m_samplerPool.Allocate(generation);
    DX11Sampler* slot = m_samplerPool.Get(index, generation);
    if (!slot) {
        sampler->Release();
        UZ_LOG(RHI, Error, "CreateSampler: pool allocation failed");
        return RHISamplerHandle::Invalid();
    }

    slot->sampler = sampler;
    slot->samplerIndex = m_nextSamplerIndex++;
    slot->generation = generation;

    return {index, generation};
}

void DX11Device::DestroySampler(RHISamplerHandle handle) {
    UZ_PROFILE_FUNCTION();
    DX11Sampler* sampler = m_samplerPool.Get(handle.index, handle.generation);
    if (!sampler) {
        UZ_LOG(RHI, Warning, "DestroySampler: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (sampler->sampler) { sampler->sampler->Release(); sampler->sampler = nullptr; }
    m_samplerPool.Free(handle.index);
}

uint32_t DX11Device::GetSamplerIndex(RHISamplerHandle handle) const {
    const DX11Sampler* sampler = m_samplerPool.Get(handle.index, handle.generation);
    if (!sampler) return UINT32_MAX;
    return sampler->samplerIndex;
}

} // namespace uz
