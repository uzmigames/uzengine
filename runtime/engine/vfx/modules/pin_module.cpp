/// VFX Pin Module implementation -- Hooke's-law spring attraction to a world-space anchor.
///
/// @ref Hooke 1678 "De Potentia Restitutiva" -- spring force law F = k * x,
///      where k is the spring constant and x is the displacement from rest position.

#include "pin_module.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Shader code -- HLSL snippet for Hooke's-law spring force
// ---------------------------------------------------------------------------

/// HLSL spring force snippet.
///
/// Computes a restoring force directed from the particle toward cbPinPosition.
/// Force magnitude is proportional to distance (Hooke's law: F = k * x).
/// Particles within 0.001 world units of the pin are skipped to avoid
/// dividing by near-zero length.
///
/// @ref Hooke 1678 "De Potentia Restitutiva" -- F = k * x
/// @constants cbPinSpringStrength = 5.0 (spring constant k, unitless coefficient)
///            cbDt = frame delta time in seconds (injected by compositor)
static constexpr const char* kPinShaderCode = R"hlsl(
// --- PinModule (Hooke's law: F = k * x) ---
{
    float3 toPin = cbPinPosition - Positions[particleIdx];
    float dist = length(toPin);
    if (dist > 0.001)
    {
        float3 force = normalize(toPin) * cbPinSpringStrength * dist;
        Velocities[particleIdx] += force * cbDt;
    }
}
)hlsl";

// ---------------------------------------------------------------------------
// IVFXModule interface
// ---------------------------------------------------------------------------

/// Returns the module's unique name for registration and serialization.
const char* PinModule::GetName() const noexcept {
    return "pin";
}

/// Returns the HLSL snippet for spring-force particle attraction.
///
/// Expects cbPinPosition (float3), cbPinSpringStrength (float), and
/// cbDt (float) to be present in the module's constant buffer.
std::string_view PinModule::GetShaderCode() const {
    return kPinShaderCode;
}

/// Declares pin parameters for editor UI and constant buffer layout.
///
/// Parameters are ordered as they appear in the constant buffer:
///   1. pin_position    (Vec3)  -- world-space anchor point
///   2. spring_strength (Float) -- Hooke's law spring constant k
std::vector<ParameterDef> PinModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(2);

    {
        ParameterDef p;
        p.name       = "pin_position";
        p.type       = ParameterType::Vec3;
        p.defaultVec3 = pinPosition;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "spring_strength";
        p.type         = ParameterType::Float;
        p.defaultFloat = springStrength;
        params.push_back(p);
    }

    return params;
}

/// No-op on CPU side. The pin compute shader evaluates spring force
/// entirely on the GPU using constant buffer values uploaded by the
/// VFXSystem before dispatch.
void PinModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- nothing to do on the CPU side.
}

} // namespace uz
