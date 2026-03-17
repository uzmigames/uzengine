#pragma once

/// DX11 backend implementation of `IRHIDevice`.
///
/// Owns the D3D11 device stack: DXGI factory/adapter, D3D11 device and
/// immediate context, and resource pools for buffers, textures, shaders,
/// samplers, and pipelines.
///
/// Unlike DX12, DX11 has no explicit memory allocator (D3D12MA), no descriptor
/// heaps, no root signatures, and no command queues. State management is
/// implicit and the driver handles resource transitions automatically.
///
/// # Thread Safety
/// Resource creation/destruction methods are NOT internally synchronized in
/// the current implementation. Callers must serialize access from multiple threads.
///
/// # Lifetime
/// Created via `CreateRHIDevice()` which returns `std::unique_ptr<IRHIDevice>`.
/// The destructor flushes the GPU and releases all resources.

#include "../rhi.h"
#include "../rhi_backend.h"
#include "../rhi_config.h"
#include "dx11_resources.h"
#include "dx11_gpu_profiler.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <wrl/client.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <string>
#include <vector>

#include <tracy/TracyD3D11.hpp>

namespace uz {

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────────────────────
// DX11Device
// ─────────────────────────────────────────────────────────────────────────────

/// DX11 implementation of `IRHIDevice`.
///
/// Initializes the D3D11.1 device stack: debug layer, DXGI factory,
/// adapter selection (high-performance preference), device creation,
/// and resource pools.
///
/// # Initialization Order
/// 1. Debug layer (optional)  2. DXGI factory  3. Adapter selection
/// 4. Device creation  5. Query ID3D11Device1/Context1 interfaces
class DX11Device final : public IRHIDevice {
public:
    DX11Device() = default;
    ~DX11Device() override;

    /// Initializes the DX11 device stack.
    ///
    /// # Arguments
    /// * `desc` — Device descriptor (backend, validation flags, adapter index).
    ///
    /// # Returns
    /// `true` on success, `false` on failure (logs detailed error via `UZ_LOG`).
    bool Init(const RHIDeviceDesc& desc);

    // ── IRHIDevice: Buffer operations ────────────────────────────────────

    /// Creates a GPU buffer on the DX11 device. See `IRHIDevice::CreateBuffer`.
    RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc) override;

    /// Destroys a DX11 buffer and releases its ID3D11Buffer resource.
    void DestroyBuffer(RHIBufferHandle handle) override;

    /// Returns a transiently mapped pointer for DX11 Upload/Readback buffers.
    /// Unlike DX12, DX11 requires `UnmapBuffer()` after writing.
    void* MapBuffer(RHIBufferHandle handle) override;

    /// Unmaps a DX11 buffer via `ID3D11DeviceContext::Unmap`. Required after writing.
    void UnmapBuffer(RHIBufferHandle handle) override;

    /// Returns the logical SRV index assigned at buffer creation time.
    uint32_t GetBufferSRVIndex(RHIBufferHandle handle) const override;

    /// Returns the logical UAV index assigned at buffer creation time.
    uint32_t GetBufferUAVIndex(RHIBufferHandle handle) const override;

    /// Returns 0 — DX11 does not support GPU virtual addresses.
    uint64_t GetBufferGPUAddress(RHIBufferHandle handle) const override;

    // ── IRHIDevice: Texture operations ───────────────────────────────────

    /// Creates a GPU texture on the DX11 device. See `IRHIDevice::CreateTexture`.
    RHITextureHandle CreateTexture(const RHITextureDesc& desc) override;

    /// Destroys a DX11 texture and releases its ID3D11Texture2D and view resources.
    void DestroyTexture(RHITextureHandle handle) override;

    /// Returns the logical SRV index assigned at texture creation time.
    uint32_t GetTextureSRVIndex(RHITextureHandle handle) const override;

    /// Returns the logical UAV index assigned at texture creation time.
    uint32_t GetTextureUAVIndex(RHITextureHandle handle) const override;

    /// Returns the creation descriptor for a DX11 texture. See `IRHIDevice::GetTextureDesc`.
    RHITextureDesc GetTextureDesc(RHITextureHandle handle) const override;

    // ── IRHIDevice: Shader operations ────────────────────────────────────

    /// Creates a shader module from DXBC/DXIL bytecode. See `IRHIDevice::CreateShader`.
    RHIShaderHandle CreateShader(const RHIShaderDesc& desc) override;

    /// Destroys a DX11 shader module and frees its bytecode storage.
    void DestroyShader(RHIShaderHandle handle) override;

    // ── IRHIDevice: Pipeline operations ──────────────────────────────────

