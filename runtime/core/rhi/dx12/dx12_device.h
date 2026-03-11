#pragma once

/// DX12 backend implementation of IRHIDevice.
///
/// Owns the D3D12 device, DXGI factory/adapter, D3D12MA allocator,
/// descriptor heaps, and command queues. All DX12 headers are confined
/// to this file and dx12_device.cpp — nothing else in the engine
/// includes d3d12.h or dxgi1_6.h.

#include "../rhi.h"
#include "../rhi_backend.h"
#include "dx12_resources.h"
#include "dx12_descriptor_allocator.h"

#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include "dx12_guids.h"
#include <string>

// Forward-declare D3D12MA to avoid pulling the header into consumers
namespace D3D12MA { class Allocator; }

namespace uz {

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor Heap Capacity Constants
// ─────────────────────────────────────────────────────────────────────────────

/// SM6.6 bindless: one million descriptors for CBV/SRV/UAV (shader-visible).
static constexpr uint32_t kMaxCbvSrvUavDescriptors = 1'000'000;

/// Maximum sampler descriptors (shader-visible).
static constexpr uint32_t kMaxSamplerDescriptors = 2048;

/// Maximum render target view descriptors (non-shader-visible).
static constexpr uint32_t kMaxRtvDescriptors = 256;

/// Maximum depth-stencil view descriptors (non-shader-visible).
static constexpr uint32_t kMaxDsvDescriptors = 64;

// ─────────────────────────────────────────────────────────────────────────────
// DX12Device
// ─────────────────────────────────────────────────────────────────────────────

/// DX12 implementation of IRHIDevice.
///
/// Initializes the full D3D12 device stack: debug layer, DXGI factory,
/// adapter, device, D3D12MA allocator, descriptor heaps, and command queues.
/// Resource creation methods are stubbed until Task 2.3/2.4.
class DX12Device final : public IRHIDevice {
public:
    DX12Device() = default;
    ~DX12Device() override;

    /// Initializes the DX12 device stack. Returns false on failure.
    bool Init(const RHIDeviceDesc& desc);

    // ── IRHIDevice: Buffer operations ────────────────────────────────────
    RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc) override;
    void DestroyBuffer(RHIBufferHandle handle) override;

    // ── IRHIDevice: Texture operations ───────────────────────────────────
    RHITextureHandle CreateTexture(const RHITextureDesc& desc) override;
    void DestroyTexture(RHITextureHandle handle) override;

    // ── IRHIDevice: Shader operations ────────────────────────────────────
    RHIShaderHandle CreateShader(const RHIShaderDesc& desc) override;
    void DestroyShader(RHIShaderHandle handle) override;

    // ── IRHIDevice: Pipeline operations ──────────────────────────────────
    RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) override;
    void DestroyPipeline(RHIPipelineHandle handle) override;

    // ── IRHIDevice: Sampler operations ───────────────────────────────────
    RHISamplerHandle CreateSampler(const RHISamplerDesc& desc) override;
    void DestroySampler(RHISamplerHandle handle) override;

    // ── IRHIDevice: Swap chain operations ────────────────────────────────
    IRHISwapChain* CreateSwapChain(const RHISwapChainDesc& desc) override;
    void DestroySwapChain(IRHISwapChain* swapChain) override;

    // ── IRHIDevice: Fence operations ─────────────────────────────────────
    IRHIFence* CreateFence() override;
    void DestroyFence(IRHIFence* fence) override;

    // ── IRHIDevice: Command list operations ──────────────────────────────
    IRHICommandList* CreateCommandList(RHIQueueType queue) override;
    void DestroyCommandList(IRHICommandList* cmdList) override;

    // ── IRHIDevice: Synchronization ──────────────────────────────────────
    void WaitIdle() override;

    // ── IRHIDevice: Device queries ───────────────────────────────────────
    const char* GetAdapterName() const override;
    const char* GetFeatureLevel() const override;
    RHIMemoryBudget GetMemoryBudget() const override;

    // ── Accessors for sibling DX12 classes ───────────────────────────────

    /// Returns the raw ID3D12Device pointer (for other dx12/ files only).
    ID3D12Device* GetDevice() const { return m_device.Get(); }

    /// Returns the direct (graphics) command queue.
    ID3D12CommandQueue* GetDirectQueue() const { return m_directQueue.Get(); }

    /// Returns the copy command queue.
    ID3D12CommandQueue* GetCopyQueue() const { return m_copyQueue.Get(); }

    /// Returns the DXGI factory.
    IDXGIFactory6* GetFactory() const { return m_factory.Get(); }

    /// Returns the D3D12MA allocator.
    D3D12MA::Allocator* GetAllocator() const { return m_allocator; }

    /// Returns a descriptor heap by type.
    ID3D12DescriptorHeap* GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

    /// Returns the descriptor allocator for a heap type.
    DescriptorAllocator& GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type);

    /// Returns the buffer pool (for command list resource lookups).
    ResourcePool<DX12Buffer>& GetBufferPool() { return m_bufferPool; }
    ResourcePool<DX12Texture>& GetTexturePool() { return m_texturePool; }

private:
    bool EnableDebugLayer(bool gpuValidation);
    bool CreateFactory(bool validation);
    bool SelectAdapter(uint32_t adapterIndex);
    bool CreateDevice();
    bool CheckSM66Support();
    bool CreateAllocator();
    bool CreateDescriptorHeaps();
    bool CreateCommandQueues();
    bool EnableDRED();

    bool CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count,
                              bool shaderVisible, ComPtr<ID3D12DescriptorHeap>& outHeap);
    void InitDescriptorAllocators();

    // ── State ────────────────────────────────────────────────────────────

    ComPtr<IDXGIFactory6>          m_factory;
    ComPtr<IDXGIAdapter4>          m_adapter;
    ComPtr<ID3D12Device>           m_device;
    D3D12MA::Allocator*            m_allocator = nullptr;

    ComPtr<ID3D12CommandQueue>     m_directQueue;
    ComPtr<ID3D12CommandQueue>     m_copyQueue;

    ComPtr<ID3D12DescriptorHeap>   m_cbvSrvUavHeap;
    ComPtr<ID3D12DescriptorHeap>   m_samplerHeap;
    ComPtr<ID3D12DescriptorHeap>   m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap>   m_dsvHeap;

    /// Fence used exclusively by WaitIdle().
    ComPtr<ID3D12Fence>            m_idleFence;
    uint64_t                       m_idleFenceValue = 0;
    HANDLE                         m_idleFenceEvent = nullptr;

    // ── Descriptor Allocators ──────────────────────────────────────────
    DescriptorAllocator            m_srvUavAllocator;
    DescriptorAllocator            m_samplerAllocator;
    DescriptorAllocator            m_rtvAllocator;
    DescriptorAllocator            m_dsvAllocator;

    // ── Resource Pools ──────────────────────────────────────────────────
    ResourcePool<DX12Buffer>       m_bufferPool;
    ResourcePool<DX12Texture>      m_texturePool;
    ResourcePool<DX12Shader>       m_shaderPool;
    ResourcePool<DX12Sampler>      m_samplerPool;

    std::string                    m_adapterName;
    std::string                    m_featureLevelStr;
    D3D_FEATURE_LEVEL              m_featureLevel = D3D_FEATURE_LEVEL_12_0;
    bool                           m_validationEnabled = false;
};

} // namespace uz
