/// UzEngine DecalComponent — ECS data for deferred DBuffer decals.
///
/// This is a projection volume + material reference for deferred decals.
/// The DecalComponent does NOT store textures or material properties directly.
/// Instead, it references a MaterialInterface* (with domain == Decal) that
/// defines the visual appearance. The renderer reads texture SRV indices and
/// scalar parameters from the material during the decal gather step.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer technique
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer decal pattern
///
/// # How Decals Work
/// A unit cube [-0.5, 0.5]^3 is transformed by the decal's world matrix to form
/// an oriented bounding box in world space. The pixel shader reconstructs world
/// position from depth, projects it into the decal's local space via the inverse
/// decal matrix, and samples decal textures at the projected UV coordinates.
///
/// # Usage
/// ```cpp
/// auto entity = world.CreateEntity();
/// auto& decal = world.AddComponent<DecalComponent>(entity);
///
/// auto* mat = new Material();
/// mat->materialDomain = EMaterialDomain::Decal;
/// mat->albedoPath = "textures/decals/bullet_hole.png";
/// mat->normalPath = "textures/decals/bullet_hole_normal.png";
/// mat->opacity = 0.9f;
/// decal.material = mat;
/// ```

#pragma once

#include <cstdint>

namespace uz {

class MaterialInterface;

/// Deferred DBuffer decal component.
///
/// Stores the projection volume configuration and a material reference.
/// The material defines textures, blend mode, opacity, and all visual properties.
/// The renderer gathers all enabled DecalComponent entities, sorts by priority,
/// and renders them after the GBuffer fill pass.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall"
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall"
struct DecalComponent {
    // ── Material Reference ───────────────────────────────────────────────

    /// The decal material. Must have materialDomain == EMaterialDomain::Decal.
    /// The renderer reads albedo/normal/roughness SRV indices, opacity,
    /// normalBlend, decalBlendMode, and other parameters from this material.
    ///
    /// Non-owning pointer — the Material is owned by the scene loader's
    /// material storage (s_sceneLoadedMaterials) or by the game code.
    /// Null = decal has no visual effect (skipped during rendering).
    MaterialInterface* material = nullptr;

    // ── Sorting & Lifetime ───────────────────────────────────────────────

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
