#pragma once

/// DX12 backend resource storage types.
///
/// Each resource type holds the D3D12MA allocation, native D3D12 resource,
/// and descriptor indices for bindless access (SRV/UAV in the global
/// CBV_SRV_UAV heap) or rendering (RTV/DSV indices).
///
/// Resources are stored in flat arrays indexed by RHIHandle::index.
/// The handle's generation counter validates access.

#include "../rhi_types.h"
#include "../resource_pool.h"

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
struct DX12Buffer {
    D3D12MA::Allocation*         allocation = nullptr;  ///< D3D12MA allocation (owns the resource)
    ID3D12Resource*              resource = nullptr;    ///< Underlying D3D12 resource
    uint32_t                     srvIndex = UINT32_MAX; ///< Bindless SRV index (UINT32_MAX = none)
    uint32_t                     uavIndex = UINT32_MAX; ///< Bindless UAV index (UINT32_MAX = none)
    uint32_t                     generation = 0;        ///< Matches RHIHandle generation
    RHIBufferDesc                desc = {};             ///< Creation parameters
    void*                        mappedPtr = nullptr;   ///< Persistent CPU mapping (Upload/Readback only)
};

// ─────────────────────────────────────────────────────────────────────────────
// DX12Texture — GPU texture with SRV and optional RTV/DSV
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX12 texture resource (GPU memory + SRV/RTV/DSV/UAV indices).
///
/// Managed by `DX12Device`'s `ResourcePool<DX12Texture>`. Not directly visible
/// to application code — accessed only via `RHITextureHandle`.
struct DX12Texture {
    D3D12MA::Allocation*         allocation = nullptr;
    ID3D12Resource*              resource = nullptr;
    uint32_t                     srvIndex = UINT32_MAX; ///< Bindless SRV index
    uint32_t                     uavIndex = UINT32_MAX; ///< Bindless UAV index
    uint32_t                     rtvIndex = UINT32_MAX; ///< RTV index (render targets only)
    uint32_t                     dsvIndex = UINT32_MAX; ///< DSV index (depth stencils only)
    uint32_t                     generation = 0;
    RHITextureDesc               desc = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// DX12Shader — Compiled shader bytecode
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a compiled shader module (owns a copy of the bytecode).
struct DX12Shader {
    std::vector<uint8_t>         bytecode;              ///< Owned copy of DXIL/SPIRV bytecode
    RHIShaderStage               stage = RHIShaderStage::Vertex;
    uint32_t                     generation = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// DX12Sampler — Texture sampler with bindless index
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a sampler descriptor (index in the global sampler heap).
struct DX12Sampler {
    uint32_t                     samplerIndex = UINT32_MAX; ///< Index in global sampler heap
    uint32_t                     generation = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// DX12Pipeline — Graphics or compute pipeline state object
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a DX12 pipeline state object (PSO).
struct DX12Pipeline {
    ID3D12PipelineState*         pso = nullptr;        ///< Native pipeline state object
    uint32_t                     generation = 0;
    RHIGraphicsPipelineDesc      desc = {};             ///< Creation parameters (for debugging)
};

// ResourcePool<T> is defined in ../resource_pool.h (shared across all backends).

} // namespace uz
