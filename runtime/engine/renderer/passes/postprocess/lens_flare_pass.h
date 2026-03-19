/// UzEngine Lens Flare Pass — Screen-Space Ghost-Based Lens Flare (Compute)
///
/// Dispatches a single compute shader that generates screen-space lens flare
/// ghosts and a halo ring from the bloom threshold texture. The output is an
/// additive RGBA16F contribution texture composited onto the scene by the
/// tonemap pass.
///
/// # Algorithm
/// The compute shader (lensflare_cs.hlsl) implements the John Chapman 2013
/// ghost-based technique: UV reflection through screen center at multiple
/// scales, chromatic aberration fringe, edge attenuation, and a 3-sample
/// Gaussian-blurred halo ring. This is intentionally different from UE5's
/// bokeh-tile rasterization approach — see the shader header for rationale.
///
/// # Integration
/// Created and owned by PostProcessStack / SceneRenderer. After bloom
/// threshold is computed, call Execute() with the threshold SRV. The tonemap
/// pass reads GetOutputSrv() and adds it to the scene color before
/// tonemapping.
///
/// # Usage
/// ```cpp
/// LensFlarePass lensFlare;
/// lensFlare.Init(device, &shaderMgr, width, height);
/// // Each frame, after bloom threshold:
/// lensFlare.Execute(cmd, cbRing, bloomThresholdSrv, settings);
/// uint32_t flareSrv = lensFlare.GetOutputSrv();
/// ```

#pragma once

#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;
class CBRingAllocator;

// ─────────────────────────────────────────────────────────────────────────────
// LensFlareSettings — runtime-tunable parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Runtime-configurable lens flare parameters.
///
/// Default values produce a subtle effect. Increase intensity/ghostIntensity
/// for a more prominent flare. Disable by default — callers opt in.
///
/// @source John Chapman 2013 — ghost + halo parameter ranges
struct LensFlareSettings {
    float intensity      = 1.0f;   ///< Overall flare intensity multiplier.
    float ghostIntensity = 0.5f;   ///< Ghost contribution multiplier.
    float haloRadius     = 0.6f;   ///< Halo ring radius in UV space, 0..1.
    float haloWidth      = 0.1f;   ///< Halo ring Gaussian width in UV space.
    float chromaticShift = 0.02f;  ///< Chromatic aberration offset per channel.
    float sunScreenX     = 0.5f;   ///< Sun position in screen UV [0,1]. 0.5 = center.
    float sunScreenY     = 0.5f;   ///< Sun position in screen UV [0,1]. 0.5 = center.
    bool  enabled        = false;  ///< Off by default — callers opt in.
};

// ─────────────────────────────────────────────────────────────────────────────
// CBV at b2 — LensFlareCB (matches HLSL cbuffer layout)
// ─────────────────────────────────────────────────────────────────────────────

/// Matches `cbuffer LensFlareCB : register(b2)` in lensflare_cs.hlsl.
/// Must be 256-byte aligned for D3D12 CBV.
///
/// HLSL layout (8 floats = 32 bytes):
///   bloomThresholdSrv (uint as float bits), intensity, ghostIntensity,
///   haloRadius, haloWidth, chromaticShift, outputWidth, outputHeight
struct LensFlareCBV {
    uint32_t bloomThresholdSrv;  ///< Bloom threshold texture SRV index.
    float    intensity;          ///< Overall lens flare intensity multiplier.
    float    ghostIntensity;     ///< Ghost contribution multiplier.
    float    haloRadius;         ///< Halo ring radius in UV space.
    float    haloWidth;          ///< Halo ring Gaussian width in UV space.
    float    chromaticShift;     ///< Chromatic fringe offset per channel.
    float    outputWidth;        ///< Output texture width in pixels.
    float    outputHeight;       ///< Output texture height in pixels.
    float    sunScreenX;         ///< Sun screen UV X [0,1].
    float    sunScreenY;         ///< Sun screen UV Y [0,1].
    float    _pad0[2];           ///< Align to 16 bytes.

    /// Pad to 256 bytes (D3D12 CB alignment).
    /// Current: 12 DWORDs = 48 bytes → need 208 bytes of padding.
    float    _pad[52];
};

