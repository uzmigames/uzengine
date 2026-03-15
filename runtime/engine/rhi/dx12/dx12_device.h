#pragma once

/// DX12 backend implementation of `IRHIDevice`.
///
/// Owns the full D3D12 device stack: DXGI factory/adapter, D3D12 device,
/// D3D12MA allocator, descriptor heaps (CBV_SRV_UAV, Sampler, RTV, DSV),
/// command queues (Direct + Copy), and the global bindless root signature.
///
/// All DX12 headers are confined to this file and `dx12_device.cpp` — nothing
/// else in the engine includes `d3d12.h` or `dxgi1_6.h` directly.
///
/// # Thread Safety
/// Resource creation/destruction methods are NOT internally synchronized in
/// the current implementation. Callers must serialize access from multiple threads.
///
/// # Lifetime
/// Created via `CreateRHIDevice()`, destroyed via `DestroyRHIDevice()`.
/// The destructor calls `WaitIdle()` and reports live objects in debug builds.

#include "../rhi.h"
#include "../rhi_backend.h"
#include "../rhi_config.h"
#include "dx12_resources.h"
#include "dx12_descriptor_allocator.h"
#include "dx12_gpu_profiler.h"

#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include "dx12_guids.h"
#include <tracy/TracyD3D12.hpp>
#include <string>
#include <vector>

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

/// DX12 implementation of `IRHIDevice`.
///
/// Initializes the full D3D12 device stack: debug layer, DXGI factory,
/// adapter selection (high-performance preference), device creation (feature
/// level 12_0+), D3D12MA allocator, descriptor heaps, command queues,
/// and bindless root signature.
///
/// # Initialization Order
/// 1. Debug layer (optional)  2. DXGI factory  3. Adapter selection
/// 4. Device creation  5. Shader model negotiation  6. D3D12MA allocator
/// 7. Descriptor heaps  8. Command queues  9. Root signature  10. DRED (optional)
class DX12Device final : public IRHIDevice {
public:
    DX12Device() = default;
    ~DX12Device() override;

    /// Initializes the DX12 device stack from debug layer through root signature.
    ///
    /// # Arguments
    /// * `desc` — Device descriptor (backend, validation flags, adapter index).
    ///
    /// # Returns
    /// `true` on success, `false` on failure (logs detailed error via `UZ_LOG`).
    bool Init(const RHIDeviceDesc& desc);

