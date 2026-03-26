/// DX12 Command List Implementation
///
/// Records GPU commands (barriers, clears, draws) into a command allocator.
/// Uses triple-buffered allocators for frame pacing and SM6.6 bindless heap binding.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_command_list.h"
#include "dx12_compat.h"
#include "dx12_device.h"
#include "dx12_resource_creation.h"
#include "d3dx12.h"

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <string_view>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// State and Format Mapping
// ─────────────────────────────────────────────────────────────────────────────

/// Maps RHIResourceState to the corresponding D3D12_RESOURCE_STATES bitmask.
static D3D12_RESOURCE_STATES ToD3D12ResourceState(RHIResourceState state) {
    switch (state) {
        case RHIResourceState::Common:            return D3D12_RESOURCE_STATE_COMMON;
        case RHIResourceState::VertexBuffer:      return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case RHIResourceState::IndexBuffer:       return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        case RHIResourceState::ConstantBuffer:    return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case RHIResourceState::ShaderResource:    return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                                                       | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case RHIResourceState::UnorderedAccess:   return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case RHIResourceState::RenderTarget:      return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case RHIResourceState::DepthStencilWrite: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case RHIResourceState::DepthStencilRead:  return D3D12_RESOURCE_STATE_DEPTH_READ;
        case RHIResourceState::CopySrc:           return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case RHIResourceState::CopyDst:           return D3D12_RESOURCE_STATE_COPY_DEST;
        case RHIResourceState::Present:           return D3D12_RESOURCE_STATE_PRESENT;
        case RHIResourceState::IndirectArgument:  return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        default:                                  return D3D12_RESOURCE_STATE_COMMON;
    }
}

/// Maps RHIIndexFormat to the corresponding DXGI_FORMAT.
static DXGI_FORMAT ToD3D12IndexFormat(RHIIndexFormat format) {
    switch (format) {
        case RHIIndexFormat::Uint16: return DXGI_FORMAT_R16_UINT;
        case RHIIndexFormat::Uint32: return DXGI_FORMAT_R32_UINT;
        default:                     return DXGI_FORMAT_R32_UINT;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Queue Type Mapping
// ─────────────────────────────────────────────────────────────────────────────

/// Maps RHIQueueType to the corresponding D3D12 command list type.
static D3D12_COMMAND_LIST_TYPE ToD3D12ListType(RHIQueueType queue) {
    switch (queue) {
        case RHIQueueType::Graphics: return D3D12_COMMAND_LIST_TYPE_DIRECT;
        case RHIQueueType::Compute:  return D3D12_COMMAND_LIST_TYPE_COMPUTE;
        case RHIQueueType::Copy:     return D3D12_COMMAND_LIST_TYPE_COPY;
        default:                     return D3D12_COMMAND_LIST_TYPE_DIRECT;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

DX12CommandList::~DX12CommandList() {
    m_cmdList.Reset();
    for (auto& alloc : m_allocators) {
        alloc.Reset();
    }
    UZ_LOG(RHI, Info, "DX12CommandList destroyed");
}

bool DX12CommandList::Init(DX12Device* device, RHIQueueType queueType) {
    UZ_PROFILE_FUNCTION();
    if (!device || !device->GetDevice()) {
        UZ_LOG(RHI, Error, "DX12CommandList::Init — null device");
        return false;
    }

    m_device = device;
    m_listType = ToD3D12ListType(queueType);

    ID3D12Device* d3dDevice = m_device->GetDevice();

    // Create one command allocator per frame in flight
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        HRESULT hr = d3dDevice->CreateCommandAllocator(
            m_listType, IID_PPV_ARGS(&m_allocators[i]));
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "CreateCommandAllocator({}) failed, HRESULT=0x{:08X}",
                   i, static_cast<uint32_t>(hr));
            return false;
        }
    }

    // Create the command list in recording state, then close it.
    // We use CreateCommandList (not CreateCommandList1) for broader compatibility
    // with ID3D12Device base interface.
    HRESULT hr = d3dDevice->CreateCommandList(
        0, m_listType, m_allocators[0].Get(), nullptr, IID_PPV_ARGS(&m_cmdList));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateCommandList failed, HRESULT=0x{:08X}",
               static_cast<uint32_t>(hr));
        return false;
    }

    // Close the command list — Begin() will reset it before recording
    m_cmdList->Close();

    // Allocate a contiguous block of kMaxFragmentTextures descriptors in the
    // shader-visible CBV_SRV_UAV heap for the SM5 SetFragmentTexture staging area.
    // These are allocated once and reused every frame via CopyDescriptorsSimple.
    if (m_listType == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        auto& srvAlloc = m_device->GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        uint32_t base = srvAlloc.Allocate();
        if (base == DescriptorAllocator::kInvalidIndex) {
            UZ_LOG(RHI, Error, "Failed to allocate fragment texture staging block");
            return false;
        }
        m_fragmentTexStagingBase = base;

        // Allocate remaining slots — bump allocator guarantees contiguity for
        // sequential Allocate() calls when no free-list entries exist yet.
        for (uint32_t i = 1; i < kMaxFragmentTextures; ++i) {
            uint32_t idx = srvAlloc.Allocate();
            if (idx != base + i) {
                UZ_LOG(RHI, Error, "Fragment texture staging block not contiguous at slot {} "
                       "(expected {}, got {})", i, base + i, idx);
                return false;
            }
        }

        UZ_LOG(RHI, Info, "Allocated fragment texture staging block at heap index {} ({} slots)",
               m_fragmentTexStagingBase, kMaxFragmentTextures);
    }

    UZ_LOG(RHI, Info, "DX12CommandList created, type={}", static_cast<int>(m_listType));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Recording Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void DX12CommandList::Begin() {
    UZ_PROFILE_FUNCTION();
    auto& allocator = m_allocators[m_currentFrame];

    HRESULT hr = allocator->Reset();
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CommandAllocator::Reset failed, HRESULT=0x{:08X}",
               static_cast<uint32_t>(hr));
        return;
    }

    hr = m_cmdList->Reset(allocator.Get(), nullptr);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CommandList::Reset failed, HRESULT=0x{:08X}",
               static_cast<uint32_t>(hr));
        return;
    }

    // Bind the global shader-visible descriptor heaps for bindless access
    if (m_listType != D3D12_COMMAND_LIST_TYPE_COPY) {
        ID3D12DescriptorHeap* heaps[] = {
            m_device->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
            m_device->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER),
        };

        uint32_t heapCount = 0;
        for (auto* h : heaps) {
            if (h) ++heapCount;
        }

        if (heapCount > 0) {
            m_cmdList->SetDescriptorHeaps(heapCount, heaps);
        }
    }
}

