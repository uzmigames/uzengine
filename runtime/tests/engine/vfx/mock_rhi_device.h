#pragma once

/// Mock RHI device for VFX unit tests.
///
/// Returns incrementing generational handles for every CreateBuffer call.
/// Tracks call counts so tests can assert how many buffers were created and
/// destroyed. No GPU hardware is needed.

#include "rhi.h"

#include <atomic>
#include <cstdint>

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// MockRHIDevice
// ─────────────────────────────────────────────────────────────────────────────

/// Minimal IRHIDevice implementation for CPU-only unit tests.
///
/// Every CreateBuffer() call returns a unique, valid handle with generation=1.
/// DestroyBuffer() is a counted no-op — it records the call but does nothing.
/// All other virtual methods return safe defaults.
class MockRHIDevice final : public IRHIDevice {
public:
    // -- Counters (inspectable by tests) --------------------------------------

    /// Number of successful CreateBuffer calls.
    std::atomic<uint32_t> buffersCreated{0};

    /// Number of DestroyBuffer calls.
    std::atomic<uint32_t> buffersDestroyed{0};

    /// Number of successful CreateTexture calls.
    std::atomic<uint32_t> texturesCreated{0};

    /// Number of DestroyTexture calls.
    std::atomic<uint32_t> texturesDestroyed{0};

    // -- Control knobs --------------------------------------------------------

    /// When true, CreateBuffer returns an invalid handle (simulates OOM).
    bool failNextBuffer{false};

    // -- Buffer interface -----------------------------------------------------

    RHIBufferHandle CreateBuffer(const RHIBufferDesc&) override {
        ++buffersCreated;
        if (failNextBuffer) {
            failNextBuffer = false;
            return RHIBufferHandle::Invalid();
        }
        const uint32_t idx = ++bufferIndex_;
        return {idx, 1u};
    }

    void DestroyBuffer(RHIBufferHandle) override {
        ++buffersDestroyed;
    }

    // -- Texture interface (stubs) --------------------------------------------

    RHITextureHandle CreateTexture(const RHITextureDesc&) override {
        const uint32_t idx = ++textureIndex_;
        ++texturesCreated;
        return {idx, 1u};
    }

    void DestroyTexture(RHITextureHandle) override {
        ++texturesDestroyed;
    }

    // -- Remaining pure-virtual stubs (all no-ops / safe defaults) -----------

    void*    MapBuffer(RHIBufferHandle) override                          { return nullptr; }
    void     UnmapBuffer(RHIBufferHandle) override                        {}
    uint32_t GetBufferSRVIndex(RHIBufferHandle) const override            { return UINT32_MAX; }
    uint32_t GetBufferUAVIndex(RHIBufferHandle) const override            { return UINT32_MAX; }
    uint64_t GetBufferGPUAddress(RHIBufferHandle) const override          { return 0; }
    uint32_t GetTextureSRVIndex(RHITextureHandle) const override          { return UINT32_MAX; }
    uint32_t GetTextureUAVIndex(RHITextureHandle) const override          { return UINT32_MAX; }
    RHITextureDesc GetTextureDesc(RHITextureHandle) const override        { return {}; }

    RHIShaderHandle   CreateShader(const RHIShaderDesc&) override                 { return {0, 0}; }
    void              DestroyShader(RHIShaderHandle) override                     {}
    RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineDesc&) override { return {0, 0}; }
    RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineDesc&) override  { return {0, 0}; }
    void              DestroyPipeline(RHIPipelineHandle) override                 {}
    std::vector<uint8_t> GetPipelineCachedBlob(RHIPipelineHandle) override        { return {}; }

    RHISamplerHandle CreateSampler(const RHISamplerDesc&) override                { return {0, 0}; }
    void             DestroySampler(RHISamplerHandle) override                    {}
    uint32_t         GetSamplerIndex(RHISamplerHandle) const override             { return UINT32_MAX; }

    IRHISwapChain*   CreateSwapChain(const RHISwapChainDesc&) override            { return nullptr; }
    void             DestroySwapChain(IRHISwapChain*) override                    {}

    IRHIFence*       CreateFence() override                                       { return nullptr; }
    void             DestroyFence(IRHIFence*) override                            {}

    IRHICommandList* CreateCommandList(RHIQueueType) override                     { return nullptr; }
    void             DestroyCommandList(IRHICommandList*) override                {}

    void             WaitIdle() override                                          {}
    const char*      GetAdapterName() const override                              { return "MockRHIDevice"; }
    const char*      GetFeatureLevel() const override                             { return "Mock"; }
    RHIMemoryBudget  GetMemoryBudget() const override                             { return {}; }
    ShaderModel      GetShaderModel() const override                              { return static_cast<ShaderModel>(0); }
    RHIBackend       GetBackend() const override                                  { return static_cast<RHIBackend>(0); }