    // ── IRHIDevice: Buffer operations ────────────────────────────────────
    RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc) override;
    void DestroyBuffer(RHIBufferHandle handle) override;
    void* MapBuffer(RHIBufferHandle handle) override;
    void UnmapBuffer(RHIBufferHandle handle) override;
    uint32_t GetBufferSRVIndex(RHIBufferHandle handle) const override;
    uint32_t GetBufferUAVIndex(RHIBufferHandle handle) const override;
    uint64_t GetBufferGPUAddress(RHIBufferHandle handle) const override;

    // ── IRHIDevice: Texture operations ───────────────────────────────────
    RHITextureHandle CreateTexture(const RHITextureDesc& desc) override;
    void DestroyTexture(RHITextureHandle handle) override;
    uint32_t GetTextureSRVIndex(RHITextureHandle handle) const override;
    uint32_t GetTextureUAVIndex(RHITextureHandle handle) const override;
    RHITextureDesc GetTextureDesc(RHITextureHandle handle) const override;

    // ── IRHIDevice: Shader operations ────────────────────────────────────
    RHIShaderHandle CreateShader(const RHIShaderDesc& desc) override;
    void DestroyShader(RHIShaderHandle handle) override;

    // ── IRHIDevice: Pipeline operations ──────────────────────────────────
    RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) override;
    RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineDesc& desc) override;
    void DestroyPipeline(RHIPipelineHandle handle) override;
    std::vector<uint8_t> GetPipelineCachedBlob(RHIPipelineHandle handle) override;

    // ── IRHIDevice: Copy queue ──────────────────────────────────────────
    uint64_t SignalCopyQueue() override;
    void GraphicsWaitForCopy(uint64_t value) override;

    // ── IRHIDevice: Compute queue ─────────────────────────────────────
    uint64_t SignalComputeQueue() override;
    void GraphicsWaitForCompute(uint64_t value) override;
    void ComputeWaitForGraphics(uint64_t value) override;

    // ── IRHIDevice: Sampler operations ───────────────────────────────────
    RHISamplerHandle CreateSampler(const RHISamplerDesc& desc) override;
    void DestroySampler(RHISamplerHandle handle) override;
    uint32_t GetSamplerIndex(RHISamplerHandle handle) const override;

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

    /// Returns the async compute command queue.
    ID3D12CommandQueue* GetComputeQueue() const { return m_computeQueue.Get(); }

    /// Returns the DXGI factory.
    IDXGIFactory6* GetFactory() const { return m_factory.Get(); }

    /// Returns the D3D12MA allocator.
    D3D12MA::Allocator* GetAllocator() const { return m_allocator; }

    /// Returns a descriptor heap by type.
    ID3D12DescriptorHeap* GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

    /// Returns the descriptor allocator for a heap type.
    DescriptorAllocator& GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type);

    /// Returns the buffer resource pool (for command list resource lookups).
    ResourcePool<DX12Buffer>& GetBufferPool() { return m_bufferPool; }

    /// Returns the texture resource pool (for command list resource lookups).
    ResourcePool<DX12Texture>& GetTexturePool() { return m_texturePool; }

    /// Returns the pipeline resource pool (for command list PSO binding).
    ResourcePool<DX12Pipeline>& GetPipelinePool() { return m_pipelinePool; }

    /// Returns the shader resource pool (for pipeline creation bytecode lookup).
    ResourcePool<DX12Shader>& GetShaderPool() { return m_shaderPool; }

    /// Returns the global bindless root signature (shared by all pipelines).
    ID3D12RootSignature* GetRootSignature() const { return m_rootSignature; }

    /// Returns the command signature for indirect indexed draw calls.
    ID3D12CommandSignature* GetDrawIndexedIndirectSignature() const { return m_drawIndexedIndirectSig.Get(); }

    /// Returns the command signature for indirect dispatch calls.
    ID3D12CommandSignature* GetDispatchIndirectSignature() const { return m_dispatchIndirectSig.Get(); }

    /// Returns the negotiated shader model for this device.
    ///
    /// Determined during device creation by probing GPU capabilities.
    /// SM6.6+ enables bindless resource access; SM5.x uses traditional binding.
    ShaderModel GetShaderModel() const override { return m_shaderModel; }
    RHIBackend GetBackend() const override { return RHIBackend::DX12; }

    /// Queries and logs DRED diagnostic data after device removal.
    ///
    /// Logs auto-breadcrumb nodes (last GPU operations before hang) and
    /// page fault info (faulting VA, existing/freed allocations).
    /// Call this when `Present()` returns `DXGI_ERROR_DEVICE_REMOVED`.
    void LogDeviceRemoved();

    /// Returns the GPU timestamp profiler for per-pass timing.
    ///
    /// # Returns
    /// Mutable reference to the device-owned `DX12GpuProfiler`.
    /// The profiler may be uninitialized if `Init()` failed — check before use.
    DX12GpuProfiler& GetGpuProfiler() { return m_gpuProfiler; }

    /// Returns the Tracy GPU profiling context for DX12.
    ///
    /// Used for GPU timestamp collection via Tracy. When `TRACY_ENABLE` is not
    /// defined, this returns `nullptr` (`TracyD3D12Ctx` is `void*`).
    TracyD3D12Ctx GetTracyGpuContext() const { return m_tracyD3D12Ctx; }

private:
    bool EnableDebugLayer(bool gpuValidation);
    bool CreateFactory(bool validation);
    bool SelectAdapter(uint32_t adapterIndex);
    bool CreateDevice();
    void NegotiateShaderModel(std::optional<ShaderModel> forceModel = std::nullopt);
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
    ComPtr<ID3D12Fence>            m_copyFence;
    uint64_t                       m_copyFenceValue = 0;

    ComPtr<ID3D12CommandQueue>     m_computeQueue;
    ComPtr<ID3D12Fence>            m_computeFence;
    uint64_t                       m_computeFenceValue = 0;

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
    ResourcePool<DX12Pipeline>     m_pipelinePool;

    /// Global bindless root signature — shared by all graphics pipelines.
    ID3D12RootSignature*           m_rootSignature = nullptr;

    /// Command signature for ExecuteIndirect with DrawIndexedInstanced args.
    ComPtr<ID3D12CommandSignature> m_drawIndexedIndirectSig;

    /// Command signature for ExecuteIndirect with Dispatch args.
    ComPtr<ID3D12CommandSignature> m_dispatchIndirectSig;

    DX12GpuProfiler                m_gpuProfiler;
    TracyD3D12Ctx                  m_tracyD3D12Ctx = nullptr;

    ShaderModel                    m_shaderModel = ShaderModel::SM5_0;
    std::string                    m_adapterName;
    std::string                    m_featureLevelStr;
    D3D_FEATURE_LEVEL              m_featureLevel = D3D_FEATURE_LEVEL_12_0;
    bool                           m_validationEnabled = false;
};

} // namespace uz