void DX12CommandList::End() {
    UZ_PROFILE_FUNCTION();
    HRESULT hr = m_cmdList->Close();
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CommandList::Close failed, HRESULT=0x{:08X}",
               static_cast<uint32_t>(hr));

        // Dump D3D12 debug layer messages for diagnosis
        ComPtr<ID3D12InfoQueue> infoQueue;
        ComPtr<ID3D12Device> device;
        m_cmdList->GetDevice(IID_PPV_ARGS(&device));
        if (device && SUCCEEDED(device.As(&infoQueue))) {
            UINT64 msgCount = infoQueue->GetNumStoredMessages();
            for (UINT64 i = 0; i < msgCount; ++i) {
                SIZE_T msgLen = 0;
                infoQueue->GetMessage(i, nullptr, &msgLen);
                if (msgLen > 0) {
                    auto* msg = static_cast<D3D12_MESSAGE*>(malloc(msgLen));
                    if (msg && SUCCEEDED(infoQueue->GetMessage(i, msg, &msgLen))) {
                        UZ_LOG(RHI, Error, "D3D12 Debug: [{}] {}",
                               static_cast<int>(msg->Severity),
                               std::string_view(msg->pDescription, msg->DescriptionByteLength - 1));
                    }
                    free(msg);
                }
            }
            infoQueue->ClearStoredMessages();
        }
    }
}

