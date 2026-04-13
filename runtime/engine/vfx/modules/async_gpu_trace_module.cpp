/// VFX Async GPU Trace Module implementation -- depth-buffer raycasting collision.

#include "async_gpu_trace_module.h"

#include "engine/logging/log.h"

#include <algorithm>
#include <json.hpp>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// IVFXModule interface
// ─────────────────────────────────────────────────────────────────────────────

const char* AsyncGPUTraceModule::GetName() const noexcept {
    return "async_gpu_trace";
}

/// Returns empty -- depth trace and collision application are dedicated
/// render graph compute passes, not inline update shader snippets.
std::string_view AsyncGPUTraceModule::GetShaderCode() const {
    return {};
}

std::vector<ParameterDef> AsyncGPUTraceModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(4);

    {
        ParameterDef p;
        p.name         = "trace_radius";
        p.type         = ParameterType::Float;
        p.defaultFloat = traceRadius;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name       = "max_steps";
        p.type       = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(maxSteps);
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "restitution";
        p.type         = ParameterType::Float;
        p.defaultFloat = restitution;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name        = "write_death_decals";
        p.type        = ParameterType::Bool;
        p.defaultBool = writeDeathDecals;
        params.push_back(p);
    }

    return params;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON configuration
// ─────────────────────────────────────────────────────────────────────────────

void AsyncGPUTraceModule::ApplyJsonConfig(const nlohmann::json& config) {
    if (!config.is_object()) {
        UZ_LOG(VFX, Warning,
               "AsyncGPUTraceModule::ApplyJsonConfig: expected object, got {}; "
               "using defaults",
               config.type_name());
        return;
    }

    if (config.contains("trace_radius") && config["trace_radius"].is_number()) {
        traceRadius = std::max(0.1f, config["trace_radius"].get<float>());
    }
    if (config.contains("max_steps") && config["max_steps"].is_number_integer()) {
        maxSteps = static_cast<uint32_t>(
            std::clamp(config["max_steps"].get<int>(), 1, 64));
    }
    if (config.contains("restitution") && config["restitution"].is_number()) {
        restitution = std::clamp(config["restitution"].get<float>(), 0.0f, 1.0f);
    }
    if (config.contains("write_death_decals") && config["write_death_decals"].is_boolean()) {
        writeDeathDecals = config["write_death_decals"].get<bool>();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute -- no-op (render graph driven)
// ─────────────────────────────────────────────────────────────────────────────

void AsyncGPUTraceModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- depth trace and collision application passes are
    // dispatched by the render graph, not by the standard module execution path.
}

} // namespace uz
