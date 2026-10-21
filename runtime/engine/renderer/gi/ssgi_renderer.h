/// UzEngine diffuse gather renderer.
///
/// r.GI.FinalGather selects surface SDF rays backed by the world radiance cache.
/// Legacy r.SSGI traces the previous HDR buffer in screen space instead.
/// Both paths share a half-resolution trace, receiver-plane spatial filter,
/// geometric temporal accumulation and joint bilateral reconstruction.
/// World mode omits previous-HDR reduction/copy and outputs material-independent E/pi.
/// Unsupported full-resolution receivers trace locally instead of borrowing unrelated surfaces.
///
/// History stores irradiance, frame count, world position and octahedral normal.
/// Reprojection uses the previous jittered view-projection; depth/normal rejection
/// handles disocclusion. Creation, resizing, mode changes and scene revisions invalidate history.
/// SceneRenderer schedules gather after GDF/RC updates and before deferred lighting,
/// which consumes the full-resolution result through PerFrameCB::ssgiSrvIndex.
#pragma once

#include "rhi_types.h"

#include <cstdint>
#include "global_distance_field.h"
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class CBRingAllocator;
class PipelineLibrary;

// ─────────────────────────────────────────────────────────────────────────────
// SSGISettings — runtime-tunable parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Runtime-configurable SSGI parameters.
///
/// SSGI CVars — runtime-configurable quality and intensity
struct SSGISettings {
    bool     enabled   = false;   ///< Master toggle for SSGI. Matches the r.SSGI CVar default.
    /// Quality level (1-4). Maps to ray count: Q1=4, Q2=8, Q3=16, Q4=32.
    /// Quality maps to ray count: Q1=4, Q2=8, Q3=16, Q4=32
    int      quality   = 2;
    float    intensity = 1.0f;    ///< SSGI indirect diffuse intensity multiplier.
};

// ─────────────────────────────────────────────────────────────────────────────
// SSGIRenderParams — per-frame dispatch parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame parameters needed to dispatch SSGI compute passes.
struct SSGIRenderParams {
    CBRingAllocator* cbRing      = nullptr;
    PipelineLibrary* pipelines   = nullptr;

    // GBuffer SRV indices (full render resolution)
    uint32_t depthSrvIndex       = UINT32_MAX;
    uint32_t normalSrvIndex      = UINT32_MAX;
    uint32_t velocitySrvIndex    = UINT32_MAX;

    // GBuffer texture handles for barriers
    RHITextureHandle depthTexture;
    RHITextureHandle normalTexture;
    RHITextureHandle velocityTexture;

    // HDR scene color for prev-frame copy
    RHITextureHandle hdrSceneColor;
    uint32_t hdrSceneColorSrv    = UINT32_MAX;

    // PerFrameCB allocation (for b1 binding)
    RHIBufferHandle perFrameBuffer;
    uint32_t        perFrameOffset = 0;
    const void*     perFrameCpuPtr = nullptr;
    uint32_t        perFrameSize   = 0;