void DX12CommandList::Execute() {
    UZ_PROFILE_FUNCTION();
    ID3D12CommandQueue* queue = nullptr;

    switch (m_listType) {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            queue = m_device->GetDirectQueue();
            break;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            queue = m_device->GetComputeQueue();
            break;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            queue = m_device->GetCopyQueue();
            break;
        default:
            UZ_LOG(RHI, Warning, "No queue available for command list type {}",
                   static_cast<int>(m_listType));
            return;
    }

    if (!queue) {
        UZ_LOG(RHI, Error, "Command queue is null for list type {}",
               static_cast<int>(m_listType));
        return;
    }

    ID3D12CommandList* lists[] = { m_cmdList.Get() };
    queue->ExecuteCommandLists(1, lists);

    m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline and Resource Binding
// ─────────────────────────────────────────────────────────────────────────────

/// Maps RHIPrimitiveTopology to D3D_PRIMITIVE_TOPOLOGY for IASetPrimitiveTopology.
/// Note: this is the IA-level topology (TriangleList, LineList, etc.), NOT the
/// PSO-level D3D12_PRIMITIVE_TOPOLOGY_TYPE (Triangle, Line, Point).
/// @source DX12 docs — ID3D12GraphicsCommandList::IASetPrimitiveTopology
static D3D_PRIMITIVE_TOPOLOGY ToDx12Topology(RHIPrimitiveTopology topo) {
    switch (topo) {
        case RHIPrimitiveTopology::TriangleList:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case RHIPrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case RHIPrimitiveTopology::LineList:       return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case RHIPrimitiveTopology::LineStrip:      return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case RHIPrimitiveTopology::PointList:      return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        default:                                   return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

void DX12CommandList::SetPipeline(RHIPipelineHandle pipeline) {
    if (!pipeline.IsValid()) {
        UZ_LOG(RHI, Warning, "SetPipeline — invalid pipeline handle");
        return;
    }

    auto* dx12Pipeline = m_device->GetPipelinePool().Get(pipeline.index, pipeline.generation);
    if (!dx12Pipeline || !dx12Pipeline->pso) {
        UZ_LOG(RHI, Warning, "SetPipeline — pipeline not found in pool (index={}, gen={})",
               pipeline.index, pipeline.generation);
        return;
    }

    m_cmdList->SetPipelineState(dx12Pipeline->pso);
    m_isComputeBound = dx12Pipeline->isCompute;

    // Bind the global root signature — compute and graphics use the same
    // root signature but require different Set* entry points.
    if (auto* rootSig = m_device->GetRootSignature()) {
        if (m_isComputeBound) {
            m_cmdList->SetComputeRootSignature(rootSig);
        } else {
            m_cmdList->SetGraphicsRootSignature(rootSig);

            // D3D12 requires the IA primitive topology to be set on the command
            // list — the PSO only declares the topology TYPE (Triangle/Line/Point),
            // not the actual topology (TriangleList/TriangleStrip/etc.).
            // Without this, DrawIndexedInstanced fails with UNDEFINED topology.
            m_cmdList->IASetPrimitiveTopology(ToDx12Topology(dx12Pipeline->desc.topology));
        }
    }
}

void DX12CommandList::SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer, uint32_t offset) {
    if (!buffer.IsValid()) {
        UZ_LOG(RHI, Warning, "SetVertexBuffer — invalid buffer handle");
        return;
    }

    auto* dx12Buffer = m_device->GetBufferPool().Get(buffer.index, buffer.generation);
    if (!dx12Buffer || !dx12Buffer->resource) {
        UZ_LOG(RHI, Warning, "SetVertexBuffer — buffer not found in pool (index={}, gen={})",
               buffer.index, buffer.generation);
        return;
    }

    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = dx12Buffer->resource->GetGPUVirtualAddress() + offset;
    vbv.SizeInBytes = static_cast<UINT>(dx12Buffer->desc.sizeBytes - offset);
    vbv.StrideInBytes = dx12Buffer->desc.stride;
    m_cmdList->IASetVertexBuffers(slot, 1, &vbv);
}

void DX12CommandList::SetIndexBuffer(RHIBufferHandle buffer, RHIIndexFormat format, uint32_t offset) {
    if (!buffer.IsValid()) {
        UZ_LOG(RHI, Warning, "SetIndexBuffer — invalid buffer handle");
        return;
    }

    auto* dx12Buffer = m_device->GetBufferPool().Get(buffer.index, buffer.generation);
    if (!dx12Buffer || !dx12Buffer->resource) {
        UZ_LOG(RHI, Warning, "SetIndexBuffer — buffer not found in pool (index={}, gen={})",
               buffer.index, buffer.generation);
        return;
    }

    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = dx12Buffer->resource->GetGPUVirtualAddress() + offset;
    ibv.SizeInBytes = static_cast<UINT>(dx12Buffer->desc.sizeBytes - offset);
    ibv.Format = ToD3D12IndexFormat(format);
    m_cmdList->IASetIndexBuffer(&ibv);
}

void DX12CommandList::SetViewport(const RHIViewport& viewport) {
    D3D12_VIEWPORT vp = {
        viewport.x, viewport.y,
        viewport.width, viewport.height,
        viewport.minDepth, viewport.maxDepth
    };
    m_cmdList->RSSetViewports(1, &vp);
}

void DX12CommandList::SetScissor(const RHIScissorRect& scissor) {
    D3D12_RECT rect = {
        static_cast<LONG>(scissor.left),
        static_cast<LONG>(scissor.top),
        static_cast<LONG>(scissor.right),
        static_cast<LONG>(scissor.bottom)
    };
    m_cmdList->RSSetScissorRects(1, &rect);
}

void DX12CommandList::SetDescriptorIndex(uint32_t rootParamIndex, uint32_t descriptorIndex) {
    if (m_isComputeBound) {
        m_cmdList->SetComputeRoot32BitConstant(rootParamIndex, descriptorIndex, 0);
    } else {
        m_cmdList->SetGraphicsRoot32BitConstant(rootParamIndex, descriptorIndex, 0);
    }
}

void DX12CommandList::SetRootConstants(uint32_t rootParamIndex, const uint32_t* data,
                                        uint32_t count, uint32_t offsetIn32BitValues) {
    if (m_isComputeBound) {
        m_cmdList->SetComputeRoot32BitConstants(rootParamIndex, count, data, offsetIn32BitValues);
    } else {
        m_cmdList->SetGraphicsRoot32BitConstants(rootParamIndex, count, data, offsetIn32BitValues);
    }
}

void DX12CommandList::SetRootCBV(uint32_t rootParamIndex, uint64_t gpuAddress) {
    if (m_isComputeBound) {
        m_cmdList->SetComputeRootConstantBufferView(rootParamIndex, gpuAddress);
    } else {
        m_cmdList->SetGraphicsRootConstantBufferView(rootParamIndex, gpuAddress);
    }
}

void DX12CommandList::SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                                  uint64_t offsetBytes, const void* /*cpuData*/, uint32_t /*dataSize*/) {
    // DX12: buffer is persistently mapped — GPU reads directly. cpuData unused.
    uint64_t gpuAddr = m_device->GetBufferGPUAddress(buffer);
    if (m_isComputeBound) {
        m_cmdList->SetComputeRootConstantBufferView(rootParamIndex, gpuAddr + offsetBytes);
    } else {
        m_cmdList->SetGraphicsRootConstantBufferView(rootParamIndex, gpuAddr + offsetBytes);
    }
}

void DX12CommandList::SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) {
    auto* heap = m_device->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = dx12::GetGPUStart(heap);
    gpuHandle.ptr += static_cast<UINT64>(descriptorIndex) *
        m_device->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if (m_isComputeBound) {
        m_cmdList->SetComputeRootDescriptorTable(rootParamIndex, gpuHandle);
    } else {
        m_cmdList->SetGraphicsRootDescriptorTable(rootParamIndex, gpuHandle);
    }
}

