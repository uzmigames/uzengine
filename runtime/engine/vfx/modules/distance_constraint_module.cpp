/// VFX Distance Constraint Module implementation -- spherical-shell radial enforcement.

#include "distance_constraint_module.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Shader code -- HLSL snippet for spherical distance constraint
// ---------------------------------------------------------------------------

// The snippet integrates directly into the module compositor's per-particle
// loop.  `particleIdx`, `Positions`, `Velocities`, and the per-frame scalar
// `cbDt` are provided by the compositor harness.
//
// Guard against degenerate case: if the particle sits exactly at the origin
// (dist <= 0.001) we skip it to avoid normalizing a near-zero vector.
// The threshold of 0.001 world units matches the spec and is safe for any
// scene using centimeter units (a particle 0.001 cm from the exact origin
// centre is considered "at the origin" and receives no correction impulse).
static constexpr const char* kDistanceConstraintShaderCode = R"hlsl(
// --- DistanceConstraintModule ---
{
    float3 toParticle = Positions[particleIdx] - cbConstraintOrigin;
    float dist = length(toParticle);
    if (dist > 0.001)
    {
        float3 dir = toParticle / dist;
        if (dist < cbMinDistance)
        {
            // Particle is inside the inner boundary -- push outward.
            float pushForce = (cbMinDistance - dist) * cbConstraintStrength;
            Velocities[particleIdx] += dir * pushForce * cbDt;
        }
        else if (dist > cbMaxDistance)
        {
            // Particle is outside the outer boundary -- pull inward.
            float pullForce = (dist - cbMaxDistance) * cbConstraintStrength;
            Velocities[particleIdx] -= dir * pullForce * cbDt;
        }
    }
}
)hlsl";

// ---------------------------------------------------------------------------
// IVFXModule interface
// ---------------------------------------------------------------------------

/// Returns the module's unique name for registration and serialization.
const char* DistanceConstraintModule::GetName() const noexcept {
    return "distance_constraint";
}

/// Returns the HLSL snippet that evaluates per-particle radial distance and
/// applies a corrective velocity impulse when the particle leaves the valid
/// spherical shell.
std::string_view DistanceConstraintModule::GetShaderCode() const {
    return kDistanceConstraintShaderCode;
}

/// Declares all four configurable parameters for the editor UI and the
/// constant buffer layout consumed by the compositor.
std::vector<ParameterDef> DistanceConstraintModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(4);

    {
        ParameterDef p;
        p.name        = "constraint_origin";
        p.type        = ParameterType::Vec3;
        p.defaultVec3 = constraintOrigin;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "min_distance";
        p.type         = ParameterType::Float;
        p.defaultFloat = minDistance;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "max_distance";
        p.type         = ParameterType::Float;
        p.defaultFloat = maxDistance;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "constraint_strength";
        p.type         = ParameterType::Float;
        p.defaultFloat = constraintStrength;
        params.push_back(p);
    }

    return params;
}

/// No-op on the CPU side.  The distance constraint compute shader evaluates
/// the spherical shell condition and applies corrective impulses entirely on
/// the GPU; no CPU-side dispatch preparation is required.
void DistanceConstraintModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- nothing to do on the CPU side.
}

} // namespace uz
