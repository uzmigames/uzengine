#pragma once

/// DX12 backend resource storage types.
///
/// Each resource type holds the D3D12MA allocation, native D3D12 resource,
/// and descriptor indices for bindless access (SRV/UAV in the global
/// CBV_SRV_UAV heap) or rendering (RTV/DSV indices).
///
/// # Descriptor Allocation Strategy
/// DX12 uses two global descriptor heaps managed by `DX12Device`:
/// - **CBV_SRV_UAV heap** (shader-visible): Bindless descriptors for textures
///   and buffers. Each resource that needs shader access gets a slot allocated
///   via `DX12Device::AllocateDescriptor()`. Freed slots return to a free list.
/// - **Sampler heap** (shader-visible): Sampler descriptors, same allocation model.
/// - **RTV heap** (non-shader-visible): Render target views for color attachments.
/// - **DSV heap** (non-shader-visible): Depth-stencil views.
///
/// Descriptor indices stored in each resource struct are offsets into these heaps.
/// `UINT32_MAX` indicates no descriptor was allocated for that view type.
///
/// # Handle Generation Validation
/// Resources live in `ResourcePool<T>` flat arrays indexed by `RHIHandle::index`.
/// Each slot has a monotonically-increasing `generation` counter. When a slot is
/// freed and reallocated, its generation increments — stale handles with the old
/// generation are safely rejected by `ResourcePool::Get()`.
///
/// # Memory Lifetime
/// GPU memory is owned by D3D12MA (`D3D12MA::Allocation`). The allocation is
/// released when the resource is destroyed via `DX12Device::DestroyBuffer()` /
/// `DestroyTexture()`. Callers must ensure the GPU is no longer referencing the
/// resource before destruction (fence synchronization).
///
/// # Thread Safety
/// NOT thread-safe. All resource creation/destruction must happen on the
/// render thread (or be externally synchronized).

#include "../rhi_types.h"
#include "../rhi_resource_pool.h"

#include <d3d12.h>
#include <cstdint>
#include <vector>