void DX12CommandList::SetFragmentTexture(uint32_t slot, RHITextureHandle texture) {
    // SM6.6 bindless path — textures accessed via ResourceDescriptorHeap[index],
    // no descriptor table copy needed.
    if (static_cast<uint8_t>(m_device->GetShaderModel()) >= static_cast<uint8_t>(ShaderModel::SM6_6)) {
        return;
    }

    if (slot >= kMaxFragmentTextures) {
        UZ_LOG(RHI, Warning, "SetFragmentTexture — slot {} exceeds max {}", slot, kMaxFragmentTextures);
        return;
    }

    if (m_fragmentTexStagingBase == UINT32_MAX) {
        UZ_LOG(RHI, Error, "SetFragmentTexture — staging block not allocated");
        return;
    }

    if (!texture.IsValid()) {
        UZ_LOG(RHI, Warning, "SetFragmentTexture — invalid texture handle at slot {}", slot);
        return;
    }

    auto* dx12Tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!dx12Tex || dx12Tex->srvIndex == UINT32_MAX) {
        UZ_LOG(RHI, Warning, "SetFragmentTexture — texture has no SRV (index={}, gen={})",
               texture.index, texture.generation);
        return;
    }

    // Copy the texture's SRV descriptor into the contiguous staging block
    auto& alloc = m_device->GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto srcHandle = alloc.CpuHandle(dx12Tex->srvIndex);
    auto dstHandle = alloc.CpuHandle(m_fragmentTexStagingBase + slot);

    m_device->GetDevice()->CopyDescriptorsSimple(
        1, dstHandle, srcHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Bind the staging block as root param 5 (SM5 SRV descriptor table)
    constexpr uint32_t kSM5SrvTableRootParam = 5;
    m_cmdList->SetGraphicsRootDescriptorTable(
        kSM5SrvTableRootParam, alloc.GpuHandle(m_fragmentTexStagingBase));
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw and Dispatch
// ─────────────────────────────────────────────────────────────────────────────

void DX12CommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                  uint32_t firstIndex, int32_t vertexOffset,
                                  uint32_t firstInstance) {
    m_cmdList->DrawIndexedInstanced(indexCount, instanceCount, firstIndex,
                                    vertexOffset, firstInstance);
}