static_assert(sizeof(LensFlareCBV) == 256,
    "LensFlareCBV must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// LensFlarePass
// ─────────────────────────────────────────────────────────────────────────────

/// Screen-space ghost-based lens flare compute pass.
///
/// Owns the output texture and compute pipeline. Dispatches a single compute
/// pass per frame that reads the bloom threshold texture and writes an
/// additive lens flare contribution.
///
/// # Ownership
/// Owns all GPU textures and pipelines. Does NOT own IRHIDevice,
/// ShaderManager, or CBRingAllocator — all must outlive this object.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.
class LensFlarePass {
public:
    LensFlarePass() = default;
    ~LensFlarePass();

    LensFlarePass(const LensFlarePass&) = delete;
    LensFlarePass& operator=(const LensFlarePass&) = delete;
    LensFlarePass(LensFlarePass&&) = delete;
    LensFlarePass& operator=(LensFlarePass&&) = delete;

    /// Initialize GPU resources and compile the compute shader.
    ///
    /// Creates the RGBA16F output texture (SRV + UAV) and lazily compiles the
    /// compute pipeline on first Execute().
    ///
    /// # Arguments
    /// * `device` — Initialized RHI device. Must outlive this object.
    /// * `shaderMgr` — Initialized shader manager. Must outlive this object.
    /// * `width` — Output texture width in pixels.
    /// * `height` — Output texture height in pixels.
    ///
    /// # Returns
    /// `true` on success, `false` if resource creation fails.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr,
                            uint32_t width, uint32_t height);

    /// Release all GPU resources.
    void Shutdown();

    /// Recreate output texture at a new resolution.
    ///
    /// Call when the render target size changes (e.g., window resize).
    void OnResize(uint32_t width, uint32_t height);

    /// Dispatches the lens flare compute shader.
    ///
    /// Reads the bloom threshold texture via bindless SRV. Writes the lens
    /// flare contribution to the internal RGBA16F output texture. After this
    /// call, GetOutputSrv() returns a valid SRV in ShaderResource state.
    ///
    /// If settings.enabled is false or the pipeline failed to compile,
    /// this is a no-op (output texture is not cleared — it retains prior data).
    ///
    /// # Arguments
    /// * `cmd` — Command list in recording state.
    /// * `cbRing` — CB ring allocator for LensFlareCB at register(b2).
    /// * `bloomThresholdSrv` — SRV index of the bloom threshold texture.
    /// * `settings` — Lens flare configuration for this frame.
    void Execute(IRHICommandList* cmd, CBRingAllocator& cbRing,
                 uint32_t bloomThresholdSrv, const LensFlareSettings& settings);

    /// Returns the SRV index of the lens flare output (additive contribution).
    /// Valid only after Init() and at least one Execute() call.
    [[nodiscard]] uint32_t GetOutputSrv() const noexcept { return m_outputSrv; }

    /// Returns true if Init() has been called and Shutdown() has not.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    /// Ensures the compute pipeline is compiled and ready.
    [[nodiscard]] bool EnsurePipeline();

    /// Creates the output texture at the given resolution.
    void CreateOutputTexture(uint32_t width, uint32_t height);

    /// Destroys the output texture and resets descriptor indices.
    void DestroyOutputTexture();

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;

    // ── Output Texture ───────────────────────────────────────────────────
    RHITextureHandle m_output;              ///< RGBA16F lens flare contribution.
    uint32_t         m_outputSrv = UINT32_MAX;
    uint32_t         m_outputUav = UINT32_MAX;

    // ── Compute Pipeline (lazily created) ────────────────────────────────
    RHIPipelineHandle m_pipeline = RHIPipelineHandle::Invalid();
    RHIShaderHandle   m_shader   = RHIShaderHandle::Invalid();

    // ── Dimensions ───────────────────────────────────────────────────────
    uint32_t m_width  = 0;
    uint32_t m_height = 0;

    bool m_initialized = false;
    bool m_initialBarrierDone = false;

    // ── Constants ────────────────────────────────────────────────────────
    static constexpr uint32_t kGroupSize = 8;  ///< Compute thread group size (8x8x1).
};

} // namespace uz
