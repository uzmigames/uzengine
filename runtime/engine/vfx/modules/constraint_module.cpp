/// VFX Constraint Module implementation -- AABB position clamping.

#include "constraint_module.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Shader code -- HLSL snippet for AABB position clamping
// ---------------------------------------------------------------------------

static constexpr const char* kConstraintShaderCode = R"hlsl(
// --- ConstraintModule ---
Positions[particleIdx] = clamp(Positions[particleIdx], cbBoundsMin, cbBoundsMax);
)hlsl";

// ---------------------------------------------------------------------------
// IVFXModule interface
// ---------------------------------------------------------------------------

/// Returns the module's unique name for registration and serialization.
const char* ConstraintModule::GetName() const noexcept {
    return "constraint";
}

/// Returns the HLSL snippet for clamping particle positions to the AABB.
/// Expects cbBoundsMin (float3) and cbBoundsMax (float3) in the module
/// constant buffer.
std::string_view ConstraintModule::GetShaderCode() const {
    return kConstraintShaderCode;
}

/// Declares bounding box parameters for editor UI and constant buffer layout.
std::vector<ParameterDef> ConstraintModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(2);

    {
        ParameterDef p;
        p.name = "bounds_min";
        p.type = ParameterType::Vec3;
        p.defaultVec3 = boundsMin;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "bounds_max";
        p.type = ParameterType::Vec3;
        p.defaultVec3 = boundsMax;
        params.push_back(p);
    }

    return params;
}

/// No-op on CPU side. The constraint compute shader handles AABB clamping
/// entirely on the GPU.
void ConstraintModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- nothing to do on the CPU side.
}

} // namespace uz