void DX12CommandList::Draw(uint32_t vertexCount, uint32_t instanceCount,
                           uint32_t firstVertex, uint32_t firstInstance) {
    m_cmdList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void DX12CommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY,
                               uint32_t groupCountZ) {
    m_cmdList->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void DX12CommandList::DrawIndexedIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset,
                                           uint32_t drawCount, uint32_t argsStride) {
    if (!argsBuffer.IsValid()) {
        UZ_LOG(RHI, Warning, "DrawIndexedIndirect — invalid args buffer handle");
        return;
    }

    auto* buf = m_device->GetBufferPool().Get(argsBuffer.index, argsBuffer.generation);
    if (!buf || !buf->resource) {
        UZ_LOG(RHI, Warning, "DrawIndexedIndirect — could not resolve args buffer");
        return;
    }

    auto* sig = m_device->GetDrawIndexedIndirectSignature();
    if (!sig) {
        UZ_LOG(RHI, Error, "DrawIndexedIndirect — command signature not initialized");
        return;
    }

    // DX12 ExecuteIndirect ignores argsStride — it uses the ByteStride from the
    // command signature. The stride parameter exists for Vulkan compatibility.
    (void)argsStride;
    m_cmdList->ExecuteIndirect(sig, drawCount, buf->resource,
                                argsOffset, nullptr, 0);
}

void DX12CommandList::DispatchIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset) {
    if (!argsBuffer.IsValid()) {
        UZ_LOG(RHI, Warning, "DispatchIndirect — invalid args buffer handle");
        return;
    }

    auto* buf = m_device->GetBufferPool().Get(argsBuffer.index, argsBuffer.generation);
    if (!buf || !buf->resource) {
        UZ_LOG(RHI, Warning, "DispatchIndirect — could not resolve args buffer");
        return;
    }

    auto* sig = m_device->GetDispatchIndirectSignature();
    if (!sig) {
        UZ_LOG(RHI, Error, "DispatchIndirect — command signature not initialized");
        return;
    }

    m_cmdList->ExecuteIndirect(sig, 1, buf->resource, argsOffset, nullptr, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Barriers
// ─────────────────────────────────────────────────────────────────────────────

void DX12CommandList::ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) {
    UZ_PROFILE_FUNCTION();
    if (!barriers || count == 0) return;

    // Stack-allocate for typical batch sizes, heap-allocate for large batches
    constexpr uint32_t kStackMax = 16;
    D3D12_RESOURCE_BARRIER stackBarriers[kStackMax];
    std::vector<D3D12_RESOURCE_BARRIER> heapBarriers;

    D3D12_RESOURCE_BARRIER* d3dBarriers = (count <= kStackMax)
        ? stackBarriers
        : (heapBarriers.resize(count), heapBarriers.data());

    uint32_t validCount = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& b = barriers[i];
        ID3D12Resource* resource = nullptr;

        // Resolve the resource from either texture or buffer handle
        if (b.texture.IsValid()) {
            auto* tex = m_device->GetTexturePool().Get(b.texture.index, b.texture.generation);
            if (tex) resource = tex->resource;
        } else if (b.buffer.IsValid()) {
            auto* buf = m_device->GetBufferPool().Get(b.buffer.index, b.buffer.generation);
            if (buf) resource = buf->resource;
        }

        if (!resource) {
            UZ_LOG(RHI, Warning, "ResourceBarrier[{}] — could not resolve resource handle", i);
            continue;
        }

        d3dBarriers[validCount] = CD3DX12_RESOURCE_BARRIER::Transition(
            resource,
            ToD3D12ResourceState(b.stateBefore),
            ToD3D12ResourceState(b.stateAfter));
        ++validCount;
    }

    if (validCount > 0) {
        m_cmdList->ResourceBarrier(validCount, d3dBarriers);
    }
}

