/// DX12 Command List Implementation
///
/// Records GPU commands (barriers, clears, draws) into a command allocator.
/// Uses triple-buffered allocators for frame pacing and SM6.6 bindless heap binding.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_command_list.h"
#include "dx12_device.h"
#include "d3dx12.h"

#include "uz_log.h"

namespace uz {

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

    UZ_LOG(RHI, Info, "DX12CommandList created, type={}", static_cast<int>(m_listType));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Recording Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void DX12CommandList::Begin() {
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
    HRESULT hr = m_cmdList->Close();
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CommandList::Close failed, HRESULT=0x{:08X}",
               static_cast<uint32_t>(hr));
    }
}

void DX12CommandList::Execute() {
    ID3D12CommandQueue* queue = nullptr;

    switch (m_listType) {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            queue = m_device->GetDirectQueue();
            break;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            queue = m_device->GetCopyQueue();
            break;
        default:
            // Compute queue not yet exposed by DX12Device
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

void DX12CommandList::SetPipeline(RHIPipelineHandle /*pipeline*/) {
    UZ_LOG(RHI, Warning, "SetPipeline not yet implemented — needs pipeline pool");
}

void DX12CommandList::SetVertexBuffer(uint32_t /*slot*/, RHIBufferHandle /*buffer*/, uint32_t /*offset*/) {
    UZ_LOG(RHI, Warning, "SetVertexBuffer not yet implemented — needs buffer pool");
}

void DX12CommandList::SetIndexBuffer(RHIBufferHandle /*buffer*/, RHIIndexFormat /*format*/, uint32_t /*offset*/) {
    UZ_LOG(RHI, Warning, "SetIndexBuffer not yet implemented — needs buffer pool");
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
    m_cmdList->SetGraphicsRoot32BitConstant(rootParamIndex, descriptorIndex, 0);
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

// ─────────────────────────────────────────────────────────────────────────────
// Barriers
// ─────────────────────────────────────────────────────────────────────────────

void DX12CommandList::ResourceBarrier(const RHIBarrierDesc* /*barriers*/, uint32_t /*count*/) {
    UZ_LOG(RHI, Warning, "ResourceBarrier (handle-based) not yet implemented — needs texture/buffer pool");
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

void DX12CommandList::ClearRenderTarget(RHITextureHandle /*texture*/, const float /*color*/[4]) {
    UZ_LOG(RHI, Warning, "ClearRenderTarget (handle-based) not yet implemented — needs texture pool with RTV descriptors");
}

void DX12CommandList::ClearDepthStencil(RHITextureHandle /*texture*/, float /*depth*/, uint8_t /*stencil*/) {
    UZ_LOG(RHI, Warning, "ClearDepthStencil not yet implemented — needs texture pool with DSV descriptors");
}

void DX12CommandList::ClearRTV(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const float color[4]) {
    m_cmdList->ClearRenderTargetView(rtv, color, 0, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Copy Operations
// ─────────────────────────────────────────────────────────────────────────────

void DX12CommandList::CopyBuffer(RHIBufferHandle /*src*/, uint64_t /*srcOffset*/,
                                 RHIBufferHandle /*dst*/, uint64_t /*dstOffset*/, uint64_t /*size*/) {
    UZ_LOG(RHI, Warning, "CopyBuffer not yet implemented — needs buffer pool");
}

void DX12CommandList::CopyTexture(RHITextureHandle /*src*/, RHITextureHandle /*dst*/) {
    UZ_LOG(RHI, Warning, "CopyTexture not yet implemented — needs texture pool");
}

// ─────────────────────────────────────────────────────────────────────────────
// Render Pass
// ─────────────────────────────────────────────────────────────────────────────

void DX12CommandList::BeginRenderPass(const RHIRenderPassDesc& /*desc*/) {
    UZ_LOG(RHI, Warning, "BeginRenderPass not yet implemented — needs texture pool for attachment resolution");
}

void DX12CommandList::EndRenderPass() {
    UZ_LOG(RHI, Warning, "EndRenderPass not yet implemented");
}

} // namespace uz
