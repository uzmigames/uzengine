#pragma once

/// DX11 backend resource storage types.
///
/// Each resource type holds the native D3D11 objects and per-resource descriptor
/// views. Unlike DX12's global descriptor heaps, DX11 creates views as standalone
/// COM objects attached to each resource.
///
/// # Descriptor Model (DX11 vs DX12)
/// DX11 has **no descriptor heaps**. Instead, each resource owns its own view COM
/// objects (`ID3D11ShaderResourceView*`, `ID3D11RenderTargetView*`, etc.) created
/// at resource creation time. Binding happens via `PSSetShaderResources()`,
/// `OMSetRenderTargets()`, etc. — the runtime manages slot-based binding internally.
///
/// The `srvIndex` fields are **logical indices** used by the RHI abstraction layer
/// to map `SetDescriptorIndex()` calls to the correct `PSSetShaderResources()` slot.
/// They are NOT offsets into a heap (there is no heap).
///
/// # Memory Lifetime
/// DX11 manages GPU memory implicitly through the driver. There is no D3D12MA
/// equivalent — the runtime allocates and frees VRAM when the resource COM object's
/// reference count reaches zero. View COM objects are released alongside the resource.
///
/// # Handle Generation Validation
/// Same model as DX12: resources live in `ResourcePool<T>` flat arrays.
/// Generation counters detect stale handles. See `rhi_resource_pool.h`.
///
/// # Thread Safety
/// NOT thread-safe. DX11 device contexts are single-threaded by design.
/// All resource operations must happen on the immediate context thread.

