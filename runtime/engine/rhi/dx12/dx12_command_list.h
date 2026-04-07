#pragma once

/// DX12 backend implementation of IRHICommandList.
///
/// Wraps ID3D12GraphicsCommandList with per-frame command allocators
/// for triple-buffered recording. Binds the global CBV_SRV_UAV and
/// Sampler descriptor heaps on Begin() for SM6.6 bindless access.

#include "../rhi.h"

#include <wrl/client.h>
#include <d3d12.h>
#include "dx12_guids.h"

namespace uz {

using Microsoft::WRL::ComPtr;

class DX12Device;

// ─────────────────────────────────────────────────────────────────────────────
// DX12CommandList
// ─────────────────────────────────────────────────────────────────────────────

/// DX12 implementation of `IRHICommandList`.
///
/// Owns one `ID3D12GraphicsCommandList` and `kFramesInFlight` (3) command
/// allocators. The allocators rotate each frame so previously submitted GPU
/// work can complete while new commands are recorded into a fresh allocator.
///
/// On `Begin()`, the global CBV_SRV_UAV and Sampler descriptor heaps are
/// bound for SM6.6 bindless shader access.
///
/// # Thread Safety
/// NOT thread-safe. Only one thread may record into this command list at a time.
class DX12CommandList final : public IRHICommandList {
public:
    DX12CommandList() = default;
    ~DX12CommandList() override;

    /// Creates command allocator(s) and the command list. Returns false on failure.
    ///
    /// # Arguments
    /// * `device` — Parent DX12 device (must outlive this command list).
    /// * `queueType` — Queue type determines the command list type (Direct, Compute, Copy).
    bool Init(DX12Device* device, RHIQueueType queueType);

    // ── IRHICommandList overrides ────────────────────────────────────────

    /// Begins recording. Resets the current frame's command allocator and
    /// binds the global CBV_SRV_UAV and Sampler descriptor heaps for bindless access.
    void Begin() override;

    /// Rebinds the engine's global CBV_SRV_UAV + Sampler descriptor heaps.
    /// Call this after any third-party API (FFX, NGX/DLSS, etc.) that may
    /// have called `SetDescriptorHeaps` with its own internal heaps and
    /// left them bound on the command list. Without this, subsequent
    /// bindless lookups (`ResourceDescriptorHeap[idx]`) read from the
    /// foreign heap and return garbage / nothing → black-screen artifacts
    /// downstream of the third-party pass.
    void RebindBindlessHeaps();

    /// Ends recording by closing the command list.
    void End() override;

    /// Submits the command list to the appropriate queue and advances the frame index.
    void Execute() override;

    /// Binds a graphics or compute pipeline state object. Sets the root signature
    /// and selects `SetGraphicsRoot*` vs `SetComputeRoot*` dispatch for subsequent calls.
    void SetPipeline(RHIPipelineHandle pipeline) override;

    /// Binds a vertex buffer view to the given input assembler slot.
    void SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer, uint32_t offset) override;

    /// Binds an index buffer view to the input assembler.
    void SetIndexBuffer(RHIBufferHandle buffer, RHIIndexFormat format, uint32_t offset) override;

    /// Sets the viewport via `RSSetViewports` on the command list.
    void SetViewport(const RHIViewport& viewport) override;

    /// Sets the scissor rectangle via `RSSetScissorRects` on the command list.
    void SetScissor(const RHIScissorRect& scissor) override;

    /// Sets a descriptor index as a 32-bit root constant (SM6.6 bindless path).
    void SetDescriptorIndex(uint32_t rootParamIndex, uint32_t descriptorIndex) override;

    /// Writes inline 32-bit root constants to the root signature.
    void SetRootConstants(uint32_t rootParamIndex, const uint32_t* data,
                          uint32_t count, uint32_t offsetIn32BitValues) override;

    /// Binds a constant buffer by GPU virtual address as a root CBV.
    void SetRootCBV(uint32_t rootParamIndex, uint64_t gpuAddress) override;

