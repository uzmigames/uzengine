/// UzEngine Cloud Pass — Volumetric cloud ray-march + composite pipeline.
///
/// Dispatches a compute shader to ray-march through a spherical cloud shell,
/// producing a half-resolution RGBA16F texture with premultiplied-alpha cloud
/// color (RGB) and transmittance (A). The result is composited over the HDR
/// scene by a fullscreen triangle pass in frame_lighting.cpp.
///
/// # Cloud Layer Model
/// Clouds occupy a spherical shell between two altitudes above sea level:
/// ```
/// innerRadius = planetRadius + layerBottomKm
/// outerRadius = innerRadius  + layerHeightKm
/// ```
///
/// # Scattering Model
/// @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
/// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
/// Single-scattering with dual-lobe Henyey-Greenstein phase function.
/// Frostbite analytical integration: (S - S*T) / max(ext, eps).
/// Premultiplied alpha composite: final = cloud.rgb + scene.rgb * transmittance.
///
/// # GPU Resources
/// - Cloud output: half-resolution RGBA16F (SRV + UAV).
/// - 3D shape noise: 128^3 RGBA8 (loaded from content/atmosphere/shape.bin).
///
/// # Integration
/// Created and owned by SceneRenderer. The cloud SRV index is passed to the
/// CloudComposite render graph pass in frame_lighting.cpp.

#pragma once

#include "engine/rhi/rhi_types.h"
#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;
class CBRingAllocator;

/// Input parameters for a single cloud dispatch, filled from VolumetricCloudsComponent
/// and per-frame scene data in frame_lighting.cpp.
struct CloudPassInput {
    float    invViewProj[16];
    float    prevViewProj[16];
    float    cameraPos[3];
    float    sunDir[3];
    float    sunColor[3];
    float    ambientColor[3];
    float    time;
    float    coverage;
    float    density;
    float    extinction;
    float    scatteringAlbedo;
    float    phaseG;
    float    phaseG2;
    float    phaseBlend;
    float    layerBottomKm;
    float    layerHeightKm;
    float    windVelocity[3];
    float    detailWindScale;
    float    ambientMultiplier;
    uint32_t primarySteps;
    uint32_t lightSteps;
    uint32_t depthSrv;
    float    globalCoverage;
    float    temporalBlendFactor;
};

/// Volumetric cloud ray-march pass.
///
/// Owns GPU resources (textures, pipelines, staging buffers) for the cloud
/// compute dispatch. Lifetime is managed by SceneRenderer (Init on scene load,
/// Shutdown on scene unload or renderer destruction).
class CloudPass {
public:
    CloudPass() = default;
    ~CloudPass();

    CloudPass(const CloudPass&) = delete;
    CloudPass& operator=(const CloudPass&) = delete;
    CloudPass(CloudPass&&) = delete;
    CloudPass& operator=(CloudPass&&) = delete;

    /// Initializes the cloud pass: creates half-res output texture + compute pipeline.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation.
    /// * `shaderMgr` — Shader manager for compute shader compilation.
    /// * `width` — Full-resolution render width (cloud output is half-res).
    /// * `height` — Full-resolution render height.
    ///
    /// # Returns
    /// `true` if all resources and pipelines were created successfully.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr,
                            uint32_t width, uint32_t height);

    /// Releases all GPU resources.
    void Shutdown();

    /// Recreates half-res textures when the render target size changes.
    void OnResize(uint32_t width, uint32_t height);

    /// Loads 3D noise textures from disk (content/atmosphere/shape.bin) and uploads to GPU.
    /// Must be called on a command list that will be submitted before Execute().
    void LoadNoiseTextures(IRHICommandList* cmdList);

    /// Dispatches the cloud ray-march compute shader.
    ///
    /// The command list must be in recording state (Begin called, End not yet).
    /// Manages all internal barriers (UAV transitions).
    void Execute(IRHICommandList* cmdList, CBRingAllocator& cbRing,
                 const CloudPassInput& input);

    /// Returns the SRV descriptor index of the cloud output texture.
    /// UINT32_MAX if not initialized.
    [[nodiscard]] uint32_t GetCloudOutputSrv() const noexcept { return m_outputSrvIndex; }

    /// Returns the cloud output texture handle (for render graph registration).
    [[nodiscard]] RHITextureHandle GetCloudOutputTexture() const noexcept { return m_outputTexture; }

    /// Returns a debug raymarch output texture (unused, returns invalid handle).
    [[nodiscard]] RHITextureHandle GetDebugRaymarchOutput(uint32_t) const noexcept { return {}; }

    /// Returns a debug raymarch output SRV (unused, returns UINT32_MAX).
    [[nodiscard]] uint32_t GetDebugRaymarchOutputSrv(uint32_t) const noexcept { return UINT32_MAX; }

    /// Returns true if Init() completed successfully.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    /// Returns true if noise textures have been uploaded to the GPU.
    [[nodiscard]] bool HasNoiseTextures() const noexcept { return m_hasNoiseTextures; }

private:
    /// Creates the half-res output texture (RGBA16F, UAV+SRV).
    bool CreateOutputTexture(uint32_t halfWidth, uint32_t halfHeight);

    /// Destroys the output texture and resets descriptor indices.
    void DestroyOutputTexture();

    /// Lazily creates the compute pipeline on first use.
    bool EnsureRaymarchPipeline();

    // ── Device + shader manager references ──
    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;

    // ── Output texture (half-res RGBA16F) ──
    RHITextureHandle m_outputTexture   = RHITextureHandle::Invalid();
    uint32_t         m_outputSrvIndex  = UINT32_MAX;
    uint32_t         m_outputUavIndex  = UINT32_MAX;
    uint32_t         m_halfWidth       = 0;
    uint32_t         m_halfHeight      = 0;

    // ── Compute pipeline ──
    RHIShaderHandle   m_raymarchCS       = RHIShaderHandle::Invalid();
    RHIPipelineHandle m_raymarchPipeline = RHIPipelineHandle::Invalid();

    // ── 3D shape noise texture (128^3 RGBA8) ──
    RHITextureHandle m_shapeNoiseTexture = RHITextureHandle::Invalid();
    uint32_t         m_shapeNoiseSrvIndex = UINT32_MAX;

    // ── Staging buffers kept alive until GPU finishes ──
    std::vector<RHIBufferHandle> m_stagingBuffers;

    // ── State flags ──
    bool m_initialized          = false;
    bool m_hasNoiseTextures     = false;
    bool m_initialBarriersDone  = false;
};

} // namespace uz
