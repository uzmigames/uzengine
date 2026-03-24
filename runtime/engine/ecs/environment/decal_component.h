/// UzEngine DecalComponent — ECS data for deferred DBuffer decals.
///
/// Defines an oriented bounding box (OBB) projector that modifies GBuffer data
/// (albedo, normal, roughness/metallic/AO) before the deferred lighting pass.
/// This is the DBuffer decal technique used in modern engines.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer technique
/// @ref Pesce 2014 "Rendering Decals in Screen Space"
///
/// # How Decals Work
/// A unit cube [-0.5, 0.5]^3 is transformed by the decal's world matrix to form
/// an oriented bounding box in world space. The pixel shader reconstructs world
/// position from depth, projects it into the decal's local space via the inverse
/// decal matrix, and samples decal textures at the projected UV coordinates.
/// Pixels outside the [0,1]^3 local volume are discarded.
///
/// The decal writes to three DBuffer render targets that are later applied to
/// the GBuffer via a fullscreen compute pass (lerp blend).
///
/// # Multiple Decals
/// Decals are sorted by priority (ascending) and rendered with alpha blending
/// into the DBuffer. Higher priority decals render last and appear on top.
///
/// # Usage
/// ```cpp
/// auto entity = world.CreateEntity();
/// auto& decal = world.AddComponent<DecalComponent>(entity);
/// decal.albedoTextureId = renderer.QueueTextureLoad("textures/bullet_hole.png");
/// decal.opacity = 0.9f;
/// decal.extents = {20.0f, 20.0f, 5.0f};  // 40x40 cm face, 10 cm depth
/// ```

#pragma once

#include <cstdint>

namespace uz {

/// Deferred DBuffer decal component.
///
/// Stores the configuration for a single decal projector. The renderer gathers
/// all enabled DecalComponent entities, sorts by priority, and renders them
/// into the DBuffer before the deferred lighting pass.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall"
/// @ref Pesce 2014 "Rendering Decals in Screen Space"
struct DecalComponent {
    // ── Projection Volume ─────────────────────────────────────────────────

    /// Half-extents of the decal OBB in centimeters (local X, Y, Z).
    /// The decal projects along its local Z axis onto surfaces within the box.
    /// A decal with extents {25, 25, 5} creates a 50x50 cm face with 10 cm depth.
    /// @ref Standard decal projector — default 25 cm half-extent (50 cm face)
    float extentsX = 25.0f;
    float extentsY = 25.0f;
    float extentsZ = 5.0f;

    // ── Textures ──────────────────────────────────────────────────────────

    /// Bindless SRV index for the decal albedo texture (RGBA8).
    /// RGB = decal color, A = per-texel opacity mask.
    /// UINT32_MAX = no albedo texture (decal does not modify albedo).
    uint32_t albedoTextureId = UINT32_MAX;

    /// Bindless SRV index for the decal normal map texture (RGBA8).
    /// Tangent-space normal map blended into the GBuffer normal.
    /// UINT32_MAX = no normal modification.
    uint32_t normalTextureId = UINT32_MAX;

    /// Bindless SRV index for the decal roughness/metallic/AO texture (RGBA8).
    /// R = roughness, G = metallic, B = AO override.
    /// UINT32_MAX = no material modification.
    uint32_t materialTextureId = UINT32_MAX;

    // ── Material Properties ───────────────────────────────────────────────

    /// Global opacity multiplier [0, 1]. Multiplied with the albedo texture alpha.
    /// 0.0 = invisible, 1.0 = fully opaque decal.
    /// @ref Standard decal opacity — default 1.0 (fully opaque)
    float opacity = 1.0f;

    /// Normal blend intensity [0, 1]. Controls how much the decal normal
    /// overrides the surface normal.
    /// 0.0 = no normal modification, 1.0 = full normal replacement.
    /// @ref Standard decal normal blend — default 1.0
    float normalBlend = 1.0f;

    /// Roughness override value [0, 1] when materialTextureId is UINT32_MAX.
    /// When a material texture is bound, this multiplies the red channel.
    /// @ref Standard PBR roughness — default 0.5
    float roughness = 0.5f;

    /// Metallic override value [0, 1] when materialTextureId is UINT32_MAX.
    /// @ref Standard PBR metallic — default 0.0
    float metallic = 0.0f;

    // ── Sorting & Lifetime ────────────────────────────────────────────────

    /// Sort priority. Higher values render later (appear on top).
    /// Decals with the same priority have undefined relative order.
    /// @ref Standard decal sort order — default 0
    int32_t priority = 0;

    /// Fade distance in centimeters from the OBB boundary.
    /// Pixels within this distance from the edge of the projection volume
    /// are faded to prevent hard cutoff at decal boundaries.
    /// 0.0 = hard edge, positive = smooth fade.
    /// @ref Standard decal fade — default 5.0 cm
    float fadeDistance = 5.0f;

    /// Angle fade — maximum angle (degrees) between surface normal and decal
    /// projection direction beyond which the decal is fully faded.
    /// Prevents decals from stretching on surfaces nearly parallel to projection.
    /// 0 = no angle fade, 90 = fade starts at 90 degrees (no fade).
    /// @ref Standard decal angle fade — default 60 degrees
    float angleFadeStart = 60.0f;

    /// Whether this decal is enabled. Disabled decals are skipped during gathering.
    bool enabled = true;
};

} // namespace uz