    /// Uploads CPU data into a buffer and binds it as a root CBV.
    void SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                    uint64_t offsetBytes, const void* cpuData, uint32_t dataSize) override;

    /// Binds a descriptor table at the given root parameter index.
    void SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) override;

    /// Copies a texture SRV into the staging descriptor block for SM5 binding.
    void SetFragmentTexture(uint32_t slot, RHITextureHandle texture) override;
    void InvalidateDescriptorBlock() override;

    /// Submits an indexed draw call via `DrawIndexedInstanced`.
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                     uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0) override;

    /// Submits a non-indexed draw call via `DrawInstanced`.
    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
              uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;

    /// Dispatches a compute workload via `Dispatch`.
    void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

    /// Submits indirect indexed draw calls via `ExecuteIndirect` with the draw signature.
    void DrawIndexedIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset,
                             uint32_t drawCount, uint32_t argsStride) override;

    /// Dispatches an indirect compute workload via `ExecuteIndirect` with the dispatch signature.
    void DispatchIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset) override;

    /// Inserts resource state transition barriers on the command list.
    void ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) override;

    /// Clears a render target view to the specified RGBA color.
    void ClearRenderTarget(RHITextureHandle texture, const float color[4]) override;

    /// Clears a depth-stencil view to the specified depth and stencil values.
    void ClearDepthStencil(RHITextureHandle texture, float depth, uint8_t stencil) override;

    /// Copies a region of bytes between two buffers via `CopyBufferRegion`.
    void CopyBuffer(RHIBufferHandle src, uint64_t srcOffset,
                    RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) override;

    /// Copies an entire texture to another texture via `CopyResource`.
    void CopyTexture(RHITextureHandle src, RHITextureHandle dst) override;

    /// Copies a specific mip/slice from one texture to another via `CopyTextureRegion`.
    void CopyTextureRegion(RHITextureHandle src, uint32_t srcMip, uint32_t srcSlice,
                           RHITextureHandle dst, uint32_t dstMip, uint32_t dstSlice) override;

    /// Copies buffer data into a texture subresource via `CopyTextureRegion`.
    void CopyBufferToTexture(RHIBufferHandle srcBuffer, uint64_t srcOffset,
                             uint32_t srcRowPitch,
                             RHITextureHandle dstTexture,
                             uint32_t mipLevel = 0, uint32_t arraySlice = 0) override;

    /// Copies a texture subresource into a buffer via `CopyTextureRegion`.
    void CopyTextureToBuffer(RHITextureHandle srcTexture, uint32_t mipLevel,
                             RHIBufferHandle dstBuffer, uint64_t dstOffset,
                             uint32_t dstRowPitch, uint32_t arraySlice = 0) override;

    /// Begins a render pass by setting render targets and depth-stencil views.
    /// Creates per-slice/mip RTVs if targeting non-zero array slices or mip levels.
    void BeginRenderPass(const RHIRenderPassDesc& desc) override;

    /// Ends the render pass. Frees any temporary per-slice/mip RTVs.
    void EndRenderPass() override;

    // ── IRHICommandList: Ray tracing commands ──────────────────────────

    /// Builds a DX12 BLAS via `BuildRaytracingAccelerationStructure`.
    void BuildBLAS(RHIBLASHandle blas, RHIBufferHandle scratchBuffer,
                   bool update) override;

    /// Builds a DX12 TLAS via `BuildRaytracingAccelerationStructure`.
    void BuildTLAS(RHITLASHandle tlas, RHIBufferHandle instanceBuffer,
                   uint32_t instanceCount, RHIBufferHandle scratchBuffer,
                   bool update) override;

    /// Dispatches rays via `DispatchRays` on an `ID3D12GraphicsCommandList4`.
    void DispatchRays(RHIRTPipelineHandle pipeline,
                      const RHIDispatchRaysDesc& desc) override;

    /// Inserts a UAV barrier for acceleration structures (null resource barrier).
    void UAVBarrierAS() override;

    // ── DX12-specific methods (pre-texture-pool) ─────────────────────────

    /// Transitions a resource between D3D12 states (low-level, for swap chain back buffers).
    ///
    /// Prefer `ResourceBarrier()` with `RHIBarrierDesc` for handle-based transitions.
    /// This overload takes raw D3D12 types for cases where no `RHIHandle` exists
    /// (e.g., swap chain back buffers before texture pool integration).
    ///
    /// # Arguments
    /// * `resource` — Native D3D12 resource pointer.
    /// * `before` — Current resource state.
    /// * `after` — Desired resource state.
    void TransitionBarrier(ID3D12Resource* resource,
                           D3D12_RESOURCE_STATES before,
                           D3D12_RESOURCE_STATES after);

    /// Clears a render target view by raw descriptor handle.
    ///
    /// Prefer `ClearRenderTarget()` with `RHITextureHandle` for handle-based clears.
    /// This overload is used by the swap chain which manages its own RTV descriptors.
    ///
    /// # Arguments
    /// * `rtv` — CPU descriptor handle for the render target view.
    /// * `color` — Four-element float array `[R, G, B, A]`.
    void ClearRTV(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const float color[4]);

    /// Returns the underlying `ID3D12GraphicsCommandList` (for sibling DX12 classes only).
    ID3D12GraphicsCommandList* GetNative() const { return m_cmdList.Get(); }

