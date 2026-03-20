/// UzEngine Cloud Pass — Volumetric Cloud Rendering Pipeline (3 Compute Shaders)
///
/// Implements a volumetric cloud system using:
/// 1. Precomputed 3D Noise — loaded from disk at startup (shape 128^3, detail 32^3)
/// 2. Weather Map (512^2 RGBA8) — updated each frame via compute (wind-animated)
/// 3. Ray March (half-res RGBA16F) — main cloud density/lighting integration
/// 4. Temporal Filter (half-res RGBA16F) — EMA temporal reprojection
///
/// The final cloud output is a premultiplied-alpha RGBA16F texture at half display
/// resolution, composited over the scene after deferred lighting and before tonemap.
///
/// @ref CloudGlobalShaderParams
/// @ref MainCommon tracing loop
/// @ref Temporal reconstruction
/// @source Schneider & Vos 2015 — Noise channel layout and density model

#pragma once

#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;
class CBRingAllocator;

// ─────────────────────────────────────────────────────────────────────────────
// CloudPassInput — per-frame parameters for cloud rendering
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame input data for the cloud pass.
///
/// All spatial values are in km unless noted. The caller is responsible for
/// converting engine units (cm) to km before filling this struct.
///
/// @ref parameter layout
struct CloudPassInput {
    float    invViewProj[16];      ///< Current frame inverse view-projection matrix (column-major)
    float    prevViewProj[16];     ///< Previous frame view-projection matrix (for temporal reprojection)
    float    cameraPos[3];         ///< Camera world position in km (engine cm * 0.00001)
    float    sunDir[3];            ///< Normalized sun direction (world space)
    float    sunColor[3];          ///< Sun color * intensity (pre-exposed luminance)
    float    ambientColor[3];      ///< Sky ambient color for multi-scatter approximation
    float    time;                 ///< Elapsed time in seconds (for wind animation)
    float    coverage;             ///< Global cloud coverage [0,1]
    float    density;              ///< Cloud density multiplier
    float    extinction;           ///< Extinction coefficient (1/km)
    float    scatteringAlbedo;     ///< Single-scattering albedo [0,1]
    float    phaseG;               ///< HG phase eccentricity — forward lobe (e.g. 0.8)
    float    phaseG2;              ///< HG phase eccentricity — back lobe (e.g. -0.3)
    float    phaseBlend;           ///< Blend factor between HG lobes [0,1]
    float    layerBottomKm;        ///< Cloud layer bottom altitude (km above planet center)
    float    layerHeightKm;        ///< Cloud layer thickness (km)
    float    windVelocity[3];      ///< Wind direction * speed (km/s)
    float    detailWindScale;      ///< Detail noise wind speed multiplier
    float    ambientMultiplier;    ///< Multi-scatter ambient intensity
    uint32_t primarySteps;         ///< Number of primary ray march steps (e.g. 64)
    uint32_t lightSteps;           ///< Number of light march steps per sample (e.g. 6)
    uint32_t depthSrv;             ///< Bindless SRV index for scene depth buffer
    float    globalCoverage;       ///< Global coverage for weather map generation [0,1]
    float    temporalBlendFactor;  ///< Base temporal blend factor (default 0.05)
};

// ─────────────────────────────────────────────────────────────────────────────
// CloudPass
// ─────────────────────────────────────────────────────────────────────────────

/// GPU volumetric cloud rendering pipeline.
///
/// Owns all GPU textures (noise, weather map, cloud output) and compute
/// pipelines. Does NOT own the IRHIDevice, ShaderManager, or CBRingAllocator
/// — all must outlive this object.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.
///
/// # Usage
/// ```cpp
/// CloudPass clouds;
/// clouds.Init(device, &shaderMgr, width, height);
/// // Each frame, after deferred lighting, before tonemap:
/// clouds.Execute(cmdList, cbRing, input);
/// uint32_t cloudSrv = clouds.GetCloudOutputSrv();
/// ```
class CloudPass {
public:
    CloudPass() = default;
    ~CloudPass();

    CloudPass(const CloudPass&) = delete;
    CloudPass& operator=(const CloudPass&) = delete;
    CloudPass(CloudPass&&) = delete;
    CloudPass& operator=(CloudPass&&) = delete;

