/// GBuffer — Manages deferred rendering targets (albedo+AO, packed normal+metal+rough, depth, velocity).
///
/// Owns four GPU textures and their SRV heap indices for the lighting and post-process passes.
/// Provides a Create/Resize/Destroy lifecycle tied to an `IRHIDevice`.
///
/// # Thread Safety
/// Not thread-safe. All calls must happen on the render thread.

#pragma once

#include "rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;

/// Deferred rendering geometry buffer.
///
/// Allocates and manages the render targets used by the GBuffer pass:
/// - **Albedo** (RGBA8Unorm) — base color (RGB) + ambient occlusion (A)
/// - **Normal** (RGBA8Unorm) — octahedral-encoded normal (RG) + metallic (B) + roughness (A)
/// - **Depth** (D32Float) — hardware depth buffer
/// - **Velocity** (RG16Float) — screen-space motion vectors for TAA/motion blur
///
/// Color and velocity targets are created with `RenderTarget | ShaderResource` usage
/// so they can be written in their respective passes and read as SRVs downstream.
/// The depth target uses `DepthStencil | ShaderResource`.
///
/// # Lifecycle
/// ```
/// GBuffer gb;
/// gb.Create(device, 1920, 1080);   // allocate
/// // ... render frames ...
/// gb.Resize(device, 2560, 1440);   // window resize
/// // ... render more ...
/// gb.Destroy(device);              // release GPU memory
/// ```
///
/// # Move Semantics
/// GBuffer is movable but not copyable. Moving transfers ownership of all
/// GPU handles — the source becomes invalid after the move.
class GBuffer {
public:
    GBuffer() = default;
    ~GBuffer() = default;

    GBuffer(const GBuffer&) = delete;
    GBuffer& operator=(const GBuffer&) = delete;

    /// Move-constructs a GBuffer, transferring all texture handles and SRV indices.
    /// The source is left in a default (invalid) state.
    GBuffer(GBuffer&& other) noexcept;

    /// Move-assigns a GBuffer, transferring all texture handles and SRV indices.
    /// The source is left in a default (invalid) state.
    ///
    /// # Warning
    /// The caller must `Destroy()` the current GBuffer before move-assigning
    /// over it, or the old textures will leak.
    GBuffer& operator=(GBuffer&& other) noexcept;

    /// Creates all four GBuffer textures at the given resolution.
    ///
    /// # Arguments
    /// * `device` — RHI device used to allocate GPU textures. Must not be null.
    /// * `width` — Render target width in texels. Must be > 0.
    /// * `height` — Render target height in texels. Must be > 0.
    ///
    /// # Returns
    /// `true` if all four textures and their SRVs were created successfully.
    /// `false` if any allocation failed — in that case, all partial resources
    /// are cleaned up and the GBuffer is left in an invalid state.
    ///
    /// # Side Effects
    /// Logs `Renderer::Info` on success, `Renderer::Error` on failure.
    [[nodiscard]] bool Create(IRHIDevice* device, uint32_t width, uint32_t height);

    /// Destroys and recreates the GBuffer at a new resolution.
    ///
    /// Equivalent to `Destroy(device); Create(device, newWidth, newHeight);`.
    ///
    /// # Arguments
    /// * `device` — RHI device. Must be the same device used for `Create()`.
    /// * `newWidth` — New render target width in texels. Must be > 0.
    /// * `newHeight` — New render target height in texels. Must be > 0.
    ///
    /// # Returns
    /// `true` if recreation succeeded, `false` otherwise.
    [[nodiscard]] bool Resize(IRHIDevice* device, uint32_t newWidth, uint32_t newHeight);

    /// Releases all GPU textures and resets handles/SRV indices to defaults.
    ///
    /// Safe to call on an already-destroyed or never-created GBuffer (no-op).
    ///
    /// # Arguments
    /// * `device` — RHI device. Must be the same device used for `Create()`.
    void Destroy(IRHIDevice* device);

    /// Returns true if all four textures are allocated and valid.
    ///
    /// # Returns
    /// `true` if albedo, normal, depth, and velocity handles are all valid.
    [[nodiscard]] bool IsValid() const;

    // ─────────────────────────────────────────────────────────────────────────
    // Texture handles — written by GBuffer pass, read by lighting pass
    // ─────────────────────────────────────────────────────────────────────────

    RHITextureHandle albedo;       ///< RGBA8Unorm — base color (RGB) + ambient occlusion (A)
    RHITextureHandle normal;       ///< RGBA8Unorm — octahedral normal (RG) + metallic (B) + roughness (A)
    RHITextureHandle depth;        ///< D32Float — depth buffer
    RHITextureHandle velocity;     ///< RG16Float — screen-space motion vectors (TAA / motion blur)
    RHITextureHandle positionWS;   ///< RGBA32Float — world-space position (RGB), debug/verification target

    // ─────────────────────────────────────────────────────────────────────────
    // SRV indices — bindless descriptor heap indices for lighting pass reads
    // ─────────────────────────────────────────────────────────────────────────

    uint32_t albedoSRV      = UINT32_MAX;    ///< Bindless SRV index for albedo texture.
    uint32_t normalSRV      = UINT32_MAX;    ///< Bindless SRV index for normal texture.
    uint32_t depthSRV       = UINT32_MAX;    ///< Bindless SRV index for depth texture.
    uint32_t velocitySRV    = UINT32_MAX;    ///< Bindless SRV index for velocity texture.
    uint32_t positionWSSRV  = UINT32_MAX;    ///< Bindless SRV index for world-space position texture.

    uint32_t width = 0;   ///< Current render target width in texels.
    uint32_t height = 0;  ///< Current render target height in texels.
};

} // namespace uz