    // Frame state
    uint32_t frameIndex          = 0;
    uint32_t renderWidth         = 0;  ///< Full render resolution width.
    uint32_t renderHeight        = 0;  ///< Full render resolution height.
    bool worldGather = false;
    bool resetHistory = false;
    uint64_t sceneRevision = 0;
    /// World boxes where the distance field changed this frame (block B.2); read at execution.
    const std::vector<GlobalDistanceField::ChangedRegion>* changedRegions = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Screen-space ReSTIR GI (Lumen-parity phase C, r.GI.ReSTIR, opt-in)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants of the ReSTIR GI passes; mirrors `SSGIRestirRoot` (ssgi_restir_gi.hlsli).
/// 16 of the compute root signature's 24 root DWORDs.
struct SSGIRestirRootConstants {
    uint32_t depthSrv;
    uint32_t normalSrv;
    uint32_t reservoirInUav;
    uint32_t reservoirOutUav;
    uint32_t historyUav;
    uint32_t outputUav;
    uint32_t aoOutputUav;
    uint32_t packedDimensions;
    uint32_t frameIndex;
    uint32_t flags;
    uint32_t candidates;
    uint32_t maxHistory;
    uint32_t spatialSamples;
    uint32_t spatialRadiusBits;
    uint32_t reserved0;
    uint32_t reserved1;
};
static_assert(sizeof(SSGIRestirRootConstants) == 64, "SSGIRestirRoot is 16 root DWORDs");

/// One per-pixel reservoir (64 bytes); mirrors `SSGIReservoir` (ssgi_restir_gi.hlsli). The sample
/// is a direction with the radiance it returned from the reservoir's own surface (and the hit
/// distance, -1 escaped, -2 exhausted); normals are octahedral 2x16 UNORM.
struct SSGIRestirReservoir {
    float sampleDirection[3]; uint32_t flags;
    float sampleRadiance[3]; float sampleDistance;
    float surfacePosition[3]; uint32_t surfaceNormal;
    float weightSum; uint32_t M; float W; uint32_t surfaceGeometricNormal;
};
static_assert(sizeof(SSGIRestirReservoir) == 64, "SSGIReservoir is 64 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// SSGIRenderer
// ─────────────────────────────────────────────────────────────────────────────

/// Shared diffuse gather subsystem; owns trace, reconstruction and history textures.
///
/// # Ownership
/// Owns all GPU textures. Does NOT own IRHIDevice, PipelineLibrary, or
/// CBRingAllocator — all must outlive this object.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.
class SSGIRenderer {
public:
    SSGIRenderer() = default;
    ~SSGIRenderer();

    SSGIRenderer(const SSGIRenderer&) = delete;
    SSGIRenderer& operator=(const SSGIRenderer&) = delete;
    SSGIRenderer(SSGIRenderer&&) noexcept = default;
    SSGIRenderer& operator=(SSGIRenderer&&) noexcept = default;

    /// Create all SSGI GPU textures at the given render resolution.
    /// Called from SceneRenderer::Init().
    [[nodiscard]] bool CreateResources(IRHIDevice* device, uint32_t width, uint32_t height);

    /// Destroy all SSGI GPU textures. Called from SceneRenderer::Shutdown().
    void DestroyResources();

    /// Recreate textures at a new resolution. Called from SceneRenderer::Resize().
    void OnResize(uint32_t width, uint32_t height);

    /// Dispatch the selected diffuse gather and reconstruction for the current frame.
    ///
    /// Must be called after GBuffer + GTAO and before deferred lighting.
    /// The prev scene color copy is performed at the END of the previous
    /// frame's HDR rendering (before tonemap).
    void Render(IRHICommandList* cmdList, const SSGIRenderParams& params);
    [[nodiscard]] bool PrepareFrame(PipelineLibrary* pipelines, bool worldGather, bool screenSpace);

    /// Copy current HDR scene color to prev-frame buffer for next frame's use.
    /// Call this AFTER deferred lighting, BEFORE tonemap/post-process.
    void CopyPrevSceneColor(IRHICommandList* cmdList,
                            RHITextureHandle hdrSceneColor,
                            uint32_t width, uint32_t height);

    /// Returns the SRV index of the final upsampled SSGI indirect diffuse result.
    [[nodiscard]] uint32_t GetResultSrvIndex() const noexcept { return m_ssgiResultSrv; }

    /// Returns the SRV index of the final upsampled SSGI AO result.
    [[nodiscard]] uint32_t GetAOResultSrvIndex() const noexcept { return m_ssgiResultAOSrv; }

    /// Returns true if SSGI is enabled and resources are valid.
    [[nodiscard]] bool IsReady() const noexcept {
        return m_initialized && settings.enabled;
    }

    /// True once Render() has produced a valid output texture. Until then the
    /// result texture has never been written and must not be bound.
    [[nodiscard]] bool HasProducedResult() const noexcept { return m_hasProducedResult; }
    [[nodiscard]] bool HasPreviousSceneColor() const noexcept { return m_hasPrevColor; }

    // ── PassInspector accessors for intermediate textures ────────────────

    /// Returns the previous frame color texture handle (full-res copy).
    [[nodiscard]] RHITextureHandle GetPrevColorTexture() const noexcept { return m_prevSceneColor; }
    /// Returns the SRV index for the previous frame color texture.
    [[nodiscard]] uint32_t GetPrevColorSrvIndex() const noexcept { return m_prevSceneColorSrv; }

    /// Returns the raw trace output texture handle (half-res indirect diffuse).
    [[nodiscard]] RHITextureHandle GetTraceTexture() const noexcept { return m_ssgiTrace; }
    /// Returns the SRV index for the raw trace output.
    [[nodiscard]] uint32_t GetTraceSrvIndex() const noexcept { return m_ssgiTraceSrv; }

    /// Returns the spatially denoised output texture handle (half-res).
    [[nodiscard]] RHITextureHandle GetDenoisedTexture() const noexcept { return m_ssgiDenoised; }
    /// Returns the SRV index for the denoised output.
    [[nodiscard]] uint32_t GetDenoisedSrvIndex() const noexcept { return m_ssgiDenoisedSrv; }

    /// Returns the final upsampled result texture handle (full-res).
    [[nodiscard]] RHITextureHandle GetResultTexture() const noexcept { return m_ssgiResult; }

    /// Returns the final upsampled AO result texture handle (full-res).
    [[nodiscard]] RHITextureHandle GetAOResultTexture() const noexcept { return m_ssgiResultAO; }
    /// Returns the SRV index for the AO result.
    [[nodiscard]] uint32_t GetAOResultSrvForInspector() const noexcept { return m_ssgiResultAOSrv; }

    /// Returns the half-res width used by SSGI.
    [[nodiscard]] uint32_t GetHalfWidth() const noexcept { return m_halfWidth; }
    /// Returns the half-res height used by SSGI.
    [[nodiscard]] uint32_t GetHalfHeight() const noexcept { return m_halfHeight; }

    /// Public settings — modifiable per-frame before Render().
    SSGISettings settings;

private:
    [[nodiscard]] bool CreateTextures(uint32_t width, uint32_t height);
    void DestroyTextures();
    void LogGatherActivation(uint32_t samples);

    void DispatchPrevColorReduce(IRHICommandList* cmdList, const SSGIRenderParams& params);
    void DispatchTrace(IRHICommandList* cmdList, const SSGIRenderParams& params);
    /// Screen-space ReSTIR GI in place of DispatchTrace (r.GI.ReSTIR, world gather only): initial,
    /// temporal and spatial reuse + resolve into the same half-resolution trace textures.
    [[nodiscard]] bool EnsureRestirResources(PipelineLibrary* pipelines);
    void DestroyRestirBuffers();
    void DispatchRestir(IRHICommandList* cmdList, const SSGIRenderParams& params);
    void DispatchSpatialDenoise(IRHICommandList* cmdList, const SSGIRenderParams& params);
    void DispatchTemporalAccum(IRHICommandList* cmdList, const SSGIRenderParams& params);
    void DispatchUpsample(IRHICommandList* cmdList, const SSGIRenderParams& params);

    IRHIDevice* m_device = nullptr;

    // ── Textures ───────────────────────────────────────────────────────────

    /// Full-res copy of previous frame's HDR scene color (for reduction input).
    RHITextureHandle m_prevSceneColor;
    uint32_t m_prevSceneColorSrv = UINT32_MAX;

    /// Half-res reduced prev color (mip 0, Karis weighted).
    RHITextureHandle m_prevColorReduced;
    uint32_t m_prevColorReducedSrv = UINT32_MAX;
    uint32_t m_prevColorReducedUav = UINT32_MAX;

    /// Half-res raw trace output: indirect diffuse (RGBA16F).
    RHITextureHandle m_ssgiTrace;
    uint32_t m_ssgiTraceSrv = UINT32_MAX;
    uint32_t m_ssgiTraceUav = UINT32_MAX;

    /// Half-res raw trace output: AO (R16F).
    RHITextureHandle m_ssgiTraceAO;
    uint32_t m_ssgiTraceAOSrv = UINT32_MAX;
    uint32_t m_ssgiTraceAOUav = UINT32_MAX;

    /// Half-res spatially denoised: indirect diffuse (RGBA16F).
    RHITextureHandle m_ssgiDenoised;
    uint32_t m_ssgiDenoisedSrv = UINT32_MAX;
    uint32_t m_ssgiDenoisedUav = UINT32_MAX;

    /// Half-res spatially denoised: AO (R16F).
    RHITextureHandle m_ssgiDenoisedAO;
    uint32_t m_ssgiDenoisedAOSrv = UINT32_MAX;
    uint32_t m_ssgiDenoisedAOUav = UINT32_MAX;

    /// Half-res temporal history ping-pong: indirect diffuse (RGBA16F) x2.
    RHITextureHandle m_ssgiHistory[2];
    uint32_t m_ssgiHistorySrv[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t m_ssgiHistoryUav[2] = {UINT32_MAX, UINT32_MAX};

    /// Half-res temporal history ping-pong: AO (R16F) x2.
    RHITextureHandle m_ssgiHistoryAO[2];
    uint32_t m_ssgiHistoryAOSrv[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t m_ssgiHistoryAOUav[2] = {UINT32_MAX, UINT32_MAX};
    RHITextureHandle m_historyGeometry[2];
    uint32_t m_historyGeometrySrv[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t m_historyGeometryUav[2] = {UINT32_MAX, UINT32_MAX};

    /// Full-res final upsampled result: indirect diffuse (RGBA16F).
    RHITextureHandle m_ssgiResult;
    uint32_t m_ssgiResultSrv = UINT32_MAX;
    uint32_t m_ssgiResultUav = UINT32_MAX;

    /// Full-res final upsampled result: AO (R16F).
    RHITextureHandle m_ssgiResultAO;
    uint32_t m_ssgiResultAOSrv = UINT32_MAX;
    uint32_t m_ssgiResultAOUav = UINT32_MAX;

    /// ReSTIR GI reservoirs (half-res, 64 B each): this frame's initial candidates and the
    /// temporal ping-pong. Created on first opt-in use only, so OFF allocates nothing.
    RHIBufferHandle m_restirInitial;
    uint32_t m_restirInitialUav = UINT32_MAX;
    RHIBufferHandle m_restirHistory[2];
    uint32_t m_restirHistoryUav[2] = {UINT32_MAX, UINT32_MAX};
    int  m_restirIndex = 0;              ///< History buffer written this frame.
    bool m_restirHistoryValid = false;   ///< The other history buffer holds last frame's reservoirs.
    bool m_restirUnavailable = false;    ///< A reservoir or pipeline failed to create: stay on the plain gather, logged once.

    // ── State ──────────────────────────────────────────────────────────────

    int      m_historyIndex = 0;   ///< Ping-pong index (0 or 1).
    uint32_t m_halfWidth    = 0;
    uint32_t m_halfHeight   = 0;
    uint32_t m_fullWidth    = 0;
    uint32_t m_fullHeight   = 0;
    bool     m_initialized  = false;
    bool     m_hasPrevColor = false; ///< False on first frame (no valid prev scene color).
    bool     m_hasProducedResult = false; ///< False until Render() writes the result texture.
    bool m_resourceStatesInitialized = false;
    uint32_t m_loggedWorldSamples = 0;
    bool m_worldGather = false;
    bool m_hasHistory = false;
    uint64_t m_sceneRevision = 0;
    uint32_t m_lastFrame = UINT32_MAX;
    float m_previousViewProj[16] = {};
    /// Last frame's change boxes: the gather reads the radiance cache one frame late, so the
    /// history is rejected inside this frame's and last frame's boxes (block B.2).
    std::vector<GlobalDistanceField::ChangedRegion> m_previousRegions;

    static constexpr uint32_t kThreadGroupSize = 8;
};

} // namespace uz
