#pragma once

/// DemoMockDevice — Shared headless mock RHI device for VFX demos.
///
/// Implements all IRHIDevice pure virtuals with no-op stubs that return
/// valid dummy handles. Used by headless demos that test CPU-side VFX
/// logic without a real GPU device.
///
/// # Usage
///   #include "mock_device.h"
///   DemoMockDevice device;
///   vfxSystem.Initialize(&device, budgetMax);

#include "engine/rhi/rhi.h"
#include "engine/rhi/rhi_config.h"
#include "engine/rhi/rhi_backend.h"
#include "engine/rhi/rhi_gpu_profiler_types.h"

#include <cstdint>
#include <vector>

namespace uz {

class DemoMockDevice final : public IRHIDevice {
public:
    RHIBufferHandle   CreateBuffer(const RHIBufferDesc&) override { return {++m_nextIdx, 1u}; }
    void              DestroyBuffer(RHIBufferHandle) override {}
    RHITextureHandle  CreateTexture(const RHITextureDesc&) override { return {++m_nextIdx, 1u}; }
    void              DestroyTexture(RHITextureHandle) override {}
    void*             MapBuffer(RHIBufferHandle) override { return nullptr; }
    void              UnmapBuffer(RHIBufferHandle) override {}
    uint32_t          GetBufferSRVIndex(RHIBufferHandle) const override { return UINT32_MAX; }
    uint32_t          GetBufferUAVIndex(RHIBufferHandle) const override { return UINT32_MAX; }
    uint64_t          GetBufferGPUAddress(RHIBufferHandle) const override { return 0; }
    uint32_t          GetTextureSRVIndex(RHITextureHandle) const override { return UINT32_MAX; }
    uint32_t          GetTextureUAVIndex(RHITextureHandle) const override { return UINT32_MAX; }
    RHITextureDesc    GetTextureDesc(RHITextureHandle) const override { return {}; }
    RHIShaderHandle   CreateShader(const RHIShaderDesc&) override { return {0, 0}; }
    void              DestroyShader(RHIShaderHandle) override {}
    RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineDesc&) override { return {0, 0}; }
    RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineDesc&) override { return {0, 0}; }
    void              DestroyPipeline(RHIPipelineHandle) override {}
    std::vector<uint8_t> GetPipelineCachedBlob(RHIPipelineHandle) override { return {}; }
    RHISamplerHandle  CreateSampler(const RHISamplerDesc&) override { return {0, 0}; }
    void              DestroySampler(RHISamplerHandle) override {}
    uint32_t          GetSamplerIndex(RHISamplerHandle) const override { return UINT32_MAX; }
    IRHISwapChain*    CreateSwapChain(const RHISwapChainDesc&) override { return nullptr; }
    void              DestroySwapChain(IRHISwapChain*) override {}
    IRHIFence*        CreateFence() override { return nullptr; }
    void              DestroyFence(IRHIFence*) override {}
    IRHICommandList*  CreateCommandList(RHIQueueType) override { return nullptr; }
    void              DestroyCommandList(IRHICommandList*) override {}
    void              WaitIdle() override {}
    const char*       GetAdapterName() const override { return "DemoMockDevice"; }
    const char*       GetFeatureLevel() const override { return "Mock"; }
    RHIMemoryBudget   GetMemoryBudget() const override { return {}; }
    ShaderModel       GetShaderModel() const override { return ShaderModel::SM5_0; }
    RHIBackend        GetBackend() const override { return RHIBackend::Null; }
    // GPU profiler uses default no-op implementations from IRHIDevice.

private:
    uint32_t m_nextIdx = 0;
};

} // namespace uz
