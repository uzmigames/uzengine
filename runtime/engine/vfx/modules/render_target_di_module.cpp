/// VFX Render Target DI Module implementation -- persistent GPU feedback loop.

#include "render_target_di_module.h"

#include "engine/logging/log.h"

#include <algorithm>
#include <json.hpp>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// IVFXModule interface
// ─────────────────────────────────────────────────────────────────────────────

const char* RenderTargetDIModule::GetName() const noexcept {
    return "render_target_di";
}

/// Returns empty -- feedback loop uses dedicated decay/paint compute passes
/// and a billboard PS read, not an inline snippet in the update shader.
std::string_view RenderTargetDIModule::GetShaderCode() const {
    return {};
}

std::vector<ParameterDef> RenderTargetDIModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(7);

    {
        ParameterDef p;
        p.name       = "tex_width";
        p.type       = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(texWidth);
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name       = "tex_height";
        p.type       = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(texHeight);
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "decay_rate";
        p.type         = ParameterType::Float;
        p.defaultFloat = decayRate;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "paint_opacity";
        p.type         = ParameterType::Float;
        p.defaultFloat = paintOpacity;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "feedback_strength";
        p.type         = ParameterType::Float;
        p.defaultFloat = feedbackStrength;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name        = "screen_space";
        p.type        = ParameterType::Bool;
        p.defaultBool = screenSpace;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name        = "clear_on_reset";
        p.type        = ParameterType::Bool;
        p.defaultBool = clearOnReset;
        params.push_back(p);
    }

    return params;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON configuration
// ─────────────────────────────────────────────────────────────────────────────

void RenderTargetDIModule::ApplyJsonConfig(const nlohmann::json& config) {
    if (!config.is_object()) {
        UZ_LOG(VFX, Warning,
               "RenderTargetDIModule::ApplyJsonConfig: expected object, got {}; "
               "using defaults",
               config.type_name());
        return;
    }

    if (config.contains("tex_width") && config["tex_width"].is_number_integer()) {
        texWidth = static_cast<uint32_t>(
            std::clamp(config["tex_width"].get<int>(), 1, 16384));
    }
    if (config.contains("tex_height") && config["tex_height"].is_number_integer()) {
        texHeight = static_cast<uint32_t>(
            std::clamp(config["tex_height"].get<int>(), 1, 16384));
    }
    if (config.contains("decay_rate") && config["decay_rate"].is_number()) {
        decayRate = std::clamp(config["decay_rate"].get<float>(), 0.0f, 1.0f);
    }
    if (config.contains("paint_opacity") && config["paint_opacity"].is_number()) {
        paintOpacity = std::clamp(config["paint_opacity"].get<float>(), 0.0f, 1.0f);
    }
    if (config.contains("feedback_strength") && config["feedback_strength"].is_number()) {
        feedbackStrength = std::clamp(config["feedback_strength"].get<float>(), 0.0f, 1.0f);
    }
    if (config.contains("screen_space") && config["screen_space"].is_boolean()) {
        screenSpace = config["screen_space"].get<bool>();
    }
    if (config.contains("clear_on_reset") && config["clear_on_reset"].is_boolean()) {
        clearOnReset = config["clear_on_reset"].get<bool>();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute -- no-op (render graph driven)
// ─────────────────────────────────────────────────────────────────────────────

void RenderTargetDIModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- decay and paint passes are dispatched by the render
    // graph. Billboard PS feedback read is handled by the compositor.
}

} // namespace uz
