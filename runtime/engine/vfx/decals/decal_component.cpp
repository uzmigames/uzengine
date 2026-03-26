/// VfxDecal implementation -- projection matrix, tick, fade, and lifetime.

#include "engine/vfx/decals/decal_component.h"

#include "engine/logging/log.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DecalBlendModeToString
// ─────────────────────────────────────────────────────────────────────────────

const char* DecalBlendModeToString(DecalBlendMode mode) noexcept {
    switch (mode) {
    case DecalBlendMode::Opaque:   return "Opaque";
    case DecalBlendMode::Additive: return "Additive";
    case DecalBlendMode::Multiply: return "Multiply";
    case DecalBlendMode::Screen:   return "Screen";
    }
    return "Unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// GetProjectionMatrix -- world-to-decal-local normalized transform
// ─────────────────────────────────────────────────────────────────────────────

Mat4 VfxDecal::GetProjectionMatrix() const {
    // Step 1: translate so the OBB center is at the origin.
    const Mat4 translate = Mat4::Translation(-position);

    // Step 2: rotate into decal-local space (undo the decal's orientation).
    const Mat4 rotate = rotation.Inverse().ToMatrix();

    // Step 3: scale into [-1,1] normalized coordinates.
    // Each axis is divided by the corresponding half-extent so that the
    // OBB boundary maps exactly to +/-1.
    const Vec3 invExtents{
        1.0f / halfExtents.x,
        1.0f / halfExtents.y,
        1.0f / halfExtents.z,
    };
    const Mat4 scale = Mat4::Scaling(invExtents);

    // Combined: world point -> translated -> rotated -> scaled.
    // Matrix multiply order follows DirectX row-vector convention:
    // point * translate * rotate * scale
    return translate * rotate * scale;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick -- advance age and compute linear fade
// ─────────────────────────────────────────────────────────────────────────────

void VfxDecal::Tick(float dt) {
    if (!isActive) {
        return;
    }

    age += dt;

    if (age >= lifetime) {
        fadeAlpha = 0.0f;
        isActive = false;
        UZ_LOG(VFX, Debug, "Decal expired: material='{}'", materialPath);
        return;
    }

    // Linear fade: 1.0 at age=0, 0.0 at age=lifetime.
    fadeAlpha = 1.0f - (age / lifetime);
}

// ─────────────────────────────────────────────────────────────────────────────
// FadeOut -- begin early fade from current state
// ─────────────────────────────────────────────────────────────────────────────

void VfxDecal::FadeOut(float duration) {
    if (duration <= 0.0f) {
        // Immediate expiry when duration is zero or negative.
        fadeAlpha = 0.0f;
        isActive = false;
        UZ_LOG(VFX, Debug, "Decal fade-out immediate: material='{}'", materialPath);
        return;
    }

    // Shift lifetime so the decal has exactly `duration` seconds left.
    lifetime = age + duration;
}

// ─────────────────────────────────────────────────────────────────────────────
// MarkForDestruction -- signal pool to reclaim this slot
// ─────────────────────────────────────────────────────────────────────────────

void VfxDecal::MarkForDestruction() noexcept {
    isActive = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// IsExpired -- lifetime check
// ─────────────────────────────────────────────────────────────────────────────

bool VfxDecal::IsExpired() const noexcept {
    return age >= lifetime;
}

} // namespace uz