// Forward-declare D3D12MA types
namespace D3D12MA { class Allocation; }

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DX12Buffer — GPU buffer with optional bindless SRV/UAV
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX12 buffer resource (GPU memory + descriptor indices).
///
/// Managed by `DX12Device`'s `ResourcePool<DX12Buffer>`. Not directly visible
/// to application code — accessed only via `RHIBufferHandle`.
///
/// # Memory Layout
/// Size: ~48 bytes (2 pointers + 4 uint32 + desc + mapped ptr). Stored in a
/// contiguous `std::vector` inside `ResourcePool` for cache-friendly iteration.
///
/// # Descriptor Model
/// - `srvIndex` / `uavIndex` are offsets into the global CBV_SRV_UAV descriptor
///   heap. Shaders access buffers bindlessly via these indices (SM6.6
///   `ResourceDescriptorHeap[index]`).
/// - `UINT32_MAX` means no descriptor was allocated for that view type.
/// - Descriptors are allocated at resource creation and freed at destruction.
///
/// # Lifetime
/// - `allocation` owns the GPU memory via D3D12MA. Released on destroy.
/// - `resource` is the committed ID3D12Resource extracted from the allocation.
/// - `mappedPtr` is valid for the entire resource lifetime (Upload/Readback heaps
///   only). Never dereference for Default heap resources.
struct DX12Buffer {
    /// D3D12MA allocation that owns the GPU memory. Releasing this frees the
    /// underlying committed resource. Always non-null for live buffers.
    D3D12MA::Allocation*         allocation = nullptr;
    /// Native D3D12 resource pointer, extracted from `allocation` at creation.
    /// Used for binding, barriers, and Map/Unmap (though persistent mapping is
    /// preferred via D3D12MA for Upload/Readback heaps).
    ID3D12Resource*              resource = nullptr;
    /// Offset into the global shader-visible CBV_SRV_UAV heap for read-only
    /// structured/typed buffer access. `UINT32_MAX` if no SRV was created
    /// (e.g., vertex buffers that are only bound via VBV, not shader-read).
    uint32_t                     srvIndex = UINT32_MAX;
    /// Offset into the global CBV_SRV_UAV heap for read-write (UAV) access.
    /// `UINT32_MAX` if the buffer was not created with `RHIBufferUsage::Storage`.
    uint32_t                     uavIndex = UINT32_MAX;
    /// Generation counter for handle validation. Incremented each time this pool
    /// slot is recycled. Must match `RHIBufferHandle::generation` for access.
    uint32_t                     generation = 0;
    /// Backend-agnostic creation parameters (size, usage flags, heap type).
    /// Retained for debugging and for recreation on device loss.
    RHIBufferDesc                desc = {};
    /// Persistently mapped CPU pointer for Upload/Readback heap buffers.
    /// Set once at creation via `D3D12MA`'s persistent mapping and valid for the
    /// resource's entire lifetime. Always `nullptr` for Default heap buffers.
    void*                        mappedPtr = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// DX12Texture — GPU texture with SRV and optional RTV/DSV
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX12 texture resource (GPU memory + SRV/RTV/DSV/UAV indices).
///
/// Managed by `DX12Device`'s `ResourcePool<DX12Texture>`. Not directly visible
/// to application code — accessed only via `RHITextureHandle`.
///
/// # Memory Layout
/// Size: ~48 bytes (2 pointers + 5 uint32 + desc). Contiguous in ResourcePool.
///
/// # Descriptor Model
/// A texture may occupy up to 4 descriptor slots across different heaps:
/// - `srvIndex`: CBV_SRV_UAV heap — for shader sampling (`Texture2D` in HLSL).
/// - `uavIndex`: CBV_SRV_UAV heap — for compute read-write (`RWTexture2D`).
/// - `rtvIndex`: RTV heap — for use as a render target color attachment.
/// - `dsvIndex`: DSV heap — for use as a depth-stencil attachment.
///
/// Each index is `UINT32_MAX` if that view type was not requested at creation.
/// Swapchain back buffers typically only have `rtvIndex` set.
///
/// # Lifetime
/// Same as `DX12Buffer` — `allocation` owns GPU memory, released on destroy.
/// Swapchain textures are a special case: `allocation` is `nullptr` because the
/// swapchain owns the underlying resource.
struct DX12Texture {
    /// D3D12MA allocation owning GPU memory. `nullptr` for swapchain-owned images.
    D3D12MA::Allocation*         allocation = nullptr;
    /// Native D3D12 resource. For swapchain textures, obtained via
    /// `IDXGISwapChain::GetBuffer()` rather than D3D12MA.
    ID3D12Resource*              resource = nullptr;
    /// CBV_SRV_UAV heap offset for shader sampling. `UINT32_MAX` if not sampled.
    uint32_t                     srvIndex = UINT32_MAX;
    /// CBV_SRV_UAV heap offset for compute read-write. `UINT32_MAX` if not storage.
    uint32_t                     uavIndex = UINT32_MAX;
    /// RTV heap offset for render target binding. `UINT32_MAX` if not a render target.
    uint32_t                     rtvIndex = UINT32_MAX;
    /// DSV heap offset for depth-stencil binding. `UINT32_MAX` if not a depth target.
    uint32_t                     dsvIndex = UINT32_MAX;
    /// Generation counter — must match `RHITextureHandle::generation`.
    uint32_t                     generation = 0;
    /// Backend-agnostic creation parameters (dimensions, format, usage flags).
    RHITextureDesc               desc = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// DX12Shader — Compiled shader bytecode
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a compiled DX12 shader module.
///
/// Stores an owned copy of the DXIL bytecode produced by DXC compilation.
/// The bytecode is referenced by pipeline state object (PSO) creation and must
/// remain valid until all PSOs using it are destroyed.
///
/// # Memory Layout
/// Variable size due to `bytecode` vector. The vector's heap allocation holds
/// the shader binary; the struct itself is ~40 bytes in the ResourcePool.
struct DX12Shader {
    /// Owned copy of DXIL bytecode. Referenced by `D3D12_SHADER_BYTECODE` in
    /// PSO descriptors. Must outlive any pipeline that uses this shader.
    std::vector<uint8_t>         bytecode;
    /// Which pipeline stage this shader targets (Vertex, Pixel, Compute, etc.).
    RHIShaderStage               stage = RHIShaderStage::Vertex;
    /// Generation counter — must match `RHIShaderHandle::generation`.
    uint32_t                     generation = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// DX12Sampler — Texture sampler with bindless index
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a sampler descriptor in the global sampler heap.
///
/// DX12 has a separate shader-visible sampler heap (max 2048 entries).
/// Each `DX12Sampler` owns one slot. Shaders reference samplers bindlessly
/// via `SamplerDescriptorHeap[samplerIndex]` (SM6.6).
///
/// # Memory Layout
/// 8 bytes (2 uint32). Minimal footprint — all state lives in the heap descriptor.
struct DX12Sampler {
    /// Offset into the global shader-visible sampler descriptor heap.
    /// `UINT32_MAX` if the slot has not been allocated (invalid sampler).
    uint32_t                     samplerIndex = UINT32_MAX;
    /// Generation counter — must match `RHISamplerHandle::generation`.
    uint32_t                     generation = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// DX12Pipeline — Graphics or compute pipeline state object
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX12 pipeline state object (PSO).
///
/// Encapsulates all fixed-function and shader state in a single monolithic
/// object. DX12 validates the full pipeline at creation time, so `SetPipeline()`
/// is a cheap pointer swap at draw time.
///
/// # Lifetime
/// The PSO holds internal references to shader bytecode, root signature, etc.
/// It must be released (`pso->Release()`) before the device is destroyed.
///
/// # Memory Layout
/// ~16 bytes (pointer + uint32 + desc). The heavy state lives inside the
/// `ID3D12PipelineState` COM object on the driver side.
struct DX12Pipeline {
    /// Native pipeline state object. Encapsulates VS, PS, rasterizer, blend,
    /// depth-stencil, input layout, and root signature state.
    ID3D12PipelineState*         pso = nullptr;
    /// Generation counter — must match `RHIPipelineHandle::generation`.
    uint32_t                     generation = 0;
    /// True if this is a compute pipeline, false for graphics.
    /// Used by `SetPipeline()` to select `SetComputeRootSignature` vs
    /// `SetGraphicsRootSignature` and by root parameter binding calls
    /// to select the correct `SetCompute*` vs `SetGraphics*` variants.
    bool                         isCompute = false;
    /// Backend-agnostic creation parameters. Retained for debugging, hot-reload
    /// (recreate PSO with modified shaders), and serialization.
    RHIGraphicsPipelineDesc      desc = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// DX12BLAS — Bottom-level acceleration structure (per-mesh DXR geometry)
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX12 bottom-level acceleration structure.
///
/// Holds the committed result buffer, scratch buffer, and build inputs
/// needed for `BuildRaytracingAccelerationStructure`. Managed by
/// `DX12Device`'s `ResourcePool<DX12BLAS>`.
///
/// @ref Microsoft DXR spec — D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC.
struct DX12BLAS {
    /// D3D12MA allocation for the result buffer.
    D3D12MA::Allocation*  resultAllocation  = nullptr;
    /// Native D3D12 resource for the result buffer.
    ID3D12Resource*       resultResource    = nullptr;
    /// D3D12MA allocation for the scratch buffer (kept alive until compaction).
    D3D12MA::Allocation*  scratchAllocation = nullptr;
    /// Native D3D12 resource for the scratch buffer.
    ID3D12Resource*       scratchResource   = nullptr;
    /// Size of the result buffer in bytes.
    uint64_t              resultSizeBytes   = 0;
    /// Size of the scratch buffer in bytes.
    uint64_t              scratchSizeBytes  = 0;
    /// Cached geometry descriptions for rebuild/compaction.
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;
    /// Build flags (cached from RHIBLASDesc for rebuild).
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    /// True if compaction is pending (post-build, pre-compact copy).
    bool                  compactionPending = false;
    /// Generation counter — must match `RHIBLASHandle::generation`.
    uint32_t              generation        = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// DX12TLAS — Top-level acceleration structure (scene instance list)
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX12 top-level acceleration structure.
///
/// Holds the result buffer, scratch buffer, and a bindless SRV descriptor
/// for shader access via `ResourceDescriptorHeap[srvIndex]`.
///
/// @ref Microsoft DXR spec — D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC.
struct DX12TLAS {
    /// D3D12MA allocation for the result buffer.
    D3D12MA::Allocation*  resultAllocation  = nullptr;
    /// Native D3D12 resource for the result buffer.
    ID3D12Resource*       resultResource    = nullptr;
    /// D3D12MA allocation for the scratch buffer.
    D3D12MA::Allocation*  scratchAllocation = nullptr;
    /// Native D3D12 resource for the scratch buffer.
    ID3D12Resource*       scratchResource   = nullptr;
    /// Size of the result buffer in bytes.
    uint64_t              resultSizeBytes   = 0;
    /// Size of the scratch buffer in bytes.
    uint64_t              scratchSizeBytes  = 0;
    /// SRV index in the global CBV_SRV_UAV heap for shader access.
    uint32_t              srvIndex          = UINT32_MAX;
    /// Maximum instance count this TLAS was allocated for.
    uint32_t              maxInstanceCount  = 0;
    /// Build flags (cached from RHITLASDesc for rebuild).
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    /// Generation counter — must match `RHITLASHandle::generation`.
    uint32_t              generation        = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// DX12RTPipeline — Ray tracing pipeline state object (RTPSO)
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX12 ray tracing state object.
///
/// Wraps `ID3D12StateObject` and its properties interface for retrieving
/// shader identifiers needed to build shader binding tables.
///
/// @ref Microsoft DXR spec — D3D12_STATE_OBJECT_DESC.
struct DX12RTPipeline {
    /// The D3D12 state object containing all DXR shaders and hit groups.
    ID3D12StateObject*               stateObject = nullptr;
    /// Properties interface for querying shader identifiers by export name.
    ID3D12StateObjectProperties*     properties  = nullptr;
    /// Generation counter — must match `RHIRTPipelineHandle::generation`.
    uint32_t                         generation  = 0;
};

// ResourcePool<T> is defined in ../rhi_resource_pool.h (shared across all backends).

} // namespace uz
