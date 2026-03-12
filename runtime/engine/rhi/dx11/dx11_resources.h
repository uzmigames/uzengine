#pragma once

/// DX11 backend resource storage types.
///
/// Each resource type holds the native D3D11 objects and descriptor views.
/// Unlike DX12, DX11 manages memory implicitly — no explicit allocator needed.
/// Views (SRV, RTV, DSV, UAV) are created alongside resources and released
/// when the resource is destroyed.
///
/// Resources are stored in flat arrays indexed by `RHIHandle::index`.
/// The handle's generation counter validates access.

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
struct DX11Buffer {
    ID3D11Buffer*                buffer = nullptr;     ///< Native D3D11 buffer
    ID3D11ShaderResourceView*    srv = nullptr;        ///< SRV for structured buffer access
    ID3D11UnorderedAccessView*   uav = nullptr;        ///< UAV for read-write access
    uint32_t                     srvIndex = UINT32_MAX; ///< Logical SRV index (for SetDescriptorIndex mapping)
    uint32_t                     generation = 0;       ///< Matches RHIHandle generation
    RHIBufferDesc                desc = {};             ///< Creation parameters
    void*                        mappedPtr = nullptr;   ///< Persistent CPU mapping (Upload/Readback only)
};

// ─────────────────────────────────────────────────────────────────────────────
// DX11Texture — GPU texture with SRV and optional RTV/DSV/UAV views
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX11 texture resource.
///
/// Managed by `DX11Device`'s `ResourcePool<DX11Texture>`. Not directly visible
/// to application code — accessed only via `RHITextureHandle`.
struct DX11Texture {
    ID3D11Texture2D*             texture = nullptr;    ///< Native D3D11 texture
    ID3D11ShaderResourceView*    srv = nullptr;        ///< SRV for shader sampling
    ID3D11RenderTargetView*      rtv = nullptr;        ///< RTV for render target binding
    ID3D11DepthStencilView*      dsv = nullptr;        ///< DSV for depth-stencil binding
    ID3D11UnorderedAccessView*   uav = nullptr;        ///< UAV for read-write access
    uint32_t                     srvIndex = UINT32_MAX; ///< Logical SRV index
    uint32_t                     generation = 0;       ///< Matches RHIHandle generation
    RHITextureDesc               desc = {};             ///< Creation parameters
};

// ─────────────────────────────────────────────────────────────────────────────
// DX11Shader — Compiled shader bytecode + native shader object
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a compiled DX11 shader module.
///
/// Stores both the raw DXBC bytecode (needed for input layout creation)
/// and the native shader object (VS/PS/CS).
struct DX11Shader {
    std::vector<uint8_t>         bytecode;             ///< Owned copy of DXBC bytecode
    ID3D11VertexShader*          vs = nullptr;         ///< Vertex shader (if stage == Vertex)
    ID3D11PixelShader*           ps = nullptr;         ///< Pixel shader (if stage == Pixel)
    ID3D11ComputeShader*         cs = nullptr;         ///< Compute shader (if stage == Compute)
    RHIShaderStage               stage = RHIShaderStage::Vertex;
    uint32_t                     generation = 0;       ///< Matches RHIHandle generation
};

// ─────────────────────────────────────────────────────────────────────────────
// DX11Sampler — Texture sampler state
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX11 sampler state.
struct DX11Sampler {
    ID3D11SamplerState*          sampler = nullptr;    ///< Native sampler state
    uint32_t                     samplerIndex = UINT32_MAX; ///< Logical index for binding
    uint32_t                     generation = 0;       ///< Matches RHIHandle generation
};

// ─────────────────────────────────────────────────────────────────────────────
// DX11Pipeline — Graphics pipeline state (input layout + rasterizer + blend + DS)
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX11 graphics pipeline.
///
/// DX11 has no monolithic PSO — state is composed from individual state objects.
/// This struct groups them together for atomic binding via `SetPipeline()`.
struct DX11Pipeline {
    ID3D11InputLayout*           inputLayout = nullptr;  ///< Vertex input layout
    ID3D11RasterizerState*       rasterizer = nullptr;   ///< Rasterizer state
    ID3D11DepthStencilState*     depthStencil = nullptr; ///< Depth-stencil state
    ID3D11BlendState*            blend = nullptr;        ///< Blend state
    RHIShaderHandle              vertexShader;            ///< VS handle for binding
    RHIShaderHandle              pixelShader;             ///< PS handle for binding
    D3D11_PRIMITIVE_TOPOLOGY     topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    uint32_t                     generation = 0;         ///< Matches RHIHandle generation
    RHIGraphicsPipelineDesc      desc = {};               ///< Creation parameters
};

} // namespace uz
