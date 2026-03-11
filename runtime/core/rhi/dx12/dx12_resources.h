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

// ─────────────────────────────────────────────────────────────────────────────
// ResourcePool — Generic typed pool with generational handles
// ─────────────────────────────────────────────────────────────────────────────

/// Flat array pool for DX12 resources with generational handle validation.
///
/// Allocates slots linearly from a growable `std::vector`, reuses freed slots
/// via a free list stack. Each slot has a generation counter — handles with
/// mismatched generations are treated as invalid (use-after-free protection).
///
/// # Thread Safety
/// NOT thread-safe. Callers must serialize access.
///
/// # Type Requirements
/// `T` must have a `uint32_t generation` member field.
template <typename T>
class ResourcePool {
public:
    /// Allocates a new slot and returns its index. Sets `outGeneration` to the
    /// slot's new generation value. The caller must fill the slot's data fields.
    ///
    /// # Arguments
    /// * `outGeneration` — Set to the new generation of the allocated slot.
    ///
    /// # Returns
    /// The index of the allocated slot.
    uint32_t Allocate(uint32_t& outGeneration) {
        uint32_t index;
        if (!m_freeList.empty()) {
            index = m_freeList.back();
            m_freeList.pop_back();
        } else {
            index = static_cast<uint32_t>(m_slots.size());
            m_slots.emplace_back();
        }
        m_slots[index].generation++;
        outGeneration = m_slots[index].generation;
        return index;
    }

    /// Validates a handle and returns a pointer to the slot data.
    ///
    /// # Arguments
    /// * `index` — Slot index from the handle.
    /// * `generation` — Generation from the handle.
    ///
    /// # Returns
    /// Pointer to the slot if index is in range and generation matches, `nullptr` otherwise.
    T* Get(uint32_t index, uint32_t generation) {
        if (index >= m_slots.size()) return nullptr;
        auto& slot = m_slots[index];
        if (slot.generation != generation || generation == 0) return nullptr;
        return &slot;
    }

    const T* Get(uint32_t index, uint32_t generation) const {
        if (index >= m_slots.size()) return nullptr;
        auto& slot = m_slots[index];
        if (slot.generation != generation || generation == 0) return nullptr;
        return &slot;
    }

    /// Frees a slot for reuse. The generation stays bumped so old handles are invalidated.
    ///
    /// # Arguments
    /// * `index` — Slot index to free. Must have been previously allocated.
    void Free(uint32_t index) {
        if (index >= m_slots.size()) return;
        m_freeList.push_back(index);
    }

    /// Access all slots (for cleanup on shutdown).
    std::vector<T>& Slots() { return m_slots; }
    const std::vector<T>& Slots() const { return m_slots; }

private:
    std::vector<T>        m_slots;
    std::vector<uint32_t> m_freeList;
};

} // namespace uz
