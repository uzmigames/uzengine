/// UzEngine Environment Map Importance Sampler.
///
/// Builds a hierarchical luminance pyramid from the environment cubemap and
/// constructs a Vose alias table for O(1) importance-weighted direction sampling.
///
/// @ref Pharr & Humphreys 2004 "Physically Based Rendering" Chapter 14.6
/// @ref Vose 1991 "A Linear Algorithm for Generating Random Numbers with a Given Distribution"
/// @ref Falcor EnvMapSampler — reference implementation
///
/// # Pipeline
/// 1. Cube → equirect luminance (compute shader, mip 0)
/// 2. Hierarchical 2×2 sum reduction (compute shader, mip 1..N)
/// 3. GPU readback of mip 0 to CPU (staging buffer + fence)
/// 4. Vose alias table construction (CPU)
/// 5. Upload alias table as StructuredBuffer to GPU
///
/// # Integration
/// After `Rebuild()`, bind `GetAliasSrv()`, `GetLuminanceMip0Srv()`, and
/// `GetTotalLuminance()` in the deferred lighting pass for MIS sampling.
///
/// # Ownership
/// Owns all GPU resources it creates (textures, buffers, pipelines).
/// Does NOT own the RHI device or shader manager.

#pragma once

#include "rhi_types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;

/// Alias table entry (matches HLSL AliasEntry in envmap_sampling.hlsli).
struct AliasEntry {
    float    prob;   ///< Probability threshold [0,1].
    uint32_t alias;  ///< Alias bin index.
};

/// GPU importance sampler for environment maps.
///
/// After calling `Rebuild(envCubemapSrv)`, the alias table and luminance
/// pyramid are available for binding in the deferred lighting pass.
class EnvMapSampler {
public:
    /// Equirectangular pyramid resolution (mip 0).
    /// Width = 2 * height (equirect aspect ratio).
    static constexpr uint32_t kPyramidHeight = 256;
    static constexpr uint32_t kPyramidWidth  = 512;
    static constexpr uint32_t kThreadGroupSize = 8;

    EnvMapSampler() = default;
    ~EnvMapSampler();

    EnvMapSampler(const EnvMapSampler&) = delete;
    EnvMapSampler& operator=(const EnvMapSampler&) = delete;

    /// Initialize with device and shader manager references.
    /// Does NOT create GPU resources — those are deferred to `Rebuild()`.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr);

    /// Build (or rebuild) the luminance pyramid and alias table from the
    /// given environment cubemap.
    ///
    /// This is an expensive operation (GPU compute + readback + CPU alias build
    /// + GPU upload). Call once per environment change, not per frame.
    ///
    /// @param envCubemapSrvIndex SRV index of the source cubemap in the descriptor heap.
    /// @return true on success.
    [[nodiscard]] bool Rebuild(uint32_t envCubemapSrvIndex);

    /// Release all GPU resources.
    void Shutdown();

    /// @return SRV index of the alias table StructuredBuffer. UINT32_MAX if not built.
    [[nodiscard]] uint32_t GetAliasSrv() const noexcept { return m_aliasSrvIndex; }

    /// @return SRV index of the luminance pyramid mip 0 (equirect R32F). UINT32_MAX if not built.
    [[nodiscard]] uint32_t GetLuminanceMip0Srv() const noexcept { return m_lumMip0SrvIndex; }

    /// @return Total luminance sum (from 1x1 mip). 0 if not built.
    [[nodiscard]] float GetTotalLuminance() const noexcept { return m_totalLuminance; }

    /// @return Texel count of luminance mip 0 (kPyramidWidth * kPyramidHeight).
    [[nodiscard]] uint32_t GetTexelCount() const noexcept { return kPyramidWidth * kPyramidHeight; }

    /// @return Width of luminance mip 0.
    [[nodiscard]] uint32_t GetLuminanceWidth() const noexcept { return kPyramidWidth; }

    /// @return Height of luminance mip 0.
    [[nodiscard]] uint32_t GetLuminanceHeight() const noexcept { return kPyramidHeight; }

    /// @return true if Rebuild() has been called successfully.
    [[nodiscard]] bool IsReady() const noexcept { return m_ready; }

    /// Diagnostics (block B.5b): when set, every Rebuild() also writes the radiance the pyramid sampled
    /// as an RGBA32F equirect (`<dir>/env_equirect.f32`, kPyramidWidth x kPyramidHeight, row-major) with
    /// a `env_equirect.json` beside it. The renderer forwards the region-dump directory here.
    void SetDumpDirectory(std::string dir) { m_dumpDir = std::move(dir); }

private:
    /// Create pyramid textures (one per mip level, R32F).
    bool CreatePyramidTextures();

    /// Destroy pyramid textures.
    void DestroyPyramidTextures();

    /// Ensure compute pipelines are compiled.
    [[nodiscard]] bool EnsurePipelines();

    /// Dispatch mip 0 pass (cube → equirect luminance; the radiance too when colorUavIndex is set).
    void DispatchMip0(IRHICommandList* cmdList, uint32_t envCubemapSrvIndex, uint32_t colorUavIndex);
    /// Read the radiance equirect back and write the dump files (block B.5b diagnostics).
    void WriteRadianceDump(RHITextureHandle colorTex, uint32_t envCubemapSrvIndex);

    /// Dispatch reduction passes (mip 1..N).
    void DispatchReductions(IRHICommandList* cmdList);

    /// Readback mip 0 to CPU, build alias table, upload to GPU.
    [[nodiscard]] bool BuildAndUploadAliasTable();

    /// @ref Vose 1991 — construct alias table from probability array.
    static void BuildAliasTable(const float* weights, uint32_t count,
                                std::vector<AliasEntry>& outTable);

    // ── Device references ────────────────────────────────────────────────
    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;

    // ── Compute pipelines ────────────────────────────────────────────────
    RHIShaderHandle   m_mip0CS       = RHIShaderHandle::Invalid();
    RHIPipelineHandle m_mip0Pipeline = RHIPipelineHandle::Invalid();
    RHIShaderHandle   m_reduceCS       = RHIShaderHandle::Invalid();
    RHIPipelineHandle m_reducePipeline = RHIPipelineHandle::Invalid();

    // ── Pyramid textures (one per mip, R32_FLOAT) ────────────────────────
    struct MipLevel {
        RHITextureHandle texture;
        uint32_t         srvIndex = UINT32_MAX;
        uint32_t         uavIndex = UINT32_MAX;
        uint32_t         width    = 0;
        uint32_t         height   = 0;
    };
    std::vector<MipLevel> m_mipLevels;

    // ── Alias table (StructuredBuffer<AliasEntry>) ───────────────────────
    RHIBufferHandle m_aliasBuffer;
    uint32_t        m_aliasSrvIndex = UINT32_MAX;

    // ── Luminance pyramid mip 0 SRV (for PDF evaluation in shader) ──────
    uint32_t m_lumMip0SrvIndex = UINT32_MAX;

    // ── Total luminance (from 1x1 mip readback) ─────────────────────────
    float m_totalLuminance = 0.0f;

    // ── State ────────────────────────────────────────────────────────────
    bool m_ready = false;
    std::string m_dumpDir;   ///< Diagnostics dump directory (empty = no dump).
};

} // namespace uz