private:
    std::atomic<uint32_t> bufferIndex_{0};
    std::atomic<uint32_t> textureIndex_{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// FailingMockRHIDevice
// ─────────────────────────────────────────────────────────────────────────────

/// An IRHIDevice that always returns invalid handles from CreateBuffer.
///
/// Used to test GPU allocation failure paths in GPUParticleBuffer and
/// ParticleEmitter.
class FailingMockRHIDevice final : public IRHIDevice {
public:
    RHIBufferHandle CreateBuffer(const RHIBufferDesc&) override {
        return RHIBufferHandle::Invalid();
    }
    void DestroyBuffer(RHIBufferHandle) override {}

    RHITextureHandle  CreateTexture(const RHITextureDesc&) override           { return {0, 0}; }
    void              DestroyTexture(RHITextureHandle) override               {}
    void*             MapBuffer(RHIBufferHandle) override                     { return nullptr; }
    void              UnmapBuffer(RHIBufferHandle) override                   {}
    uint32_t          GetBufferSRVIndex(RHIBufferHandle) const override       { return UINT32_MAX; }
    uint32_t          GetBufferUAVIndex(RHIBufferHandle) const override       { return UINT32_MAX; }
    uint64_t          GetBufferGPUAddress(RHIBufferHandle) const override     { return 0; }
    uint32_t          GetTextureSRVIndex(RHITextureHandle) const override     { return UINT32_MAX; }
    uint32_t          GetTextureUAVIndex(RHITextureHandle) const override     { return UINT32_MAX; }
    RHITextureDesc    GetTextureDesc(RHITextureHandle) const override         { return {}; }
    RHIShaderHandle   CreateShader(const RHIShaderDesc&) override             { return {0, 0}; }
    void              DestroyShader(RHIShaderHandle) override                 {}
    RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineDesc&) override { return {0, 0}; }
    RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineDesc&) override  { return {0, 0}; }
    void              DestroyPipeline(RHIPipelineHandle) override             {}
    std::vector<uint8_t> GetPipelineCachedBlob(RHIPipelineHandle) override   { return {}; }
    RHISamplerHandle  CreateSampler(const RHISamplerDesc&) override           { return {0, 0}; }
    void              DestroySampler(RHISamplerHandle) override               {}
    uint32_t          GetSamplerIndex(RHISamplerHandle) const override        { return UINT32_MAX; }
    IRHISwapChain*    CreateSwapChain(const RHISwapChainDesc&) override       { return nullptr; }
    void              DestroySwapChain(IRHISwapChain*) override               {}
    IRHIFence*        CreateFence() override                                  { return nullptr; }
    void              DestroyFence(IRHIFence*) override                       {}
    IRHICommandList*  CreateCommandList(RHIQueueType) override                { return nullptr; }
    void              DestroyCommandList(IRHICommandList*) override           {}
    void              WaitIdle() override                                     {}
    const char*       GetAdapterName() const override                         { return "FailingMockRHIDevice"; }
    const char*       GetFeatureLevel() const override                        { return "Mock"; }
    RHIMemoryBudget   GetMemoryBudget() const override                        { return {}; }
    ShaderModel       GetShaderModel() const override                         { return static_cast<ShaderModel>(0); }
    RHIBackend        GetBackend() const override                             { return static_cast<RHIBackend>(0); }
};

}  // namespace uz::test