    /// Initializes GPU resources and compiles compute shaders.
    ///
    /// Creates noise textures (3D shape 128^3, 3D detail 32^3), weather map
    /// (2D 512^2), and two half-res ping-pong cloud output textures (RGBA16F).
    /// Lazily compiles 5 compute pipelines on first use.
    ///
    /// # Arguments
    /// * `device` — Initialized RHI device. Must outlive this object.
    /// * `shaderMgr` — Initialized shader manager. Must outlive this object.
    /// * `displayWidth` — Full display resolution width in pixels.
    /// * `displayHeight` — Full display resolution height in pixels.
    ///
    /// # Returns
    /// `true` on success, `false` if resource creation fails.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr,
                            uint32_t displayWidth, uint32_t displayHeight);

    /// Releases all GPU resources (textures, pipelines, shaders).
    void Shutdown();

    /// Recreates half-res cloud output textures for a new display resolution.
    ///
    /// Noise textures and weather map are resolution-independent and are not
    /// affected by resize.
    void OnResize(uint32_t displayWidth, uint32_t displayHeight);

    /// Loads precomputed noise textures from disk.
    ///
    /// Reads `cloud_shape.bin` (128^3 R8) and `cloud_shape_detail.bin` (32^3 R8)
    /// from the `assets/` directory next to the executable. Expands single-channel
    /// R8 data to RGBA8 (replicating the value to all four channels), uploads to
    /// the GPU via staging buffers, and transitions textures to SRV state.
    ///
    /// If noise is already loaded, this is a no-op.
    ///
    /// The precomputed .bin files are MIT-licensed (three-geospatial).
    void LoadNoiseTextures(IRHICommandList* cmd);

    /// Executes the full cloud rendering pipeline for one frame.
    ///
    /// Call after deferred lighting, before tonemap. Dispatches:
    /// 1. Weather map update (wind-animated, 512^2)
    /// 2. Ray march at half-res
    /// 3. Temporal filter (blend with history)
    /// 4. Flip ping-pong index
    ///
    /// If noise textures are not yet baked, they are baked first.
    ///
    /// # Arguments
    /// * `cmd` — Command list in recording state.
    /// * `cbRing` — Per-frame constant buffer ring allocator.
    /// * `input` — Per-frame cloud parameters.
    void Execute(IRHICommandList* cmd, CBRingAllocator& cbRing,
                 const CloudPassInput& input);

    /// Returns the SRV index of the final cloud output (RGBA16F, half-res).
    /// Valid only after Execute() has been called at least once.
    /// The output is premultiplied alpha: rgb = cloud color * alpha, a = transmittance.
    [[nodiscard]] uint32_t GetCloudOutputSrv() const noexcept;

    /// Returns true if Init() has been called and Shutdown() has not.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    // ── Pipeline creation (lazy, on first Execute) ──────────────────────
    [[nodiscard]] bool EnsureWeatherMapPipeline();
    [[nodiscard]] bool EnsureRaymarchPipeline();
    [[nodiscard]] bool EnsureTemporalPipeline();

    /// Loads a single R8 raw binary file from disk, expands to RGBA8, and
    /// uploads to a 3D GPU texture via a staging buffer.
    [[nodiscard]] bool LoadAndUploadNoise3D(IRHICommandList* cmd,
                                            const char* filename,
                                            RHITextureHandle dstTexture,
                                            uint32_t dim,
                                            const char* debugLabel);

    // ── Texture management ──────────────────────────────────────────────
    [[nodiscard]] bool CreateNoiseTextures();
    [[nodiscard]] bool CreateWeatherMapTexture();
    [[nodiscard]] bool CreateCloudOutputTextures(uint32_t halfW, uint32_t halfH);
    void DestroyNoiseTextures();
    void DestroyWeatherMapTexture();
    void DestroyCloudOutputTextures();

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;

    bool m_initialized  = false;
    bool m_noisesLoaded = false;

    // ── Noise Textures (loaded from disk once) ──────────────────────────
    RHITextureHandle m_shapeNoise;                       ///< 128^3 RGBA8 3D texture
    uint32_t         m_shapeNoiseSrv = UINT32_MAX;

    RHITextureHandle m_detailNoise;                      ///< 32^3 RGBA8 3D texture
    uint32_t         m_detailNoiseSrv = UINT32_MAX;

    // ── Staging buffers for noise upload (kept alive until GPU copy completes) ──
    std::vector<RHIBufferHandle> m_noiseStagingBuffers;

    // ── Weather Map (updated per frame) ─────────────────────────────────
    RHITextureHandle m_weatherMap;                       ///< 512^2 RGBA8 2D texture
    uint32_t         m_weatherMapSrv = UINT32_MAX;
    uint32_t         m_weatherMapUav = UINT32_MAX;

    // ── Cloud Output (half-res, ping-pong for temporal) ─────────────────
    RHITextureHandle m_cloudOutput[2];                   ///< RGBA16F at half display res
    uint32_t         m_cloudOutputSrv[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t         m_cloudOutputUav[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t         m_cloudHistoryIndex = 0;            ///< Ping-pong: 0 or 1
    uint32_t         m_halfWidth  = 0;
    uint32_t         m_halfHeight = 0;

    // ── Compute Pipelines (lazily created) ──────────────────────────────
    RHIPipelineHandle m_weatherMapPipeline  = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_raymarchPipeline    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_temporalPipeline    = RHIPipelineHandle::Invalid();

    // ── Shader Handles ──────────────────────────────────────────────────
    RHIShaderHandle m_weatherMapCS  = RHIShaderHandle::Invalid();
    RHIShaderHandle m_raymarchCS    = RHIShaderHandle::Invalid();
    RHIShaderHandle m_temporalCS    = RHIShaderHandle::Invalid();

    // ── State tracking ──────────────────────────────────────────────────
    bool m_initialCloudBarriersDone  = false;  ///< First-frame Common→UAV for cloud outputs
    bool m_initialWeatherBarrierDone = false;   ///< First-frame Common→UAV for weather map
    uint32_t m_frameCount = 0;                  ///< Total frames executed (for temporal history)

    // ── Constants ───────────────────────────────────────────────────────
    static constexpr uint32_t kGroupSize         = 8;   ///< Compute thread group size (8x8x1 for 2D)
    static constexpr uint32_t kShapeNoiseSize    = 128; ///< Shape noise 3D texture dimension
    static constexpr uint32_t kDetailNoiseSize   = 32;  ///< Detail noise 3D texture dimension
    static constexpr uint32_t kWeatherMapSize    = 512; ///< Weather map 2D texture dimension
};

} // namespace uz
