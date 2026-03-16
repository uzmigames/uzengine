/// UzEngine IBL Baker — GPU Compute Pipeline for Image-Based Lighting.
///
/// Orchestrates the three-stage IBL bake pipeline:
/// 1. BRDF integration LUT (512x512 RG16F) — baked once at engine init
/// 2. Irradiance cubemap (32x32 per face, RGBA16F) — per environment
/// 3. Prefiltered specular cubemap (128x128 base, 5 mips, RGBA16F) — per environment
///
/// All stages run as compute shader dispatches on the graphics queue.
/// Resources are created via the RHI device and bound through the standard
/// root signature (b0 root constants, u0 UAV, s0 sampler).
///
/// # Ownership
/// Owns all GPU textures and pipelines it creates. Does NOT own the
/// `IRHIDevice` or `ShaderManager` — both must outlive this object.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render/init thread.
///
/// # Examples
/// ```cpp
/// IBLBaker baker;
/// if (!baker.Init(device, &shaderMgr)) { /* handle failure */ }
///
/// uint32_t brdfLut = baker.BakeBRDFLut();
/// auto result = baker.BakeEnvironment(envCubemapSrvIndex);
/// // Use result.irradianceSrvIndex and result.prefilteredSrvIndex in shaders.
///
/// baker.Shutdown();
/// ```

#pragma once

#include "rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class ShaderManager;

/// GPU compute pipeline for baking IBL textures (BRDF LUT, irradiance, prefiltered specular).
///
/// The baker compiles compute shaders on first use via `ShaderManager` and
/// creates all intermediate GPU resources through the RHI device. Baked
/// textures persist until `Shutdown()` is called.
class IBLBaker {
public:
    IBLBaker() = default;
    ~IBLBaker();

    IBLBaker(const IBLBaker&) = delete;
    IBLBaker& operator=(const IBLBaker&) = delete;
    IBLBaker(IBLBaker&&) = delete;
    IBLBaker& operator=(IBLBaker&&) = delete;

    /// Initializes the baker with device and shader manager references.
    ///
    /// Does NOT compile shaders or create resources — those are deferred to
    /// the first `Bake*()` call. This only stores the pointers.
    ///
    /// # Arguments
    /// * `device` — Initialized RHI device. Must outlive this baker.
    /// * `shaderMgr` — Initialized shader manager. Must outlive this baker.
    ///
    /// # Returns
    /// `true` on success, `false` if either argument is null.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr);

    /// Bakes the BRDF integration LUT (split-sum scale + bias terms).
    ///
    /// Creates a 512x512 RG16F texture, dispatches `brdf_lut_cs.hlsl`,
    /// transitions to ShaderResource state, and returns the SRV index.
    /// Subsequent calls return the cached SRV index without re-baking.
    ///
    /// # Returns
    /// SRV descriptor heap index on success, `UINT32_MAX` on failure (error logged).
    [[nodiscard]] uint32_t BakeBRDFLut();

    /// Result of environment cubemap baking.
    struct BakeResult {
        uint32_t irradianceSrvIndex  = UINT32_MAX; ///< SRV index for irradiance cubemap (diffuse IBL).
        uint32_t prefilteredSrvIndex = UINT32_MAX; ///< SRV index for prefiltered specular cubemap.
    };

    /// Bakes irradiance and prefiltered specular cubemaps from a source environment cubemap.
    ///
    /// Creates two cubemap textures (stored as Texture2DArray with 6 slices),
    /// dispatches `irradiance_cs.hlsl` and `prefilter_cs.hlsl` for each mip
    /// level, and transitions to ShaderResource state.
    ///
    /// For per-mip UAV dispatch, separate temporary textures are created per
    /// mip level and copied into the final mip chain to avoid per-mip UAV
    /// descriptor complexity.
    ///
    /// # Arguments
    /// * `envCubemapSrvIndex` — SRV index of the source environment cubemap
    ///   in the global descriptor heap.
    ///
    /// # Returns
    /// `BakeResult` with valid SRV indices on success, or `UINT32_MAX` indices
    /// on failure (error logged).
    [[nodiscard]] BakeResult BakeEnvironment(uint32_t envCubemapSrvIndex);

    /// Returns true if the BRDF LUT has been baked.
    [[nodiscard]] bool HasBRDFLut() const noexcept { return m_brdfLutSrvIndex != UINT32_MAX; }

    /// Returns the BRDF LUT SRV index (`UINT32_MAX` if not yet baked).
    [[nodiscard]] uint32_t GetBRDFLutSrvIndex() const noexcept { return m_brdfLutSrvIndex; }

    /// Destroys all GPU resources and pipelines owned by the baker.
    ///
    /// Safe to call multiple times or without prior `Init()`.
    void Shutdown();

    /// Returns true if `Init()` has been called and `Shutdown()` has not.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    /// Ensures the BRDF LUT compute pipeline is compiled and ready.
    [[nodiscard]] bool EnsureBRDFLutPipeline();

    /// Ensures the irradiance compute pipeline is compiled and ready.
    [[nodiscard]] bool EnsureIrradiancePipeline();

    /// Ensures the prefilter compute pipeline is compiled and ready.
    [[nodiscard]] bool EnsurePrefilterPipeline();

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;
    bool           m_initialized = false;

    // ── Compute pipelines (lazily created) ──────────────────────────────
    RHIPipelineHandle m_brdfLutPipeline    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_irradiancePipeline = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_prefilterPipeline  = RHIPipelineHandle::Invalid();

    // ── Shader handles ──────────────────────────────────────────────────
    RHIShaderHandle m_brdfLutCS    = RHIShaderHandle::Invalid();
    RHIShaderHandle m_irradianceCS = RHIShaderHandle::Invalid();
    RHIShaderHandle m_prefilterCS  = RHIShaderHandle::Invalid();

    // ── BRDF LUT texture (persistent, baked once) ───────────────────────
    RHITextureHandle m_brdfLutTexture = RHITextureHandle::Invalid();
    uint32_t         m_brdfLutSrvIndex = UINT32_MAX;

    // ── Environment bake textures (persistent, re-baked on sky change) ──
    RHITextureHandle m_irradianceTexture  = RHITextureHandle::Invalid();
    RHITextureHandle m_prefilteredTexture = RHITextureHandle::Invalid();

    // ── Bake parameters ─────────────────────────────────────────────────
    static constexpr uint32_t kBRDFLutSize          = 512;
    static constexpr uint32_t kIrradianceSize       = 32;
    static constexpr uint32_t kPrefilteredSize      = 128;
    static constexpr uint32_t kPrefilteredMipLevels = 5;
    static constexpr uint32_t kThreadGroupSize      = 8;
};

} // namespace uz
