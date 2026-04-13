/// VFX Neighbor Interaction Module implementation -- Boids-style forces via spatial hash.

#include "neighbor_interaction_module.h"

#include "engine/logging/log.h"

#include <algorithm>
#include <json.hpp>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// IVFXModule interface
// ─────────────────────────────────────────────────────────────────────────────

const char* NeighborInteractionModule::GetName() const noexcept {
    return "neighbor_interaction";
}

/// Returns empty -- neighbor interaction uses dedicated grid scatter + force
/// compute passes rather than an inline snippet in the particle update shader.
std::string_view NeighborInteractionModule::GetShaderCode() const {
    return {};
}

std::vector<ParameterDef> NeighborInteractionModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(7);

    {
        ParameterDef p;
        p.name         = "separation_radius";
        p.type         = ParameterType::Float;
        p.defaultFloat = separationRadius;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "separation_strength";
        p.type         = ParameterType::Float;
        p.defaultFloat = separationStrength;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "alignment_radius";
        p.type         = ParameterType::Float;
        p.defaultFloat = alignmentRadius;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "alignment_strength";
        p.type         = ParameterType::Float;
        p.defaultFloat = alignmentStrength;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "cohesion_radius";
        p.type         = ParameterType::Float;
        p.defaultFloat = cohesionRadius;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "cohesion_strength";
        p.type         = ParameterType::Float;
        p.defaultFloat = cohesionStrength;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name       = "force_mode_mask";
        p.type       = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(forceModeMask);
        params.push_back(p);
    }

    return params;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON configuration
// ─────────────────────────────────────────────────────────────────────────────

void NeighborInteractionModule::ApplyJsonConfig(const nlohmann::json& config) {
    if (!config.is_object()) {
        UZ_LOG(VFX, Warning,
               "NeighborInteractionModule::ApplyJsonConfig: expected object, got {}; "
               "using defaults",
               config.type_name());
        return;
    }

    // Boids force parameters
    if (config.contains("separation_radius") && config["separation_radius"].is_number()) {
        separationRadius = std::max(0.0f, config["separation_radius"].get<float>());
    }
    if (config.contains("separation_strength") && config["separation_strength"].is_number()) {
        separationStrength = config["separation_strength"].get<float>();
    }
    if (config.contains("alignment_radius") && config["alignment_radius"].is_number()) {
        alignmentRadius = std::max(0.0f, config["alignment_radius"].get<float>());
    }
    if (config.contains("alignment_strength") && config["alignment_strength"].is_number()) {
        alignmentStrength = config["alignment_strength"].get<float>();
    }
    if (config.contains("cohesion_radius") && config["cohesion_radius"].is_number()) {
        cohesionRadius = std::max(0.0f, config["cohesion_radius"].get<float>());
    }
    if (config.contains("cohesion_strength") && config["cohesion_strength"].is_number()) {
        cohesionStrength = config["cohesion_strength"].get<float>();
    }
    if (config.contains("force_mode_mask") && config["force_mode_mask"].is_number_integer()) {
        forceModeMask = static_cast<uint32_t>(
            std::clamp(config["force_mode_mask"].get<int>(), 0, 7));
    }

    // Grid configuration
    if (config.contains("grid") && config["grid"].is_object()) {
        const auto& g = config["grid"];
        if (g.contains("min_x") && g["min_x"].is_number())
            gridConfig.gridMinX = g["min_x"].get<float>();
        if (g.contains("min_y") && g["min_y"].is_number())
            gridConfig.gridMinY = g["min_y"].get<float>();
        if (g.contains("min_z") && g["min_z"].is_number())
            gridConfig.gridMinZ = g["min_z"].get<float>();
        if (g.contains("extent_x") && g["extent_x"].is_number())
            gridConfig.gridExtentX = std::max(1.0f, g["extent_x"].get<float>());
        if (g.contains("extent_y") && g["extent_y"].is_number())
            gridConfig.gridExtentY = std::max(1.0f, g["extent_y"].get<float>());
        if (g.contains("extent_z") && g["extent_z"].is_number())
            gridConfig.gridExtentZ = std::max(1.0f, g["extent_z"].get<float>());
        if (g.contains("cells_x") && g["cells_x"].is_number_integer())
            gridConfig.cellsX = static_cast<uint32_t>(std::clamp(g["cells_x"].get<int>(), 1, 256));
        if (g.contains("cells_y") && g["cells_y"].is_number_integer())
            gridConfig.cellsY = static_cast<uint32_t>(std::clamp(g["cells_y"].get<int>(), 1, 256));
        if (g.contains("cells_z") && g["cells_z"].is_number_integer())
            gridConfig.cellsZ = static_cast<uint32_t>(std::clamp(g["cells_z"].get<int>(), 1, 256));
        if (g.contains("max_per_cell") && g["max_per_cell"].is_number_integer())
            gridConfig.maxPerCell = static_cast<uint32_t>(std::clamp(g["max_per_cell"].get<int>(), 1, 64));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute -- no-op (GPU-driven)
// ─────────────────────────────────────────────────────────────────────────────

void NeighborInteractionModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- grid scatter and neighbor force passes are dispatched
    // by the render graph, not by the standard module execution path.
}

} // namespace uz
