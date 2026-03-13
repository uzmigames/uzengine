#pragma once

/// Metal backend implementation of `IRHIDevice`.
///
/// Owns the Metal device stack: MTLDevice, command queues, argument buffers
/// (Metal's bindless descriptor system), and resource pools. Uses Objective-C
/// bridging in the .cpp implementation — all forward-declared MTL:: types are
/// cast via `__bridge` in the implementation file.
///
/// # Initialization Order
/// 1. MTLCreateSystemDefaultDevice()
/// 2. GPU family detection (Apple/Mac families)
/// 3. ShaderModel mapping from GPU family
/// 4. Argument buffer tier detection
/// 5. Command queue creation (graphics + transfer)
/// 6. Argument buffer creation (texture + sampler)
///
/// # Metal-Specific Design
/// - No VMA — Metal manages GPU memory internally via storage modes.
/// - No descriptor pool — Metal uses argument buffers (tier 2) for bindless.
/// - Rasterizer/depth-stencil state is set at draw time, not baked into PSOs.
/// - Metal handles hazard tracking automatically within a single command encoder.
///
/// # Thread Safety
/// Resource creation/destruction methods are NOT internally synchronized.
/// Callers must serialize access from multiple threads.
///
/// # Lifetime
/// Created via `CreateRHIDevice()`, destroyed via `DestroyRHIDevice()`.
/// The destructor flushes the GPU and releases all resources.

#include "../rhi.h"
#include "../rhi_backend.h"
#include "../rhi_config.h"
#include "metal_resources.h"

#include <memory>
#include <string>
#include <vector>

// Forward declarations for Metal types (avoids Metal framework dependency)
namespace MTL {
    class Device;
    class CommandQueue;
    class Buffer;
} // namespace MTL

namespace uz {

class MetalGpuProfiler;

// ─────────────────────────────────────────────────────────────────────────────
// MetalDevice
// ─────────────────────────────────────────────────────────────────────────────

/// Metal implementation of `IRHIDevice`.
///
/// Initializes the Metal device stack with argument buffer tier 2 for bindless
/// resource access. Push constants are emulated via a small constant buffer
/// updated per draw call (128 bytes, matching DX12/Vulkan root constant space).
class MetalDevice final : public IRHIDevice {
public:
    MetalDevice();
    ~MetalDevice() override;

    /// Initializes the Metal device stack.
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
    RHIBackend GetBackend() const override { return RHIBackend::Metal; }

    // ── Accessors for sibling Metal classes ──────────────────────────────

    /// Returns the native MTL::Device pointer (cast via __bridge in Metal code).
    MTL::Device* GetMTLDevice() const { return m_device; }

    /// Returns the primary graphics command queue.
    MTL::CommandQueue* GetGraphicsQueue() const { return m_graphicsQueue; }

    /// Returns the dedicated transfer command queue.
    MTL::CommandQueue* GetTransferQueue() const { return m_transferQueue; }

    /// Returns the buffer resource pool (for command list lookups).
    ResourcePool<MetalBuffer>& GetBufferPool() { return m_bufferPool; }

    /// Returns the texture resource pool (for command list lookups).
    ResourcePool<MetalTexture>& GetTexturePool() { return m_texturePool; }

    /// Returns the shader resource pool.
    ResourcePool<MetalShader>& GetShaderPool() { return m_shaderPool; }

    /// Returns the sampler resource pool.
    ResourcePool<MetalSampler>& GetSamplerPool() { return m_samplerPool; }

    /// Returns the pipeline resource pool (for command list PSO binding).
    ResourcePool<MetalPipeline>& GetPipelinePool() { return m_pipelinePool; }

    /// Returns the argument buffer used for bindless texture access.
    ///
    /// Textures are encoded into this buffer at their SRV index, enabling
    /// shaders to access any texture via `textures[index]` in MSL.
    MTL::Buffer* GetTextureArgumentBuffer() const { return m_textureArgBuffer; }

