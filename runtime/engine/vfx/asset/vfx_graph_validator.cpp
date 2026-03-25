/// VFX Graph Validator implementation -- structural checks for loaded graphs.

#include "vfx_graph_validator.h"

#include "vfx_graph.h"

#include "engine/logging/log.h"

#include <unordered_set>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {
namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Individual validation checks
// ─────────────────────────────────────────────────────────────────────────────

/// Checks that the graph contains at least one module.
void CheckModulesNotEmpty(const VFXGraph& graph, VFXValidationResult& result) {
    if (graph.GetModules().empty()) {
        const auto msg = "VFXGraph validation: graph has no modules";
        UZ_LOG(VFX, Error, "{}", msg);
        result.errors.push_back(msg);
        result.valid = false;
    }
}

/// Checks that emitter max_particles is greater than zero.
void CheckEmitterMaxParticles(const VFXGraph& graph, VFXValidationResult& result) {
    if (graph.GetEmitterConfig().maxParticles == 0) {
        const auto msg = "VFXGraph validation: emitter max_particles must be > 0";
        UZ_LOG(VFX, Error, "{}", msg);
        result.errors.push_back(msg);
        result.valid = false;
    }
}

/// Checks that emitter lifespan_default is greater than zero.
void CheckEmitterLifespan(const VFXGraph& graph, VFXValidationResult& result) {
    if (graph.GetEmitterConfig().lifespanDefault <= 0.0f) {
        const auto msg = "VFXGraph validation: emitter lifespan_default must be > 0";
        UZ_LOG(VFX, Error, "{}", msg);
        result.errors.push_back(msg);
        result.valid = false;
    }
}

/// Checks for duplicate parameter names.
void CheckDuplicateParameterNames(const VFXGraph& graph, VFXValidationResult& result) {
    std::unordered_set<std::string> seen;

    for (const auto& param : graph.GetParameters()) {
        if (!seen.insert(param.name).second) {
            const auto msg = "VFXGraph validation: duplicate parameter name '" + param.name + "'";
            UZ_LOG(VFX, Error, "{}", msg);
            result.errors.push_back(msg);
            result.valid = false;
        }
    }
}

/// Checks that parameter types are valid for graph-level definitions.
///
/// Only Float, Vec3, and Vec4 are valid as exposed graph parameters.
/// Int, Bool, and Curve are module-internal types not exposed to the graph.
void CheckParameterTypes(const VFXGraph& graph, VFXValidationResult& result) {
    for (const auto& param : graph.GetParameters()) {
        const bool validType = (param.type == ParameterType::Float)
                            || (param.type == ParameterType::Vec3)
                            || (param.type == ParameterType::Vec4);

        if (!validType) {
            const auto msg = "VFXGraph validation: parameter '" + param.name
                           + "' has invalid type for graph parameter";
            UZ_LOG(VFX, Error, "{}", msg);
            result.errors.push_back(msg);
            result.valid = false;
        }
    }
}

/// Checks for duplicate event names.
void CheckDuplicateEventNames(const VFXGraph& graph, VFXValidationResult& result) {
    std::unordered_set<std::string> seen;

    for (const auto& event : graph.GetEvents()) {
        if (!seen.insert(event.name).second) {
            const auto msg = "VFXGraph validation: duplicate event name '" + event.name + "'";
            UZ_LOG(VFX, Error, "{}", msg);
            result.errors.push_back(msg);
            result.valid = false;
        }
    }
}

/// Checks that all events have positive emission counts.
void CheckEventEmissionCounts(const VFXGraph& graph, VFXValidationResult& result) {
    for (const auto& event : graph.GetEvents()) {
        if (event.emissionCount == 0) {
            const auto msg = "VFXGraph validation: event '" + event.name
                           + "' has emission_count == 0";
            UZ_LOG(VFX, Error, "{}", msg);
            result.errors.push_back(msg);
            result.valid = false;
        }
    }
}

/// Checks that all events have positive velocity scales.
void CheckEventVelocityScales(const VFXGraph& graph, VFXValidationResult& result) {
    for (const auto& event : graph.GetEvents()) {
        if (event.velocityScale <= 0.0f) {
            const auto msg = "VFXGraph validation: event '" + event.name
                           + "' has velocity_scale <= 0";
            UZ_LOG(VFX, Error, "{}", msg);
            result.errors.push_back(msg);
            result.valid = false;
        }
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ValidateVFXGraph -- public entry point
// ─────────────────────────────────────────────────────────────────────────────

VFXValidationResult ValidateVFXGraph(const VFXGraph& graph) {
    VFXValidationResult result;

    CheckModulesNotEmpty(graph, result);
    CheckEmitterMaxParticles(graph, result);
    CheckEmitterLifespan(graph, result);
    CheckDuplicateParameterNames(graph, result);
    CheckParameterTypes(graph, result);
    CheckDuplicateEventNames(graph, result);
    CheckEventEmissionCounts(graph, result);
    CheckEventVelocityScales(graph, result);

    return result;
}

} // namespace uz