#include "../rhi_types.h"
#include "../rhi_resource_pool.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11_1.h>
#include <cstdint>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DX11Buffer — GPU buffer with optional SRV/UAV views
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX11 buffer resource.
///
/// Managed by `DX11Device`'s `ResourcePool<DX11Buffer>`. Not directly visible
/// to application code — accessed only via `RHIBufferHandle`.
///
/// # Descriptor Model
/// DX11 buffers own their view COM objects directly. SRV/UAV are created at
/// resource creation if the corresponding usage flags are set, and released
/// when the buffer is destroyed. Binding is slot-based: `srvIndex` maps to
/// the `PSSetShaderResources(srvIndex, 1, &srv)` call in `SetDescriptorIndex()`.
///
/// # Memory Layout
/// ~48 bytes (3 COM pointers + uint32 index + generation + desc + mapped ptr).
/// Stored contiguously in `ResourcePool` for cache-friendly iteration.
struct DX11Buffer {
    /// Native D3D11 buffer. Owns the GPU memory implicitly via COM ref count.
    /// Released on destroy — driver frees VRAM when refcount hits zero.
    ID3D11Buffer*                buffer = nullptr;
    /// Shader resource view for read-only structured/typed buffer access.
    /// `nullptr` if the buffer was not created with `RHIBufferUsage::ShaderResource`.
    /// Bound via `PSSetShaderResources()` / `VSSetShaderResources()`.
    ID3D11ShaderResourceView*    srv = nullptr;
    /// Unordered access view for compute read-write access.
    /// `nullptr` if not created with `RHIBufferUsage::Storage`.
    /// Bound via `CSSetUnorderedAccessViews()`.
    ID3D11UnorderedAccessView*   uav = nullptr;
    /// Logical SRV binding slot used by `SetDescriptorIndex()` to map RHI-level
    /// descriptor indices to DX11's `XSSetShaderResources(slot, ...)` calls.
    /// `UINT32_MAX` if no SRV exists.
    uint32_t                     srvIndex = UINT32_MAX;
    /// Generation counter — must match `RHIBufferHandle::generation`.
    uint32_t                     generation = 0;
    /// Backend-agnostic creation parameters (size, usage flags, heap type).
    RHIBufferDesc                desc = {};
    /// Persistently mapped CPU pointer for Dynamic/Staging buffers.
    /// DX11 uses `Map(D3D11_MAP_WRITE_NO_OVERWRITE)` for persistent mapping.
    /// `nullptr` for Default usage buffers.
    void*                        mappedPtr = nullptr;
    /// CPU-side shadow copy for DYNAMIC buffers that need persistent read-back.
    /// DX11 DYNAMIC buffers are often backed by write-combined (WC) memory,
    /// making reads from the mapped pointer unreliable (may return zeros/stale).
    /// The shadow copy is allocated on first MapBuffer call and freed on destroy.
    /// When non-null, MapBuffer returns shadowCopy instead of the mapped WC pointer.
    uint8_t*                     shadowCopy = nullptr;
    /// Size of the shadow copy allocation (matches buffer size).
    uint64_t                     shadowCopySize = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// DX11Texture — GPU texture with SRV and optional RTV/DSV/UAV views
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX11 texture resource.
///
/// Managed by `DX11Device`'s `ResourcePool<DX11Texture>`. Not directly visible
/// to application code — accessed only via `RHITextureHandle`.
///
/// # Descriptor Model
/// A DX11 texture may own up to 4 view COM objects (SRV, RTV, DSV, UAV),
/// each created at resource creation based on usage flags. Views are standalone
/// COM objects — no heap allocation, no index management. The `srvIndex` field
/// is a logical slot for the RHI binding abstraction.
///
/// # Memory Layout
/// ~56 bytes (5 COM pointers + uint32 srvIndex + generation + desc).
struct DX11Texture {
    /// Native D3D11 2D texture. Owns GPU memory via COM refcount.
    /// For swapchain back buffers, obtained via `IDXGISwapChain::GetBuffer()`.
    ID3D11Texture2D*             texture = nullptr;
    /// SRV for shader sampling (`Texture2D.Sample()` in HLSL).
    /// `nullptr` if not sampled (e.g., depth-only or render-target-only).
    ID3D11ShaderResourceView*    srv = nullptr;
    /// RTV for use as a color attachment in `OMSetRenderTargets()`.
    /// `nullptr` if not a render target.
    ID3D11RenderTargetView*      rtv = nullptr;
    /// DSV for use as a depth-stencil attachment in `OMSetRenderTargets()`.
    /// `nullptr` if not a depth-stencil target.
    ID3D11DepthStencilView*      dsv = nullptr;
    /// UAV for compute read-write access (`RWTexture2D` in HLSL).
    /// `nullptr` if not created with storage usage.
    ID3D11UnorderedAccessView*   uav = nullptr;
    /// Logical SRV binding slot for `SetDescriptorIndex()`. `UINT32_MAX` if none.
    uint32_t                     srvIndex = UINT32_MAX;
    /// Generation counter — must match `RHITextureHandle::generation`.
    uint32_t                     generation = 0;
    /// Backend-agnostic creation parameters (dimensions, format, usage flags).
    RHITextureDesc               desc = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// DX11Shader — Compiled shader bytecode + native shader object
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a compiled DX11 shader module.
///
/// Stores both the raw DXBC bytecode and the native shader COM object. The
/// bytecode must be retained because DX11 requires it for `CreateInputLayout()`
/// — the input layout needs the VS bytecode's input signature to validate
/// against the vertex buffer layout.
///
/// # Memory Layout
/// Variable size due to `bytecode` vector. Fixed fields: ~32 bytes (3 COM
/// pointers + stage enum + generation). Only one of vs/ps/cs is non-null.
///
/// # Lifetime
/// The native shader object (vs/ps/cs) holds a driver-internal copy of the
/// bytecode, but we keep our copy for input layout creation. Both the COM
/// object and the bytecode vector are released on destroy.
struct DX11Shader {
    /// Owned copy of DXBC bytecode. Required by `CreateInputLayout()` for vertex
    /// shaders. Retained for all stages for consistency and hot-reload support.
    std::vector<uint8_t>         bytecode;
    /// Native vertex shader. Non-null only when `stage == Vertex`.
    ID3D11VertexShader*          vs = nullptr;
    /// Native pixel shader. Non-null only when `stage == Pixel`.
    ID3D11PixelShader*           ps = nullptr;
    /// Native compute shader. Non-null only when `stage == Compute`.
    ID3D11ComputeShader*         cs = nullptr;
    /// Which pipeline stage this shader targets.
    RHIShaderStage               stage = RHIShaderStage::Vertex;
    /// Generation counter — must match `RHIShaderHandle::generation`.
    uint32_t                     generation = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// DX11Sampler — Texture sampler state
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX11 sampler state.
///
/// DX11 samplers are standalone COM objects bound via `PSSetSamplers()`.
/// No heap — the `samplerIndex` is the logical slot for the RHI abstraction.
///
/// # Memory Layout
/// ~12 bytes (1 COM pointer + 2 uint32).
struct DX11Sampler {
    /// Native sampler state encapsulating filter, address mode, LOD, etc.
    ID3D11SamplerState*          sampler = nullptr;
    /// Logical binding slot for `XSSetSamplers(slot, 1, &sampler)`.
    /// `UINT32_MAX` if not yet assigned.
    uint32_t                     samplerIndex = UINT32_MAX;
    /// Generation counter — must match `RHISamplerHandle::generation`.
    uint32_t                     generation = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// DX11Pipeline — Graphics pipeline state (input layout + rasterizer + blend + DS)
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX11 graphics pipeline.
///
/// DX11 has no monolithic PSO like DX12/Vulkan. Pipeline state is a collection
/// of independent state objects that must be bound individually. This struct
/// groups them so `SetPipeline()` can bind all state atomically in one call,
/// matching the RHI abstraction's pipeline model.
///
/// # Binding Sequence
/// `SetPipeline()` calls: `IASetInputLayout()`, `RSSetState()`,
/// `OMSetDepthStencilState()`, `OMSetBlendState()`, `VSSetShader()`,
/// `PSSetShader()`, `IASetPrimitiveTopology()` — all from this struct.
///
/// # Memory Layout
/// ~48 bytes (4 COM pointers + 2 RHI handles + topology + generation + desc).
///
/// # Lifetime
/// Each COM state object is created at pipeline creation and released on destroy.
/// Shader handles reference `DX11Shader` pool slots — the pipeline does NOT own
/// the shader objects, only borrows them by handle.
struct DX11Pipeline {
    /// Input layout mapping vertex buffer elements to shader input semantics.
    /// Created from the vertex shader's DXBC input signature.
    /// `nullptr` for compute pipelines.
    ID3D11InputLayout*           inputLayout = nullptr;
    /// Rasterizer state (fill mode, cull mode, depth bias, scissor enable).
    /// `nullptr` for compute pipelines.
    ID3D11RasterizerState*       rasterizer = nullptr;
    /// Depth-stencil state (depth test, stencil ops, comparison func).
    /// `nullptr` for compute pipelines.
    ID3D11DepthStencilState*     depthStencil = nullptr;
    /// Blend state (per-RT blend enable, blend factors, write mask).
    /// `nullptr` for compute pipelines.
    ID3D11BlendState*            blend = nullptr;
    /// Handle to the vertex shader in `ResourcePool<DX11Shader>`. Not owned —
    /// the shader must outlive the pipeline. Invalid for compute pipelines.
    RHIShaderHandle              vertexShader;
    /// Handle to the pixel shader in `ResourcePool<DX11Shader>`. Not owned.
    /// Invalid for compute pipelines.
    RHIShaderHandle              pixelShader;
    /// Handle to the compute shader in `ResourcePool<DX11Shader>`. Not owned.
    /// Invalid for graphics pipelines.
    RHIShaderHandle              computeShader;
    /// Primitive topology for the input assembler stage.
    /// Unused for compute pipelines.
    D3D11_PRIMITIVE_TOPOLOGY     topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    /// True if this is a compute pipeline, false for graphics.
    /// Determines which binding path `SetPipeline()` takes.
    bool                         isCompute = false;
    /// Generation counter — must match `RHIPipelineHandle::generation`.
    uint32_t                     generation = 0;
    /// Backend-agnostic creation parameters for debugging and hot-reload.
    RHIGraphicsPipelineDesc      desc = {};
};

} // namespace uz
