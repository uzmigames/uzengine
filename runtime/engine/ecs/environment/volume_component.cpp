/// volume_component.cpp — Serialization for VolumeComponent.
///
/// Registers a type-erased serializer entry so that the scene loader can
/// dynamically create and serialize VolumeComponent by the JSON key
/// "volume" without hardcoded dispatch.

#include "volume_component.h"
#include "component_serializer.h"
#include "world.h"
#include "../core/json_helpers.h"

#include <json.hpp>
#include <entt/entt.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

UZ_REGISTER_COMPONENT_SERIALIZER(volume, uz::VolumeComponent,
    // Deserialize
    [](uz::World& world, uz::Entity entity, const void* jsonPtr) {
        const auto& j = *static_cast<const nlohmann::json*>(jsonPtr);
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        auto& c = world.GetRegistry().emplace<uz::VolumeComponent>(entt);

        const uz::VolumeComponent defaults{};

        // Grid and bounds
        c.densityGrid  = uz::json::String(j, "density_grid", defaults.densityGrid);
        c.halfExtents  = uz::json::Vec3FromArray(j, "half_extents", defaults.halfExtents);
        c.densityScale = uz::json::Float(j, "density_scale", defaults.densityScale);

        // Medium
        c.sigmaA   = uz::json::Float(j, "sigma_a", defaults.sigmaA);
        c.sigmaS   = uz::json::Float(j, "sigma_s", defaults.sigmaS);
        c.phaseG   = uz::json::Float(j, "phase_g", defaults.phaseG);
        c.emission = uz::json::Vec3FromArray(j, "emission", defaults.emission);

        // Control
        c.enabled = uz::json::Bool(j, "enabled", defaults.enabled);
    },
    // Serialize
    [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        const auto* c = world.GetRegistry().try_get<uz::VolumeComponent>(entt);
        if (!c) return false;
        auto& j = *static_cast<nlohmann::json*>(jsonPtr);

        // Grid and bounds
        j["density_grid"] = c->densityGrid;
        uz::json::WriteVec3(j, "half_extents", c->halfExtents);
        j["density_scale"] = c->densityScale;

        // Medium
        j["sigma_a"] = c->sigmaA;
        j["sigma_s"] = c->sigmaS;
        j["phase_g"] = c->phaseG;
        uz::json::WriteVec3(j, "emission", c->emission);

        // Control
        j["enabled"] = c->enabled;

        return true;
    }
);