private:
    /// Number of command allocators for triple-buffered frame pacing.
    static constexpr uint32_t kFramesInFlight = 3;

    /// Maximum fragment textures bindable via SetFragmentTexture (SM5 path).
    static constexpr uint32_t kMaxFragmentTextures = 16;

    /// Descriptors available per frame for SM5 SetFragmentTexture staging.
    /// Each SetPipeline call consumes kMaxFragmentTextures descriptors within
    /// this budget. 256 descriptors / 16 per pass = 16 passes per frame, which
    /// covers the typical deferred rendering pipeline (GBuffer, shadow, lighting,
    /// post-process, UI, debug overlays).
    static constexpr uint32_t kDescriptorsPerFrame = 4096;

    DX12Device*                          m_device = nullptr;
    ComPtr<ID3D12GraphicsCommandList>    m_cmdList;
    ComPtr<ID3D12CommandAllocator>       m_allocators[kFramesInFlight];
    uint32_t                             m_currentFrame = 0;
    D3D12_COMMAND_LIST_TYPE              m_listType = D3D12_COMMAND_LIST_TYPE_DIRECT;

    /// True when the currently bound pipeline is a compute PSO.
    /// Used to select `SetComputeRoot*` vs `SetGraphicsRoot*` variants.
    bool                                 m_isComputeBound = false;

    /// Base index in the CBV_SRV_UAV heap for the per-frame descriptor ring.
    /// A contiguous block of `kDescriptorsPerFrame * kFramesInFlight` descriptors
    /// is allocated once in Init(). Each frame uses slice
    /// `m_frameRingBase + m_currentFrame * kDescriptorsPerFrame` so the GPU
    /// never reads descriptors that the CPU is simultaneously writing.
    /// @ref Microsoft DX12 docs: "Descriptors cannot be changed while a command
    /// list submitted for execution might reference that location."
    uint32_t                             m_frameRingBase = UINT32_MAX;

    /// Bump offset within the current frame's descriptor slice.
    /// Reset to 0 in Begin(). Advanced by kMaxFragmentTextures on each
    /// SetPipeline call that is followed by SetFragmentTexture.
    uint32_t                             m_frameDescriptorOffset = 0;

    /// Base index of the current pass's descriptor block within the frame slice.
    /// Set when the first SetFragmentTexture call after SetPipeline allocates
    /// a fresh kMaxFragmentTextures block. Reset to UINT32_MAX in SetPipeline.
    uint32_t                             m_currentPassDescBase = UINT32_MAX;

    /// Maximum number of temporary per-slice/mip RTVs per render pass.
    static constexpr uint32_t kMaxTempRtvs = 8;

    /// Temporary RTV descriptor indices allocated during BeginRenderPass
    /// for array slice or mip level targeting. Freed in EndRenderPass.
    uint32_t m_tempRtvIndices[kMaxTempRtvs] = {};
    uint32_t m_tempRtvCount = 0;

    // ── Error loop detection ─────────────────────────────────────────────
    /// Counts consecutive frames where End() (Close) or Begin() (Reset) fails.
    /// When this exceeds kMaxConsecutiveErrors, the engine writes a crash
    /// report and terminates to prevent infinite error log spam and device
    /// removal cascades (e.g., the VelocityBuffer PSO loop bug).
    static constexpr uint32_t kMaxConsecutiveErrors = 5;
    uint32_t m_consecutiveErrors = 0;

    /// Triggers crash report and process termination when the RHI enters
    /// an unrecoverable error loop.
    [[noreturn]] void FatalErrorLoop(const char* operation, uint32_t hresult);
};

} // namespace uz