void DX12CommandList::TransitionBarrier(ID3D12Resource* resource,
                                        D3D12_RESOURCE_STATES before,
                                        D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        resource, before, after);
    m_cmdList->ResourceBarrier(1, &barrier);
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear Operations
// ─────────────────────────────────────────────────────────────────────────────

void DX12CommandList::ClearRenderTarget(RHITextureHandle texture, const float color[4]) {
    if (!texture.IsValid()) {
        UZ_LOG(RHI, Warning, "ClearRenderTarget — invalid texture handle");
        return;
    }

    auto* dx12Tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!dx12Tex || dx12Tex->rtvIndex == UINT32_MAX) {
        UZ_LOG(RHI, Warning, "ClearRenderTarget — texture has no RTV descriptor (index={}, gen={})",
               texture.index, texture.generation);
        return;
    }

    auto rtv = m_device->GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_RTV).CpuHandle(dx12Tex->rtvIndex);
    m_cmdList->ClearRenderTargetView(rtv, color, 0, nullptr);
}

void DX12CommandList::ClearDepthStencil(RHITextureHandle texture, float depth, uint8_t stencil) {
    if (!texture.IsValid()) {
        UZ_LOG(RHI, Warning, "ClearDepthStencil — invalid texture handle");
        return;
    }

    auto* dx12Tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!dx12Tex || dx12Tex->dsvIndex == UINT32_MAX) {
        UZ_LOG(RHI, Warning, "ClearDepthStencil — texture has no DSV descriptor (index={}, gen={})",
               texture.index, texture.generation);
        return;
    }

    auto dsv = m_device->GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_DSV).CpuHandle(dx12Tex->dsvIndex);
    m_cmdList->ClearDepthStencilView(
        dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        depth, stencil, 0, nullptr);
}