    /// Creates a DX11 graphics pipeline (rasterizer + blend + depth-stencil + input layout).
    RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) override;

    /// Creates a DX11 compute pipeline (compute shader only).
    RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineDesc& desc) override;

    /// Destroys a DX11 pipeline and its state objects. See `IRHIDevice::DestroyPipeline`.
    void DestroyPipeline(RHIPipelineHandle handle) override;

    /// Returns empty — DX11 does not support pipeline cached blobs.
    std::vector<uint8_t> GetPipelineCachedBlob(RHIPipelineHandle handle) override;

    // ── IRHIDevice: Sampler operations ───────────────────────────────────

    /// Creates a DX11 sampler state via `ID3D11Device::CreateSamplerState`.
    RHISamplerHandle CreateSampler(const RHISamplerDesc& desc) override;

    /// Destroys a DX11 sampler state. See `IRHIDevice::DestroySampler`.
    void DestroySampler(RHISamplerHandle handle) override;

    /// Returns the logical sampler index assigned at creation time.
    uint32_t GetSamplerIndex(RHISamplerHandle handle) const override;

    // ── IRHIDevice: Swap chain operations ────────────────────────────────

    /// Creates a DXGI swap chain for DX11. See `IRHIDevice::CreateSwapChain`.
    IRHISwapChain* CreateSwapChain(const RHISwapChainDesc& desc) override;

    /// Destroys a DX11 swap chain. See `IRHIDevice::DestroySwapChain`.
    void DestroySwapChain(IRHISwapChain* swapChain) override;

    // ── IRHIDevice: Fence operations ─────────────────────────────────────

    /// Creates a DX11 fence (emulated via ID3D11Query event queries).
    IRHIFence* CreateFence() override;

    /// Destroys a DX11 fence. See `IRHIDevice::DestroyFence`.
    void DestroyFence(IRHIFence* fence) override;

    // ── IRHIDevice: Command list operations ──────────────────────────────

    /// Creates a DX11 command list wrapping the immediate context.
    IRHICommandList* CreateCommandList(RHIQueueType queue) override;

    /// Destroys a DX11 command list. See `IRHIDevice::DestroyCommandList`.
    void DestroyCommandList(IRHICommandList* cmdList) override;

    // ── IRHIDevice: Synchronization ──────────────────────────────────────

    /// Flushes the DX11 immediate context and blocks until the GPU is idle.
    void WaitIdle() override;

    // ── IRHIDevice: Device queries ───────────────────────────────────────

    /// Returns the DXGI adapter description string (e.g., "NVIDIA GeForce RTX 4090").
    const char* GetAdapterName() const override;

    /// Returns the D3D feature level string (e.g., "D3D_FEATURE_LEVEL_11_1").
    const char* GetFeatureLevel() const override;

    /// Queries DXGI adapter memory via `IDXGIAdapter3::QueryVideoMemoryInfo`.
    RHIMemoryBudget GetMemoryBudget() const override;

    /// Returns the DX11 shader model (always SM5_0). See `IRHIDevice::GetShaderModel`.
    ShaderModel GetShaderModel() const override { return m_shaderModel; }

    /// Returns `RHIBackend::DX11`. See `IRHIDevice::GetBackend`.
    RHIBackend GetBackend() const override { return RHIBackend::DX11; }

    // ── Accessors for sibling DX11 classes ───────────────────────────────

    /// Returns the raw ID3D11Device1 pointer (for other dx11/ files only).
    ID3D11Device1* GetDevice() const { return m_device.Get(); }

    /// Returns the immediate device context.
    ID3D11DeviceContext1* GetImmediateContext() const { return m_immediateContext.Get(); }

    /// Returns the DXGI factory.
    IDXGIFactory2* GetFactory() const { return m_factory.Get(); }

    /// Returns the buffer resource pool (for command list resource lookups).
    ResourcePool<DX11Buffer>& GetBufferPool() { return m_bufferPool; }

    /// Returns the texture resource pool (for command list resource lookups).
    ResourcePool<DX11Texture>& GetTexturePool() { return m_texturePool; }

    /// Returns the pipeline resource pool (for command list PSO binding).
    ResourcePool<DX11Pipeline>& GetPipelinePool() { return m_pipelinePool; }

    /// Returns the shader resource pool (for pipeline creation bytecode lookup).
    ResourcePool<DX11Shader>& GetShaderPool() { return m_shaderPool; }

    /// Returns the sampler resource pool.
    ResourcePool<DX11Sampler>& GetSamplerPool() { return m_samplerPool; }

    /// Finds a texture SRV by its logical SRV index (assigned at creation).
    ///
    /// Iterates the texture pool to find a texture whose `srvIndex` matches.
    /// Used by DX11CommandList::SetDescriptorTable to bind SRVs to shader slots.
    ///
    /// # Returns
    /// The ID3D11ShaderResourceView pointer, or `nullptr` if not found.
    ID3D11ShaderResourceView* FindTextureSRVByIndex(uint32_t srvIndex);

    /// Finds a texture UAV by its pool index (returned by GetTextureUAVIndex).
    ///
    /// Iterates the texture pool to find a texture whose pool index matches.
    /// Used by DX11CommandList::SetDescriptorTable to bind UAVs for compute dispatch.
    ///
    /// # Returns
    /// The ID3D11UnorderedAccessView pointer, or `nullptr` if not found.
    ID3D11UnorderedAccessView* FindTextureUAVByPoolIndex(uint32_t poolIndex);

    /// Finds a buffer UAV by its pool index (returned by GetBufferUAVIndex).
    ///
    /// # Returns
    /// The ID3D11UnorderedAccessView pointer, or `nullptr` if not found.
    ID3D11UnorderedAccessView* FindBufferUAVByPoolIndex(uint32_t poolIndex);

    /// Finds a sampler by its logical sampler index (assigned at creation).
    ///
    /// # Returns
    /// The ID3D11SamplerState pointer, or `nullptr` if not found.
    ID3D11SamplerState* FindSamplerByIndex(uint32_t samplerIndex);

    /// Returns the DX11 GPU timestamp profiler.
    ///
    /// Used for per-pass GPU timing alongside Tracy's frame-level profiling.
    DX11GpuProfiler& GetGpuProfiler() { return m_gpuProfiler; }

    /// Reads all pending ID3D11InfoQueue messages and logs them via UZ_LOG.
    ///
    /// Call after Present() or any suspect operation to surface DX11 debug layer
    /// warnings and errors. No-op if the debug layer is not active.
    void FlushDebugMessages();

    /// Returns the Tracy GPU profiling context for DX11.
    ///
    /// Used by `DX11SwapChain::Present()` to collect GPU timestamps each frame.
    /// When `TRACY_ENABLE` is not defined, this returns `nullptr` (`TracyD3D11Ctx`
    /// is `void*` and all Tracy macros are no-ops).
    TracyD3D11Ctx GetTracyGpuContext() const { return m_tracyD3D11Ctx; }

    /// Returns the default linear-wrap sampler (matches DX12 static sampler s0).
    ID3D11SamplerState* GetDefaultSampler() const { return m_defaultSampler; }

    /// Returns the default shadow comparison sampler (matches DX12 static sampler s1).
    ID3D11SamplerState* GetShadowComparisonSampler() const { return m_shadowComparisonSampler; }

