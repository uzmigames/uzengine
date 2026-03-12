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
/// Created via `CreateRHIDevice()`, destroyed via `DestroyRHIDevice()`.
/// The destructor flushes the GPU and releases all resources.

#include "../rhi.h"
#include "../rhi_backend.h"
#include "../rhi_config.h"
#include "dx11_resources.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <wrl/client.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <string>

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
    RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc) override;
    void DestroyBuffer(RHIBufferHandle handle) override;
    void* MapBuffer(RHIBufferHandle handle) override;
    uint32_t GetBufferSRVIndex(RHIBufferHandle handle) const override;
    uint64_t GetBufferGPUAddress(RHIBufferHandle handle) const override;

    // ── IRHIDevice: Texture operations ───────────────────────────────────
    RHITextureHandle CreateTexture(const RHITextureDesc& desc) override;
    void DestroyTexture(RHITextureHandle handle) override;
    uint32_t GetTextureSRVIndex(RHITextureHandle handle) const override;
    RHITextureDesc GetTextureDesc(RHITextureHandle handle) const override;

    // ── IRHIDevice: Shader operations ────────────────────────────────────
    RHIShaderHandle CreateShader(const RHIShaderDesc& desc) override;
    void DestroyShader(RHIShaderHandle handle) override;

    // ── IRHIDevice: Pipeline operations ──────────────────────────────────
    RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) override;
    void DestroyPipeline(RHIPipelineHandle handle) override;

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
    ShaderModel GetShaderModel() const override { return m_shaderModel; }

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
    uint32_t                       m_nextSrvIndex = 0;

    /// Monotonically increasing sampler index counter.
    uint32_t                       m_nextSamplerIndex = 0;

    ShaderModel                    m_shaderModel = ShaderModel::SM5_0;
    std::string                    m_adapterName;
    std::string                    m_featureLevelStr;
    D3D_FEATURE_LEVEL              m_featureLevel = D3D_FEATURE_LEVEL_11_0;
    bool                           m_validationEnabled = false;
};

} // namespace uz
