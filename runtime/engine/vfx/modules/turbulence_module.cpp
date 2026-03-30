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
// --- TurbulenceModule (FBM mode) ---
float3 noisePos = Positions[particleIdx] * cbTurbFrequency;
float3 turbForce = FBM3D(noisePos, cbTime, cbTurbOctaves) * cbTurbStrength;
Velocities[particleIdx] += turbForce * cbDt;
)hlsl";

/// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow"
/// Divergence-free velocity perturbation using AnimatedCurlNoise.
/// NOTE: vfx_curl_noise.hlsli must be included by the compositor.
static constexpr const char* kCurlNoiseShaderCode = R"hlsl(
// --- TurbulenceModule (Curl mode) ---
// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow"
float3 curlForce = AnimatedCurlNoise(
    Positions[particleIdx].xyz, cbTime, cbCurlSpeed, cbCurlFrequency
) * cbCurlStrength;
Velocities[particleIdx] += curlForce * cbDt;
)hlsl";

// ---------------------------------------------------------------------------
// IVFXModule interface
// ---------------------------------------------------------------------------

/// Returns the module's unique name for registration and serialization.
const char* TurbulenceModule::GetName() const noexcept {
    return "turbulence";
}

/// Returns the HLSL snippet for noise-based velocity perturbation.
///
/// When useCurlMode is false (default), returns the FBM noise snippet that
/// expects FBM3D(), cbTurbFrequency, cbTurbStrength, cbTurbOctaves, cbTime, cbDt.
///
/// When useCurlMode is true, returns the curl noise snippet that expects
/// AnimatedCurlNoise() (from vfx_curl_noise.hlsli), cbCurlFrequency,
/// cbCurlStrength, cbCurlSpeed, cbTime, cbDt.
std::string_view TurbulenceModule::GetShaderCode() const {
    return useCurlMode ? kCurlNoiseShaderCode : kTurbulenceShaderCode;
}

/// Declares turbulence parameters for editor UI and constant buffer layout.
/// Octaves are clamped to [kMinOctaves, kMaxOctaves] in the default value.
std::vector<ParameterDef> TurbulenceModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(4);

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
    {
        ParameterDef p;
        p.name = "use_curl_mode";
        p.type = ParameterType::Bool;
        p.defaultBool = useCurlMode;
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
