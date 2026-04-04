/// VFX Update Module implementation -- gravity, drag, and rotation integration.

#include "update_module.h"

#include "engine/logging/log.h"
#include <json.hpp>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Shader code -- HLSL snippet for Euler integration
// ---------------------------------------------------------------------------

static constexpr const char* kUpdateShaderCode = R"hlsl(
// --- UpdateModule ---
float3 vel = Velocities[particleIdx];
vel += cbGravity * cbDt;
vel *= cbDrag;
Positions[particleIdx] += vel * cbDt;
Velocities[particleIdx] = vel;
Rotations[particleIdx] += cbAngularVelocity * cbDt;
)hlsl";

// ---------------------------------------------------------------------------
// IVFXModule interface
// ---------------------------------------------------------------------------

/// Returns the module's unique name for registration and serialization.
const char* UpdateModule::GetName() const noexcept {
    return "update";
}

/// Returns the HLSL snippet for velocity/position Euler integration.
/// Expects cbGravity (float3), cbDrag (float), cbDt (float), and
/// cbAngularVelocity (float) in the module constant buffer.
std::string_view UpdateModule::GetShaderCode() const {
    return kUpdateShaderCode;
}

/// Declares physics parameters for editor UI and constant buffer layout.
std::vector<ParameterDef> UpdateModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(3);

    {
        ParameterDef p;
        p.name = "gravity";
        p.type = ParameterType::Vec3;
        p.defaultVec3 = gravity;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "drag";
        p.type = ParameterType::Float;
        p.defaultFloat = drag;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "angular_velocity";
        p.type = ParameterType::Float;
        p.defaultFloat = angularVelocity;
        params.push_back(p);
    }

    return params;
}

/// Applies JSON config to gravity, drag, and angular velocity fields.
void UpdateModule::ApplyJsonConfig(const nlohmann::json& config) {
    if (config.contains("gravity") && config["gravity"].is_array()) {
        const auto& arr = config["gravity"];
        if (arr.size() >= 3) {
            gravity = Vec3{arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>()};
        }
    }
    if (config.contains("drag") && config["drag"].is_number()) {
        drag = config["drag"].get<float>();
    }
    if (config.contains("angular_velocity") && config["angular_velocity"].is_number()) {
        angularVelocity = config["angular_velocity"].get<float>();
    }
}

/// No-op on CPU side. The update compute shader handles integration
/// entirely on the GPU.
void UpdateModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- nothing to do on the CPU side.
}

} // namespace uz
