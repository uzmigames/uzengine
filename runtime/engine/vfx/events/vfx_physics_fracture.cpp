#include "vfx_physics_fracture.h"

#include <algorithm>
#include <cmath>

namespace uz {

namespace {

/// Returns the impact velocity vector = impactNormal · impactSpeed.
/// Unit-length normal assumed; the translator clamps when it fires.
Vec3 ImpactVelocity(const VfxFractureEventInput& in) noexcept {
    const float speed = std::max(0.0f, in.impactSpeed);
    // Normalise to guard against non-unit normals reported by Jolt.
    const float n = std::sqrt(in.impactNormal.x * in.impactNormal.x +
                              in.impactNormal.y * in.impactNormal.y +
                              in.impactNormal.z * in.impactNormal.z);
    if (n < 1e-6f) return Vec3{0, 0, 0};
    const float inv = speed / n;
    return Vec3{in.impactNormal.x * inv,
                in.impactNormal.y * inv,
                in.impactNormal.z * inv};
}

float TotalMass(const VfxFractureEventInput& in) noexcept {
    float sum = 0.0f;
    for (const auto& p : in.pieces) {
        sum += std::max(0.0f, p.mass);
    }
    return sum;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Translation
// ─────────────────────────────────────────────────────────────────────────────

VFXPendingEvent VfxTranslateFractureEvent(const VfxFractureEventInput& input,
                                             const std::string& eventName) noexcept {
    VFXPendingEvent out;
    out.name = eventName;
    out.emissionCount = 0u;
    out.velocityScale = 1.0f;

    if (input.pieces.empty()) {
        return out; // no pieces → empty payload
    }

    const Vec3  impact   = ImpactVelocity(input);
    const float totalM   = TotalMass(input);
    const float safeTotal = (totalM > 1e-6f) ? totalM : 1.0f;

    out.payloads.reserve(input.pieces.size());
    for (const auto& piece : input.pieces) {
        EventPayload p;
        p.position = piece.position;
        // Combine the impact velocity with the piece's own linear velocity
        // so big pieces keep their inertia while taking the impact kick.
        p.velocity = Vec3{
            impact.x + piece.linearVelocity.x,
            impact.y + piece.linearVelocity.y,
            impact.z + piece.linearVelocity.z,
        };
        p.color = Vec4{
            input.authoredTint.x,
            input.authoredTint.y,
            input.authoredTint.z,
            std::clamp(std::max(0.0f, piece.mass) / safeTotal, 0.0f, 1.0f),
        };
        p.age = 0.0f;
        out.payloads.push_back(p);
    }

    out.emissionCount = static_cast<uint32_t>(out.payloads.size());
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bridge
// ─────────────────────────────────────────────────────────────────────────────

bool VfxPhysicsFractureBridge::OnFracture(const VfxFractureEventInput& input) {
    if (queue_ == nullptr) return false;
    if (input.pieces.empty()) return false;

    VFXPendingEvent evt = VfxTranslateFractureEvent(input, eventName_);
    if (evt.payloads.empty()) return false;

    queue_->EmitWithPayloads(evt.name, std::move(evt.payloads), 1.0f);
    ++forwarded_;
    return true;
}

} // namespace uz
