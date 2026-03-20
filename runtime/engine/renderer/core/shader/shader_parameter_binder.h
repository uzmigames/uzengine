/// UzEngine Shader Parameter Binder — Automatic CB upload and resource binding.
///
/// Given a C++ shader parameter struct and its `ShaderParameterMetadata`, the
/// binder performs three steps:
/// 1. Allocates space from the CB ring allocator and copies scalar data.
/// 2. Binds the constant buffer at the designated root parameter slot.
/// 3. Binds texture, sampler, and UAV resources at their HLSL register slots.
///
/// This eliminates per-pass boilerplate for CB upload and resource binding,
/// matching the reference engine's `SetShaderParameters` pattern.
///
/// # Binding Modes
/// - **Bindless (SM6.6+)**: Texture SRV indices are available via
///   `IRHIDevice::GetTextureSRVIndex()`. The binder does NOT push these as
///   root constants — that remains the caller's responsibility (root constant
///   layout is pass-specific). Instead, textures are a no-op in bindless mode
///   since shaders fetch via `ResourceDescriptorHeap[index]`.
/// - **SM5 fallback**: Textures are bound via `SetFragmentTexture(slot, handle)`.
///   Samplers are bound via `SetFragmentSampler(slot, handle)`.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread during command recording.
///
/// # See Also
/// - `shader_parameters.h` — Type definitions and metadata structures.
/// - `cb_ring_allocator.h` — Ring allocator that provides CB memory.
/// - `root_slots.h` — Named constants for root parameter slot indices.

#pragma once

#include "cb_ring_allocator.h"
#include "shader_parameters.h"

#include <cstdint>

namespace uz {

class IRHICommandList;
class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// ShaderParameterBinder
// ─────────────────────────────────────────────────────────────────────────────

/// Stateless utility for binding a shader parameter struct to a command list.
///
/// All methods are static — no instance state is needed. The binder reads
/// metadata to determine which bytes to copy and which resources to bind.
///
/// # Examples
/// ```cpp
/// PerFrameParams params{};
/// params.viewProj = camera.GetViewProjection();
/// params.irradianceCubemap = iblResult.irradianceCubemap;
/// // ... fill other members ...
///
/// auto alloc = ShaderParameterBinder::BindCB(
///     cmd, &params, PerFrameParams::GetMetadata(),
///     cbRing, kSlotPerFrame);
///
/// // For SM5, also bind resources:
/// ShaderParameterBinder::BindResources(
///     cmd, &params, PerFrameParams::GetMetadata(), /*bindless=*/false);
/// ```
class ShaderParameterBinder {
public:
    /// Allocates CB space, copies scalar data, and binds the CB at a root slot.
    ///
    /// Copies exactly `meta.cbSize` bytes from the start of `structData` into
    /// a fresh ring allocation, then binds it as a root CBV.
    ///
    /// # Arguments
    /// * `cmd` — Command list in recording state.
    /// * `structData` — Pointer to the shader parameter struct instance.
    /// * `meta` — Metadata describing the struct layout.
    /// * `cbRing` — Ring allocator to suballocate from.
    /// * `cbRootSlot` — Root parameter index for the CBV binding.
    ///
    /// # Returns
    /// The `CBAllocation` used for the constant buffer. The caller may need
    /// this to pass to other systems (e.g., render graph pass descriptors).
    /// Returns a zeroed allocation with `cpuPtr == nullptr` if the ring is full.
    ///
    /// # Thread Safety
    /// NOT thread-safe. Must be called from the render thread.
    [[nodiscard]] static CBAllocation BindCB(
        IRHICommandList* cmd,
        const void* structData,
        const ShaderParameterMetadata& meta,
        CBRingAllocator& cbRing,
        uint32_t cbRootSlot);

    /// Binds texture and sampler resources from a shader parameter struct.
    ///
    /// Iterates all resource members in `meta` and binds them to the command
    /// list at their designated HLSL register slots.
    ///
    /// In bindless mode (`bindless == true`), texture binding is skipped
    /// because shaders access textures via `ResourceDescriptorHeap[index]`.
    /// Samplers are still bound in both modes (static samplers in root sig
    /// handle most cases, but explicit sampler binding is a safety net).
    ///
    /// # Arguments
    /// * `cmd` — Command list in recording state.
    /// * `structData` — Pointer to the shader parameter struct instance.
    /// * `meta` — Metadata describing the struct layout.
    /// * `bindless` — True if SM6.6+ bindless resource access is active.
    ///
    /// # Thread Safety
    /// NOT thread-safe. Must be called from the render thread.
    static void BindResources(
        IRHICommandList* cmd,
        const void* structData,
        const ShaderParameterMetadata& meta,
        bool bindless);

    /// Convenience: BindCB + BindResources in a single call.
    ///
    /// Equivalent to calling `BindCB()` followed by `BindResources()`.
    /// Returns the CB allocation.
    ///
    /// # Arguments
    /// * `cmd` — Command list in recording state.
    /// * `structData` — Pointer to the shader parameter struct instance.
    /// * `meta` — Metadata describing the struct layout.
    /// * `cbRing` — Ring allocator to suballocate from.
    /// * `cbRootSlot` — Root parameter index for the CBV binding.
    /// * `bindless` — True if SM6.6+ bindless resource access is active.
    ///
    /// # Returns
    /// The `CBAllocation` for the constant buffer.
    [[nodiscard]] static CBAllocation Bind(
        IRHICommandList* cmd,
        const void* structData,
        const ShaderParameterMetadata& meta,
        CBRingAllocator& cbRing,
        uint32_t cbRootSlot,
        bool bindless);
};

} // namespace uz
