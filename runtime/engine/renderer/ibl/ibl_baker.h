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
    // ── Bake parameters (public for external reference) ──────────────────
    static constexpr uint32_t kBRDFLutSize          = 512;
    static constexpr uint32_t kIrradianceSize       = 64;
    static constexpr uint32_t kPrefilteredSize      = 1024;
    static constexpr uint32_t kPrefilteredMipLevels = 8;
    static constexpr uint32_t kThreadGroupSize      = 8;

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
    ///
    /// The caller owns the returned textures and is responsible for calling
    /// `device->DestroyTexture()` when they are no longer needed. The baker
    /// does NOT track or auto-destroy these textures.
    struct BakeResult {
        uint32_t irradianceSrvIndex  = UINT32_MAX; ///< SRV index for irradiance cubemap (diffuse IBL).
        uint32_t prefilteredSrvIndex = UINT32_MAX; ///< SRV index for prefiltered specular cubemap.
        RHITextureHandle irradianceTexture;         ///< Texture handle for irradiance cubemap (caller-owned).
        RHITextureHandle prefilteredTexture;        ///< Texture handle for prefiltered cubemap (caller-owned).
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
    [[nodiscard]] BakeResult BakeEnvironment(uint32_t envCubemapSrvIndex,
                                             RHITextureHandle envCubemapTexture = {});

    /// Result of irradiance-only bake.
    struct IrradianceResult {
        uint32_t irradianceSrvIndex = UINT32_MAX; ///< SRV index for irradiance cubemap.
        RHITextureHandle irradianceTexture;        ///< Texture handle (caller-owned).
    };

    /// Bakes only the irradiance cubemap (64x64, cheap) from a source cubemap.
    ///
    /// Use this when the prefiltered specular is already available (e.g. PMREM
    /// atlas). Skips the expensive per-mip prefilter dispatch entirely.
    ///
    /// # Arguments
    /// * `envCubemapSrvIndex` — SRV index of the source cubemap.
    /// * `envCubemapTexture` — Texture handle (for SM5 barrier).
    [[nodiscard]] IrradianceResult BakeIrradianceOnly(uint32_t envCubemapSrvIndex,
                                                       RHITextureHandle envCubemapTexture = {});

    /// Result of equirectangular-to-cubemap conversion.
    struct EquirectResult {
        RHITextureHandle cubemap;          ///< Cubemap texture (6-slice Texture2DArray, RGBA16F).
        uint32_t srvIndex = UINT32_MAX;    ///< SRV heap index for the cubemap.
    };

    /// Converts an equirectangular HDR panorama texture into a cubemap.
    ///
    /// Dispatches `equirect_to_cubemap_cs.hlsl` to convert a 2:1 equirect
    /// panorama (any resolution) into an NxN cubemap stored as a
    /// Texture2DArray with 6 slices, RGBA16Float format.
    ///
    /// The output cubemap can be passed directly to `BakeEnvironment()` for
    /// IBL irradiance and prefiltered specular baking.
    ///
    /// # Arguments
    /// * `equirectSrvIndex` — SRV descriptor heap index of the equirect
    ///   texture. Must be a valid RGBA16F or RGBA32F 2D texture.
    /// * `cubemapSize` — Face resolution in texels (e.g., 256, 512, 1024).
    ///   Must be a multiple of `kThreadGroupSize` (8).
    ///
    /// # Returns
    /// `EquirectResult` with a valid cubemap handle and SRV on success.
    /// Returns default (invalid) result on failure (error logged).
    ///
    /// # Ownership
    /// The returned cubemap texture is owned by the CALLER — the baker does
    /// NOT track or destroy it. Call `device->DestroyTexture()` when done.
    [[nodiscard]] EquirectResult EquirectToCubemap(uint32_t equirectSrvIndex,
                                                    uint32_t cubemapSize = 512);

    /// Result of PMREM atlas to cubemap conversion.
    struct PmremResult {
        RHITextureHandle cubemap;          ///< Mipmapped cubemap (6-slice Texture2DArray, RGBA16F).
        uint32_t srvIndex = UINT32_MAX;    ///< SRV heap index for the cubemap.
    };

    /// Extracts a standard cubemap with mip chain from a Three.js CubeUV PMREM atlas.
    ///
    /// The PMREM atlas contains prefiltered environment faces at multiple roughness
    /// LODs packed in a 3-wide x 2-tall grid per LOD band. This method dispatches
    /// `pmrem_to_cubemap_cs.hlsl` to extract each LOD into a cubemap mip level.
    ///
    /// The output cubemap has `lodCount` mip levels, with mip 0 = faceSize and
    /// each subsequent mip halved. Since PMREM is already prefiltered, the caller
    /// can use this directly as the prefiltered specular cubemap (skipping the
    /// expensive prefilter bake).
    ///
    /// # Arguments
    /// * `atlasSrvIndex` — SRV index of the PMREM atlas (2D, RGBA16F).
    /// * `atlasTexture` — Texture handle of the atlas (for SM5 binding).
    /// * `atlasWidth` — Total atlas width in pixels.
    /// * `atlasHeight` — Total atlas height in pixels.
    /// * `faceSize` — Per-face resolution at LOD 0 (e.g. 512).
    /// * `lodCount` — Number of LOD levels in the atlas.
    ///
    /// # Returns
    /// `PmremResult` with valid cubemap and SRV on success.
    ///
    /// # Ownership
    /// Caller owns the returned cubemap texture.
    [[nodiscard]] PmremResult PmremToCubemap(uint32_t atlasSrvIndex,
                                              RHITextureHandle atlasTexture,
                                              uint32_t atlasWidth,
                                              uint32_t atlasHeight,
                                              uint32_t faceSize,
                                              uint32_t lodCount);

    /// Returns true if the BRDF LUT has been baked.
    [[nodiscard]] bool HasBRDFLut() const noexcept { return m_brdfLutSrvIndex != UINT32_MAX; }

    /// Returns the BRDF LUT SRV index (`UINT32_MAX` if not yet baked).
    [[nodiscard]] uint32_t GetBRDFLutSrvIndex() const noexcept { return m_brdfLutSrvIndex; }

    /// Returns the BRDF LUT texture handle (for SM5 texture binding).
    [[nodiscard]] RHITextureHandle GetBRDFLutTexture() const noexcept { return m_brdfLutTexture; }

    /// Returns the irradiance cubemap texture handle (for SM5 texture binding).
    /// @deprecated Callers should use BakeResult::irradianceTexture instead.
    [[nodiscard]] RHITextureHandle GetIrradianceTexture() const noexcept { return m_lastIrradianceTexture; }

    /// Returns the prefiltered specular cubemap texture handle (for SM5 texture binding).
    /// @deprecated Callers should use BakeResult::prefilteredTexture instead.
    [[nodiscard]] RHITextureHandle GetPrefilteredTexture() const noexcept { return m_lastPrefilteredTexture; }

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

    /// Ensures the equirect-to-cubemap compute pipeline is compiled and ready.
    [[nodiscard]] bool EnsureEquirectPipeline();

    /// Ensures the PMREM-to-cubemap compute pipeline is compiled and ready.
    [[nodiscard]] bool EnsurePmremPipeline();

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;
    bool           m_initialized = false;

    // ── Compute pipelines (lazily created) ──────────────────────────────
    RHIPipelineHandle m_brdfLutPipeline    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_irradiancePipeline = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_prefilterPipeline  = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_equirectPipeline   = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_pmremPipeline      = RHIPipelineHandle::Invalid();

    // ── Shader handles ──────────────────────────────────────────────────
    RHIShaderHandle m_brdfLutCS    = RHIShaderHandle::Invalid();
    RHIShaderHandle m_irradianceCS = RHIShaderHandle::Invalid();
    RHIShaderHandle m_prefilterCS  = RHIShaderHandle::Invalid();
    RHIShaderHandle m_equirectCS   = RHIShaderHandle::Invalid();
    RHIShaderHandle m_pmremCS      = RHIShaderHandle::Invalid();

    // ── BRDF LUT texture (persistent, baked once) ───────────────────────
    RHITextureHandle m_brdfLutTexture = RHITextureHandle::Invalid();
    uint32_t         m_brdfLutSrvIndex = UINT32_MAX;

    // ── Last baked textures (non-owning reference for SM5 GetXxxTexture()) ──
    // The baker no longer owns these — callers own the BakeResult textures.
    // These are kept only for backwards compatibility with GetIrradianceTexture()/
    // GetPrefilteredTexture() accessors used by SM5 binding paths.
    RHITextureHandle m_lastIrradianceTexture  = RHITextureHandle::Invalid();
    RHITextureHandle m_lastPrefilteredTexture = RHITextureHandle::Invalid();

};

} // namespace uz
