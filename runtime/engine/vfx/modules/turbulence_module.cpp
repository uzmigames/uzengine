/// VFX Turbulence Module implementation -- FBM noise velocity perturbation.

#include "turbulence_module.h"

#include "engine/logging/log.h"

#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Shader code -- HLSL snippet for FBM turbulence
// ---------------------------------------------------------------------------

static constexpr const char* kTurbulenceShaderCode = R"hlsl(
// --- TurbulenceModule ---
float3 noisePos = Positions[particleIdx] * cbTurbFrequency;
float3 turbForce = FBM3D(noisePos, cbTime, cbTurbOctaves) * cbTurbStrength;
Velocities[particleIdx] += turbForce * cbDt;
)hlsl";

// ---------------------------------------------------------------------------
// IVFXModule interface
// ---------------------------------------------------------------------------

/// Returns the module's unique name for registration and serialization.
const char* TurbulenceModule::GetName() const noexcept {
    return "turbulence";
}

/// Returns the HLSL snippet for FBM noise evaluation and velocity perturbation.
/// Expects FBM3D() helper function, cbTurbFrequency (float),
/// cbTurbStrength (float), cbTurbOctaves (int), cbTime (float), and cbDt (float).
std::string_view TurbulenceModule::GetShaderCode() const {
    return kTurbulenceShaderCode;
}

/// Declares turbulence parameters for editor UI and constant buffer layout.
/// Octaves are clamped to [kMinOctaves, kMaxOctaves] in the default value.
std::vector<ParameterDef> TurbulenceModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(3);

    {
        ParameterDef p;
        p.name = "strength";
        p.type = ParameterType::Float;
        p.defaultFloat = strength;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "frequency";
        p.type = ParameterType::Float;
        p.defaultFloat = frequency;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "octaves";
        p.type = ParameterType::Int;
        p.defaultInt = std::clamp(octaves, kMinOctaves, kMaxOctaves);
        params.push_back(p);
    }

    return params;
}

/// No-op on CPU side. The turbulence compute shader evaluates FBM noise
/// and applies velocity perturbation entirely on the GPU.
void TurbulenceModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- nothing to do on the CPU side.
}

} // namespace uz
