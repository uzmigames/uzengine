/// UzEngine Brixelizer GI Pass — AMD FidelityFX Brixelizer Global Illumination.
///
/// Integrates AMD FidelityFX Brixelizer (sparse SDF voxelization) with
/// BrixelizerGI (screen-space probe-based global illumination) to produce
/// spatially-varying diffuse and specular indirect lighting.
///
/// # Architecture
/// Uses the PIMPL pattern to hide FFX SDK types from the public header.
/// The FFX SDK 1.1.4 is compiled separately (CMake/MSVC) and linked as a
/// static library. When `UZ_BRIXELIZER_ENABLED` is not defined, the pass
/// compiles as a no-op stub.
///
/// # Output
/// Two RGBA16F textures at display resolution:
/// - `diffuseGI`  — indirect diffuse radiance (add to IBL diffuse)
/// - `specularGI` — indirect specular radiance (blend with IBL specular)
///
/// # CVar Control
/// - `r.BrixelizerGI`          — master toggle (default: false)
/// - `r.BrixelizerGI.Cascades` — number of cascades (default: 4, max 24)
/// - `r.BrixelizerGI.Intensity`— GI intensity multiplier (default: 1.0)
/// - `r.BrixelizerGI.Debug`    — debug mode (0=off, 1=radiance, 2=irradiance)
///
/// @ref AMD 2024 "FidelityFX Brixelizer GI"

#pragma once

#include "rhi_types.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;
struct DrawCommand;

// ─────────────────────────────────────────────────────────────────────────────
// BrixelizerGIPass
// ─────────────────────────────────────────────────────────────────────────────

/// AMD FidelityFX Brixelizer GI integration pass.
///
/// # Lifecycle
/// ```cpp
/// BrixelizerGIPass pass;
/// pass.Init(device, width, height);
/// // Per frame:
/// pass.RegisterGeometry(drawList);
/// pass.Execute(cmdList, viewProj, invViewProj, cameraPos, ...);
/// uint32_t diffSrv = pass.GetDiffuseGISrv();
/// uint32_t specSrv = pass.GetSpecularGISrv();
/// // Shutdown:
/// pass.Shutdown();
/// ```
class BrixelizerGIPass {
public:
    BrixelizerGIPass();
    ~BrixelizerGIPass();

    BrixelizerGIPass(const BrixelizerGIPass&) = delete;
    BrixelizerGIPass& operator=(const BrixelizerGIPass&) = delete;
    BrixelizerGIPass(BrixelizerGIPass&&) noexcept;
    BrixelizerGIPass& operator=(BrixelizerGIPass&&) noexcept;

    /// Initializes FFX Brixelizer and BrixelizerGI contexts.
    ///
    /// Creates the FfxInterface DX12 backend, allocates scratch memory,
    /// creates Brixelizer context with cascaded SDF voxels, and creates
    /// the BrixelizerGI context for screen-space probe GI.
    ///
    /// # Returns
    /// `true` if initialization succeeded.
    [[nodiscard]] bool Init(IRHIDevice* device, uint32_t displayWidth, uint32_t displayHeight);

    /// Destroys both FFX contexts and frees all resources.
    void Shutdown();

    /// Returns true if both FFX contexts are created and ready.
    [[nodiscard]] bool IsInitialized() const;

    /// Registers geometry from the draw list for SDF voxelization.
    ///
    /// Translates UzEngine DrawCommand entries into FfxBrixelizerInstanceDescription
    /// structs. Static geometry is registered once; dynamic geometry is re-registered
    /// every frame.
    void RegisterGeometry(const std::vector<DrawCommand>& drawList);

    /// Executes the Brixelizer SDF bake and GI dispatch.
    ///
    /// GBuffer texture handles are required (not SRV indices) because the FFX SDK
    /// needs to wrap the underlying ID3D12Resource* for each input texture.
    void Execute(
        IRHICommandList* cmdList,
        const float* viewMatrix,
        const float* projMatrix,
        const float* prevView,
        const float* prevProj,
        float cameraPosX, float cameraPosY, float cameraPosZ,
        uint32_t frameIndex,
        RHITextureHandle depthTexture,
        RHITextureHandle normalTexture,
        RHITextureHandle velocityTexture,
        RHITextureHandle envMapTexture,
        RHITextureHandle prevLitTexture);

    /// Handles window/viewport resize — recreates GI output textures.
    void OnResize(uint32_t newWidth, uint32_t newHeight);

    /// Returns the SRV index for the diffuse GI output texture.
    /// UINT32_MAX if not initialized or disabled.
    [[nodiscard]] uint32_t GetDiffuseGISrv() const;

    /// Returns the SRV index for the specular GI output texture.
    /// UINT32_MAX if not initialized or disabled.
    [[nodiscard]] uint32_t GetSpecularGISrv() const;

    /// Returns the UAV index for the diffuse GI output texture.
    [[nodiscard]] uint32_t GetDiffuseGIUav() const;

    /// Returns the UAV index for the specular GI output texture.
    [[nodiscard]] uint32_t GetSpecularGIUav() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace uz