    /// Returns the argument buffer used for bindless sampler access.
    MTL::Buffer* GetSamplerArgumentBuffer() const { return m_samplerArgBuffer; }

    /// Allocates a logical SRV descriptor index for bindless texture/buffer access.
    ///
    /// Reuses freed indices when available, otherwise increments the counter.
    ///
    /// # Returns
    /// A unique SRV index for use in argument buffers.
    uint32_t AllocateSrvIndex();

    /// Frees a previously allocated SRV index for reuse.
    ///
    /// # Arguments
    /// * `index` — The SRV index to return to the free list.
    void FreeSrvIndex(uint32_t index);

    /// Allocates a logical sampler descriptor index for bindless sampler access.
    ///
    /// # Returns
    /// A unique sampler index for use in argument buffers.
    uint32_t AllocateSamplerIndex();

    /// Frees a previously allocated sampler index for reuse.
    ///
    /// # Arguments
    /// * `index` — The sampler index to return to the free list.
    void FreeSamplerIndex(uint32_t index);

    /// Returns true if the device supports argument buffer tier 2 (full bindless).
    bool SupportsArgumentBuffersTier2() const { return m_supportsArgumentBuffersTier2; }

    // ── GPU Profiling ───────────────────────────────────────────────────

    /// Returns the custom engine GPU profiler (counter-sample-buffer based).
    ///
    /// # Returns
    /// Pointer to the profiler, or nullptr if the device does not support
    /// GPU timestamp counters. Caller does not own the pointer.
    MetalGpuProfiler* GetGpuProfiler() const { return m_gpuProfiler.get(); }

    /// Returns the opaque Tracy Metal GPU context for remote profiling.
    ///
    /// # Returns
    /// Opaque pointer to the internal `MetalTracyState`, or nullptr if Tracy
    /// is disabled or the device does not support counter sampling.
    void* GetTracyGpuContext() const { return m_tracyGpuCtx; }

private:
    bool CreateDevice();
    bool DetectGPUFamily();
    bool CreateCommandQueues();
    bool CreateArgumentBuffers();

    // ── State ────────────────────────────────────────────────────────────

    MTL::Device*        m_device = nullptr;
    MTL::CommandQueue*  m_graphicsQueue = nullptr;
    MTL::CommandQueue*  m_transferQueue = nullptr;
    MTL::Buffer*        m_textureArgBuffer = nullptr;
    MTL::Buffer*        m_samplerArgBuffer = nullptr;

    // ── Resource Pools ──────────────────────────────────────────────────

    ResourcePool<MetalBuffer>     m_bufferPool;
    ResourcePool<MetalTexture>    m_texturePool;
    ResourcePool<MetalShader>     m_shaderPool;
    ResourcePool<MetalSampler>    m_samplerPool;
    ResourcePool<MetalPipeline>   m_pipelinePool;

    // ── Descriptor Index Allocation ─────────────────────────────────────

    /// Monotonically increasing SRV index counter for logical descriptor binding.
    uint32_t m_nextSrvIndex = 0;

    /// Monotonically increasing sampler index counter.
    uint32_t m_nextSamplerIndex = 0;

    /// Free list of previously allocated SRV indices for reuse.
    std::vector<uint32_t> m_freeSrvIndices;

    /// Free list of previously allocated sampler indices for reuse.
    std::vector<uint32_t> m_freeSamplerIndices;

    // ── GPU Profiling ──────────────────────────────────────────────────

    /// Custom engine GPU profiler (counter sample buffer based).
    std::unique_ptr<MetalGpuProfiler> m_gpuProfiler;

    /// Tracy Metal GPU context (ARC-isolated behind metal_tracy_gpu.mm).
    void* m_tracyGpuCtx = nullptr;

    // ── Device Info ─────────────────────────────────────────────────────

    ShaderModel m_shaderModel = ShaderModel::SM6_6;
    std::string m_adapterName;
    std::string m_gpuFamilyStr;
    bool        m_supportsArgumentBuffersTier2 = false;
    bool        m_validationEnabled = false;
};

} // namespace uz