private:
    bool EnableDebugLayer();
    bool CreateFactory();
    bool SelectAdapter(uint32_t adapterIndex);
    bool CreateDevice();

    // ── State ────────────────────────────────────────────────────────────

    ComPtr<IDXGIFactory2>          m_factory;
    ComPtr<IDXGIAdapter1>          m_adapter;
    ComPtr<IDXGIAdapter3>          m_adapter3;    ///< For memory budget queries
    ComPtr<ID3D11Device1>          m_device;
    ComPtr<ID3D11DeviceContext1>   m_immediateContext;

    // ── Resource Pools ──────────────────────────────────────────────────
    ResourcePool<DX11Buffer>       m_bufferPool;
    ResourcePool<DX11Texture>      m_texturePool;
    ResourcePool<DX11Shader>       m_shaderPool;
    ResourcePool<DX11Sampler>      m_samplerPool;
    ResourcePool<DX11Pipeline>     m_pipelinePool;

    /// Monotonically increasing SRV index counter for logical descriptor binding.
    uint32_t                       m_nextSrvIndex = 1; ///< Starts at 1: index 0 means "no texture" in shaders.

    /// Monotonically increasing sampler index counter.
    uint32_t                       m_nextSamplerIndex = 0;

    ShaderModel                    m_shaderModel = ShaderModel::SM5_0;
    std::string                    m_adapterName;
    std::string                    m_featureLevelStr;
    D3D_FEATURE_LEVEL              m_featureLevel = D3D_FEATURE_LEVEL_11_0;
    bool                           m_validationEnabled = false;

    /// Per-pass GPU timestamp profiler (individual query objects, not Tracy).
    DX11GpuProfiler                m_gpuProfiler;

    /// Tracy GPU profiling context (no-op `void*` when `TRACY_ENABLE` is off).
    TracyD3D11Ctx                  m_tracyD3D11Ctx = nullptr;

    /// Default linear-wrap sampler (equivalent to DX12 static sampler s0).
    ID3D11SamplerState*            m_defaultSampler = nullptr;

    /// Shadow PCF comparison sampler (equivalent to DX12 static sampler s1).
    ID3D11SamplerState*            m_shadowComparisonSampler = nullptr;

    /// Root signature layout mirroring DX12 root parameter → HLSL register mapping.
    /// Built during Init() so DX11 command list can translate rootParamIndex to
    /// the correct HLSL register for VSSetConstantBuffers/PSSetConstantBuffers.
    RHIRootSignatureDesc           m_rootLayout;

public:
    /// Returns the root signature layout for translating root param indices
    /// to HLSL register numbers.
    const RHIRootSignatureDesc& GetRootLayout() const { return m_rootLayout; }
};

} // namespace uz