void DX12CommandList::ClearRTV(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const float color[4]) {
    m_cmdList->ClearRenderTargetView(rtv, color, 0, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Copy Operations
// ─────────────────────────────────────────────────────────────────────────────

void DX12CommandList::CopyBuffer(RHIBufferHandle src, uint64_t srcOffset,
                                 RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) {
    if (!src.IsValid() || !dst.IsValid()) {
        UZ_LOG(RHI, Warning, "CopyBuffer — invalid source or destination handle");
        return;
    }

    auto* srcBuf = m_device->GetBufferPool().Get(src.index, src.generation);
    auto* dstBuf = m_device->GetBufferPool().Get(dst.index, dst.generation);
    if (!srcBuf || !srcBuf->resource || !dstBuf || !dstBuf->resource) {
        UZ_LOG(RHI, Warning, "CopyBuffer — could not resolve buffer handles");
        return;
    }

    m_cmdList->CopyBufferRegion(dstBuf->resource, dstOffset,
                                srcBuf->resource, srcOffset, size);
}

void DX12CommandList::CopyTexture(RHITextureHandle src, RHITextureHandle dst) {
    if (!src.IsValid() || !dst.IsValid()) {
        UZ_LOG(RHI, Warning, "CopyTexture — invalid source or destination handle");
        return;
    }

    auto* srcTex = m_device->GetTexturePool().Get(src.index, src.generation);
    auto* dstTex = m_device->GetTexturePool().Get(dst.index, dst.generation);
    if (!srcTex || !srcTex->resource || !dstTex || !dstTex->resource) {
        UZ_LOG(RHI, Warning, "CopyTexture — could not resolve texture handles");
        return;
    }

    m_cmdList->CopyResource(dstTex->resource, srcTex->resource);
}

void DX12CommandList::CopyTextureRegion(RHITextureHandle src, uint32_t srcMip, uint32_t srcSlice,
                                         RHITextureHandle dst, uint32_t dstMip, uint32_t dstSlice) {
    if (!src.IsValid() || !dst.IsValid()) {
        UZ_LOG(RHI, Warning, "CopyTextureRegion — invalid source or destination handle");
        return;
    }

    auto* srcTex = m_device->GetTexturePool().Get(src.index, src.generation);
    auto* dstTex = m_device->GetTexturePool().Get(dst.index, dst.generation);
    if (!srcTex || !srcTex->resource || !dstTex || !dstTex->resource) {
        UZ_LOG(RHI, Warning, "CopyTextureRegion — could not resolve texture handles");
        return;
    }

    D3D12_RESOURCE_DESC srcDesc = srcTex->resource->GetDesc();
    D3D12_RESOURCE_DESC dstDesc = dstTex->resource->GetDesc();
    const uint32_t srcSubresource = D3D12CalcSubresource(srcMip, srcSlice, 0,
        srcDesc.MipLevels, static_cast<uint32_t>(srcDesc.DepthOrArraySize));
    const uint32_t dstSubresource = D3D12CalcSubresource(dstMip, dstSlice, 0,
        dstDesc.MipLevels, static_cast<uint32_t>(dstDesc.DepthOrArraySize));

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = srcTex->resource;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = srcSubresource;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = dstTex->resource;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = dstSubresource;

    m_cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
}

void DX12CommandList::CopyBufferToTexture(RHIBufferHandle srcBuffer, uint64_t srcOffset,
                                           uint32_t srcRowPitch,
                                           RHITextureHandle dstTexture,
                                           uint32_t mipLevel, uint32_t arraySlice) {
    UZ_PROFILE_FUNCTION();
    if (!srcBuffer.IsValid() || !dstTexture.IsValid()) {
        UZ_LOG(RHI, Warning, "CopyBufferToTexture — invalid source or destination handle");
        return;
    }

    auto* buf = m_device->GetBufferPool().Get(srcBuffer.index, srcBuffer.generation);
    auto* tex = m_device->GetTexturePool().Get(dstTexture.index, dstTexture.generation);
    if (!buf || !buf->resource || !tex || !tex->resource) {
        UZ_LOG(RHI, Warning, "CopyBufferToTexture — could not resolve handles");
        return;
    }

    // Query the required layout from DX12 for this subresource
    const uint32_t subresource = mipLevel + (arraySlice * tex->desc.mipLevels);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT64 totalBytes = 0;

    D3D12_RESOURCE_DESC resDesc = tex->resource->GetDesc();
    m_device->GetDevice()->GetCopyableFootprints(
        &resDesc, subresource, 1, srcOffset, &footprint, nullptr, nullptr, &totalBytes);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = tex->resource;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = subresource;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = buf->resource;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    m_cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
}

/// Maps RHIFormat to DXGI_FORMAT for copy operations.
static DXGI_FORMAT ToDXGIFormat(RHIFormat format) {
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

void DX12CommandList::CopyTextureToBuffer(RHITextureHandle srcTexture, uint32_t mipLevel,
                                           RHIBufferHandle dstBuffer, uint64_t dstOffset,
                                           uint32_t dstRowPitch, uint32_t arraySlice) {
    UZ_PROFILE_FUNCTION();
    if (!srcTexture.IsValid() || !dstBuffer.IsValid()) {
        UZ_LOG(RHI, Warning, "CopyTextureToBuffer — invalid source or destination handle");
        return;
    }

    auto* tex = m_device->GetTexturePool().Get(srcTexture.index, srcTexture.generation);
    auto* buf = m_device->GetBufferPool().Get(dstBuffer.index, dstBuffer.generation);
    if (!tex || !tex->resource || !buf || !buf->resource) {
        UZ_LOG(RHI, Warning, "CopyTextureToBuffer — could not resolve handles");
        return;
    }

    const uint32_t mipWidth  = std::max(1u, tex->desc.width >> mipLevel);
    const uint32_t mipHeight = std::max(1u, tex->desc.height >> mipLevel);

    // DX12 subresource index = mipLevel + arraySlice * mipLevels
    const uint32_t mipLevels = std::max(static_cast<uint16_t>(1), tex->desc.mipLevels);
    const uint32_t subresourceIndex = mipLevel + arraySlice * mipLevels;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = tex->resource;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = subresourceIndex;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = buf->resource;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint.Offset = dstOffset;
    dstLoc.PlacedFootprint.Footprint.Format = ToDXGIFormat(tex->desc.format);
    dstLoc.PlacedFootprint.Footprint.Width = mipWidth;
    dstLoc.PlacedFootprint.Footprint.Height = mipHeight;
    dstLoc.PlacedFootprint.Footprint.Depth = 1;
    dstLoc.PlacedFootprint.Footprint.RowPitch = dstRowPitch;

    m_cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Render Pass
// ─────────────────────────────────────────────────────────────────────────────

void DX12CommandList::BeginRenderPass(const RHIRenderPassDesc& desc) {
    UZ_PROFILE_FUNCTION();

    // Free any lingering temp RTVs from a previous pass (defensive)
    auto& rtvHeap = m_device->GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (uint32_t t = 0; t < m_tempRtvCount; ++t) {
        rtvHeap.Free(m_tempRtvIndices[t]);
    }
    m_tempRtvCount = 0;

    // Resolve RTV CPU handles for each color attachment
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[8] = {};
    uint32_t validRtvCount = 0;

    for (uint32_t i = 0; i < desc.colorAttachmentCount && i < 8; ++i) {
        const auto& att = desc.colorAttachments[i];
        if (!att.texture.IsValid()) {
            UZ_LOG(RHI, Warning, "BeginRenderPass — color attachment {} has invalid texture", i);
            continue;
        }

        auto* tex = m_device->GetTexturePool().Get(att.texture.index, att.texture.generation);
        if (!tex) {
            UZ_LOG(RHI, Warning, "BeginRenderPass — color attachment {} texture not found", i);
            continue;
        }

        // Fast path: use the default RTV when targeting slice 0, mip 0
        if (att.arraySlice == 0 && att.mipLevel == 0) {
            if (tex->rtvIndex == UINT32_MAX) {
                UZ_LOG(RHI, Warning, "BeginRenderPass — color attachment {} has no RTV", i);
                continue;
            }
            rtvHandles[validRtvCount] = rtvHeap.CpuHandle(tex->rtvIndex);
        } else {
            // Slow path: create a temporary per-slice/mip RTV on the fly.
            // This is needed for rendering into individual cubemap faces or
            // specific mip levels of array textures.
            uint32_t tempIdx = rtvHeap.Allocate();
            if (tempIdx == DescriptorAllocator::kInvalidIndex) {
                UZ_LOG(RHI, Error, "BeginRenderPass — failed to allocate temp RTV for slice={} mip={}",
                       att.arraySlice, att.mipLevel);
                continue;
            }

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = ToDxgiFormat(tex->desc.format);

            if (tex->desc.depthOrArraySize > 1) {
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                rtvDesc.Texture2DArray.MipSlice        = att.mipLevel;
                rtvDesc.Texture2DArray.FirstArraySlice  = att.arraySlice;
                rtvDesc.Texture2DArray.ArraySize        = 1;
                rtvDesc.Texture2DArray.PlaneSlice       = 0;
            } else {
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Texture2D.MipSlice   = att.mipLevel;
                rtvDesc.Texture2D.PlaneSlice = 0;
            }

            m_device->GetDevice()->CreateRenderTargetView(
                tex->resource, &rtvDesc, rtvHeap.CpuHandle(tempIdx));

            rtvHandles[validRtvCount] = rtvHeap.CpuHandle(tempIdx);

            // Track for cleanup in EndRenderPass
            if (m_tempRtvCount < kMaxTempRtvs) {
                m_tempRtvIndices[m_tempRtvCount++] = tempIdx;
            } else {
                UZ_LOG(RHI, Warning, "BeginRenderPass — exceeded max temp RTVs ({}), leaking descriptor",
                       kMaxTempRtvs);
            }
        }

        if (att.clear) {
            m_cmdList->ClearRenderTargetView(rtvHandles[validRtvCount], att.clearColor, 0, nullptr);
        }
        ++validRtvCount;
    }

    // Resolve optional DSV
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    const D3D12_CPU_DESCRIPTOR_HANDLE* dsvPtr = nullptr;

    if (desc.hasDepth && desc.depthAttachment.texture.IsValid()) {
        auto* depthTex = m_device->GetTexturePool().Get(
            desc.depthAttachment.texture.index, desc.depthAttachment.texture.generation);
        if (depthTex && depthTex->dsvIndex != UINT32_MAX) {
            dsvHandle = m_device->GetDescriptorAllocator(
                D3D12_DESCRIPTOR_HEAP_TYPE_DSV).CpuHandle(depthTex->dsvIndex);
            dsvPtr = &dsvHandle;

            if (desc.depthAttachment.clear) {
                m_cmdList->ClearDepthStencilView(
                    dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                    desc.depthAttachment.clearDepth, desc.depthAttachment.clearStencil,
                    0, nullptr);
            }
        }
    }

    m_cmdList->OMSetRenderTargets(validRtvCount, validRtvCount > 0 ? rtvHandles : nullptr,
                                  FALSE, dsvPtr);
}

void DX12CommandList::EndRenderPass() {
    // Free temporary per-slice/mip RTVs allocated during BeginRenderPass
    if (m_tempRtvCount > 0) {
        auto& rtvHeap = m_device->GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (uint32_t i = 0; i < m_tempRtvCount; ++i) {
            rtvHeap.Free(m_tempRtvIndices[i]);
        }
        m_tempRtvCount = 0;
    }
}

} // namespace uz
