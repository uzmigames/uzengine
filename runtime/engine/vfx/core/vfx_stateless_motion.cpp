#include "vfx_stateless_motion.h"

#include <cmath>

namespace uz {

namespace {

/// Epsilon below which damping is treated as zero to avoid divide-by-tiny
/// amplification. 1e-4 catches authoring mistakes ("almost zero") while
/// being well below any physically meaningful damping value.
constexpr float kDampingEpsilon = 1.0e-4f;

} // namespace

Vec3 VfxStatelessEvaluatePosition(const VfxStatelessParticle& p, float t) noexcept {
    // Clamp negative t — particle hasn't been born yet.
    const float tt = (t > 0.0f) ? t : 0.0f;

    // Gravity contribution: standard 0.5 * g * t^2.
    const Vec3 gravityTerm {
        0.5f * p.gravity.x * tt * tt,
        0.5f * p.gravity.y * tt * tt,
        0.5f * p.gravity.z * tt * tt,
    };

    // Velocity contribution — branch on damping magnitude.
    Vec3 velocityTerm;
    if (std::fabs(p.damping) < kDampingEpsilon) {
        // No drag: linear ballistic integration.
        velocityTerm = Vec3 {
            p.spawnVelocity.x * tt,
            p.spawnVelocity.y * tt,
            p.spawnVelocity.z * tt,
        };
    } else {
        // Exponential drag: v0 * (1 - exp(-k*t)) / k
        const float k         = p.damping;
        const float expFactor = std::exp(-k * tt);
        const float scale     = (1.0f - expFactor) / k;
        velocityTerm = Vec3 {
            p.spawnVelocity.x * scale,
            p.spawnVelocity.y * scale,
            p.spawnVelocity.z * scale,
        };
    }

    return Vec3 {
        p.spawnPosition.x + velocityTerm.x + gravityTerm.x,
        p.spawnPosition.y + velocityTerm.y + gravityTerm.y,
        p.spawnPosition.z + velocityTerm.z + gravityTerm.z,
    };
}

Vec3 VfxStatelessEvaluateVelocity(const VfxStatelessParticle& p, float t) noexcept {
    const float tt = (t > 0.0f) ? t : 0.0f;

    // Gravity contribution: g * t.
    const Vec3 gravityTerm {
        p.gravity.x * tt,
        p.gravity.y * tt,
        p.gravity.z * tt,
    };

    // Velocity contribution.
    Vec3 velFromInit;
    if (std::fabs(p.damping) < kDampingEpsilon) {
        velFromInit = p.spawnVelocity;  // no decay
    } else {
        const float expFactor = std::exp(-p.damping * tt);
        velFromInit = Vec3 {
            p.spawnVelocity.x * expFactor,
            p.spawnVelocity.y * expFactor,
            p.spawnVelocity.z * expFactor,
        };
    }

    return Vec3 {
        velFromInit.x + gravityTerm.x,
        velFromInit.y + gravityTerm.y,
        velFromInit.z + gravityTerm.z,
    };
}

float VfxStatelessNormalizedAge(const VfxStatelessParticle& p, float t) noexcept {
    // Defensive: zero lifetime would divide by zero; treat as "always dead"
    // (normalised age 1) so fade shaders immediately discard.
    if (!(p.lifetime > 0.0f)) {
        return 1.0f;
    }
    const float tt = (t > 0.0f) ? t : 0.0f;
    return tt / p.lifetime;
}

} // namespace uz
