/// VFX Dynamic Parameters Module implementation -- per-particle Vec4 payload.
///
/// @ref GDC 2014 "Destiny's Multithreaded Rendering Architecture" -- per-particle
///      payload concept for material-driven dynamic parameters.

#include "dynamic_params_module.h"

#include "engine/logging/log.h"

#include <algorithm>
#include <json.hpp>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

/// Default-constructs with `DynamicParamsModuleConfig` defaults -- required for
/// `ModuleRegistry::Register<DynamicParamsModule>()`.
DynamicParamsModule::DynamicParamsModule()
    : DynamicParamsModule(DynamicParamsModuleConfig{})
{}

/// Stores the configuration in the public `config` member.
DynamicParamsModule::DynamicParamsModule(const DynamicParamsModuleConfig& cfg)
    : config(cfg)
{}

// ─────────────────────────────────────────────────────────────────────────────
// IVFXModule interface
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the unique registration name. Stable -- changing this breaks saved assets.
const char* DynamicParamsModule::GetName() const noexcept {
    return "dynamic_params";
}

/// Returns an empty string view. This module has no inline HLSL snippet --
/// the GPU computation is handled by the particle update compute shader via
/// constant buffer fields populated from this module's config.
std::string_view DynamicParamsModule::GetShaderCode() const {
    return {};
}

/// Declares the three configurable dynamic params parameters for editor UI
/// and constant buffer serialization.
std::vector<ParameterDef> DynamicParamsModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(3);

    {
        ParameterDef p;
        p.name       = "default_value";
        p.type       = ParameterType::Vec4;
        p.defaultVec4 = config.defaultValue;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name       = "evolve_mode";
        p.type       = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(config.evolveMode);
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "evolve_rate";
        p.type         = ParameterType::Float;
        p.defaultFloat = config.evolveRate;
        params.push_back(p);
    }

    return params;
}

/// Applies configuration from a JSON object to the public `config` member.
void DynamicParamsModule::ApplyJsonConfig(const nlohmann::json& j) {
    config = ParseConfig(j);
}

/// No CPU-side work required. The constant buffer values are uploaded by the
/// ModuleCompositor from the public `config` member each frame.
void DynamicParamsModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- nothing to do on the CPU side.
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration access
// ─────────────────────────────────────────────────────────────────────────────

const DynamicParamsModuleConfig& DynamicParamsModule::GetConfig() const noexcept {
    return config;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON deserialization
// ─────────────────────────────────────────────────────────────────────────────

/// Parses a DynamicParamsModuleConfig from a JSON object.
/// All fields are optional -- absent keys retain their default values.
DynamicParamsModuleConfig DynamicParamsModule::ParseConfig(const nlohmann::json& j) {
    DynamicParamsModuleConfig cfg;

    if (!j.is_object()) {
        UZ_LOG(VFX, Warning,
               "DynamicParamsModule::ParseConfig: expected a JSON object, got {}; "
               "using defaults",
               j.type_name());
        return cfg;
    }

    // "default_value": array of 4 floats.
    if (j.contains("default_value") && j["default_value"].is_array()) {
        const auto& arr = j["default_value"];
        if (arr.size() >= 4) {
            for (size_t i = 0; i < 4; ++i) {
                if (arr[i].is_number()) {
                    cfg.defaultValue[static_cast<int>(i)] = arr[i].get<float>();
                }
            }
        } else {
            UZ_LOG(VFX, Warning,
                   "DynamicParamsModule::ParseConfig: 'default_value' array has {} "
                   "elements, expected 4; using defaults for missing components",
                   arr.size());
            // Parse as many components as available.
            for (size_t i = 0; i < arr.size(); ++i) {
                if (arr[i].is_number()) {
                    cfg.defaultValue[static_cast<int>(i)] = arr[i].get<float>();
                }
            }
        }
    }

    // "evolve_mode": integer 0-2.
    if (j.contains("evolve_mode") && j["evolve_mode"].is_number_integer()) {
        const int raw = j["evolve_mode"].get<int>();
        const int clamped = std::clamp(raw, 0, 2);
        if (raw != clamped) {
            UZ_LOG(VFX, Warning,
                   "DynamicParamsModule::ParseConfig: evolve_mode {} clamped to [0,2]",
                   raw);
        }
        cfg.evolveMode = static_cast<uint8_t>(clamped);
    }

    // "evolve_rate": float >= 0.
    if (j.contains("evolve_rate") && j["evolve_rate"].is_number()) {
        const float raw = j["evolve_rate"].get<float>();
        if (raw < 0.0f) {
            UZ_LOG(VFX, Warning,
                   "DynamicParamsModule::ParseConfig: evolve_rate {} is negative; "
                   "clamping to 0.0",
                   raw);
        }
        cfg.evolveRate = std::max(0.0f, raw);
    }

    return cfg;
}

} // namespace uz
