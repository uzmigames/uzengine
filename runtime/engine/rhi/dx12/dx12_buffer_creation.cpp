/// UzEngine — DX12 Buffer Creation
///
/// Buffer resource creation/destruction via D3D12MA.
/// Handles constant buffer alignment, persistent CPU mapping, and SRV/UAV descriptors.

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
    UZ_PROFILE_FUNCTION();
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
            const uint64_t numElements = desc.sizeBytes / desc.stride;
            if (numElements > UINT_MAX) {
                UZ_LOG(RHI, Error, "CreateBuffer: SRV element count {} exceeds UINT_MAX (name='{}')",
                       numElements, desc.debugName ? desc.debugName : "");
                srvUavHeap.Free(srvIdx);
                return false;
            }
            srvDesc.Buffer.NumElements = static_cast<UINT>(numElements);
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
                const uint64_t uavElements = desc.sizeBytes / desc.stride;
                if (uavElements > UINT_MAX) {
                    UZ_LOG(RHI, Error, "CreateBuffer: UAV element count {} exceeds UINT_MAX (name='{}')",
                           uavElements, desc.debugName ? desc.debugName : "");
                    srvUavHeap.Free(uavIdx);
                    return false;
                }
                uavDesc.Buffer.NumElements = static_cast<UINT>(uavElements);
                uavDesc.Buffer.StructureByteStride = desc.stride;
            } else {
                const uint64_t uavElements = desc.sizeBytes / 4;
                if (uavElements > UINT_MAX) {
                    UZ_LOG(RHI, Error, "CreateBuffer: UAV element count {} exceeds UINT_MAX (name='{}')",
                           uavElements, desc.debugName ? desc.debugName : "");
                    srvUavHeap.Free(uavIdx);
                    return false;
                }
                uavDesc.Buffer.NumElements = static_cast<UINT>(uavElements);
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
    UZ_PROFILE_FUNCTION();
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

} // namespace uz
