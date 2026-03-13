#pragma once

/// UzEngine Backend Capabilities — Runtime feature detection across RHI backends.
///
/// Provides a unified capability query system that abstracts backend-specific
/// feature detection. Used by the renderer to select optimal code paths
/// (e.g., bindless vs bound, mesh shaders vs vertex, indirect vs direct).
///
/// # Design
/// - Feature tiers (Tier1/Tier2/Tier3) group related capabilities.
/// - Each backend populates its capabilities during Init.
/// - Renderer queries capabilities once and caches code path decisions.
///
/// # Thread Safety
/// Immutable after initialization. Safe to read from any thread.

#include <cstdint>

namespace uz {

/// Feature tier levels for progressive enhancement.
///
/// The tier is computed from individual capability flags via `ComputeTier()`.
/// Higher tiers unlock more advanced rendering paths.
enum class FeatureTier : uint8_t {
    Tier1, ///< Baseline: SM5.0, bound descriptors, no mesh shaders.
    Tier2, ///< Mid-tier: SM6.0+, bindless descriptors, indirect draw.
    Tier3, ///< High-end: SM6.6, mesh shaders, ray tracing, work graphs.
};

/// Individual capability flags populated by the RHI backend during init.
///
/// After setting flags, call `ComputeTier()` to derive the overall tier.
/// Then call `LogCapabilities()` to emit a diagnostic summary.
struct BackendCapabilities {
    // ── Descriptor model ────────────────────────────────────────────────────
    bool bindlessResources  = false; ///< SM6.6 bindless / VK descriptor indexing.
    bool descriptorIndexing = false; ///< VK_EXT_descriptor_indexing / DX12 resource binding tier 3.

    // ── Geometry pipeline ───────────────────────────────────────────────────
    bool meshShaders     = false; ///< DX12 mesh shaders / VK_EXT_mesh_shader.
    bool indirectDraw    = false; ///< ExecuteIndirect / vkCmdDrawIndexedIndirect.
    bool indirectDispatch = false; ///< Indirect compute dispatch.

    // ── Ray tracing ─────────────────────────────────────────────────────────
    bool rayTracing       = false; ///< DXR 1.1 / VK_KHR_ray_tracing_pipeline.
    bool inlineRayTracing = false; ///< DXR 1.1 inline / VK_KHR_ray_query.

    // ── Compute ─────────────────────────────────────────────────────────────
    bool asyncCompute    = false; ///< Separate compute queue available.
    bool waveIntrinsics  = false; ///< SM6.0 wave intrinsics / subgroup ops.

    // ── Memory ──────────────────────────────────────────────────────────────
    bool sparseResources = false; ///< Tiled resources / sparse binding.
    bool placedResources = false; ///< Placed/aliased resources.

    // ── Shader model ────────────────────────────────────────────────────────
    uint32_t maxShaderModel = 50; ///< Numeric shader model (50=SM5.0, 60=SM6.0, 66=SM6.6).

    // ── Derived tier ────────────────────────────────────────────────────────
    FeatureTier tier = FeatureTier::Tier1; ///< Computed by `ComputeTier()`.

    /// Computes the feature tier from individual capability flags.
    ///
    /// Must be called after all flags have been populated by the backend.
    /// Sets `tier` to the highest matching tier.
    void ComputeTier();

    /// Logs all capabilities and the computed tier via UZ_LOG(RHI, Info).
    void LogCapabilities() const;
};

} // namespace uz
