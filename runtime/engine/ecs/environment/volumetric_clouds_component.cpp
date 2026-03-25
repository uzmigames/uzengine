/// volumetric_clouds_component.cpp — Serialization for VolumetricCloudsComponent.
///
/// Registers a type-erased serializer entry so that the scene loader can
/// dynamically create and serialize VolumetricCloudsComponent by the JSON key
/// "volumetric_clouds" without hardcoded dispatch.

#include "volumetric_clouds_component.h"
#include "component_serializer.h"
#include "world.h"
#include "../core/json_helpers.h"

#include <json.hpp>
#include <entt/entt.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

UZ_REGISTER_COMPONENT_SERIALIZER(volumetric_clouds, uz::VolumetricCloudsComponent,
    // Deserialize
    [](uz::World& world, uz::Entity entity, const void* jsonPtr) {
        const auto& j = *static_cast<const nlohmann::json*>(jsonPtr);
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        auto& c = world.GetRegistry().emplace<uz::VolumetricCloudsComponent>(entt);

        const uz::VolumetricCloudsComponent defaults{};

        // Cloud layer geometry
        c.layerBottomAltitudeKm = uz::json::Float(j, "layer_bottom_altitude_km", defaults.layerBottomAltitudeKm);
        c.layerHeightKm         = uz::json::Float(j, "layer_height_km", defaults.layerHeightKm);

        // Cloud density
        c.coverage  = uz::json::Float(j, "coverage", defaults.coverage);
        c.density   = uz::json::Float(j, "density", defaults.density);
        c.cloudType = uz::json::Float(j, "cloud_type", defaults.cloudType);

        // Wind
        c.windVelocity    = uz::json::Vec3FromArray(j, "wind_velocity", defaults.windVelocity);
        c.detailWindScale = uz::json::Float(j, "detail_wind_scale", defaults.detailWindScale);

        // Absorption & Scattering
        c.extinction       = uz::json::Float(j, "extinction", defaults.extinction);
        c.scatteringAlbedo = uz::json::Float(j, "scattering_albedo", defaults.scatteringAlbedo);
        c.phaseG           = uz::json::Float(j, "phase_g", defaults.phaseG);
        c.phaseG2          = uz::json::Float(j, "phase_g2", defaults.phaseG2);
        c.phaseBlend       = uz::json::Float(j, "phase_blend", defaults.phaseBlend);

        // Multi-scatter approximation
        c.ambientMultiplier = uz::json::Float(j, "ambient_multiplier", defaults.ambientMultiplier);

        // Ray march parameters
        c.tracingMaxDistanceKm = uz::json::Float(j, "tracing_max_distance_km", defaults.tracingMaxDistanceKm);
        c.primarySteps         = uz::json::Uint(j, "primary_steps", defaults.primarySteps);
        c.lightSteps           = uz::json::Uint(j, "light_steps", defaults.lightSteps);

        // Control
        c.enabled = uz::json::Bool(j, "enabled", defaults.enabled);
    },
    // Serialize
    [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        const auto* c = world.GetRegistry().try_get<uz::VolumetricCloudsComponent>(entt);
        if (!c) return false;
        auto& j = *static_cast<nlohmann::json*>(jsonPtr);

        // Cloud layer geometry
        j["layer_bottom_altitude_km"] = c->layerBottomAltitudeKm;
        j["layer_height_km"]          = c->layerHeightKm;

        // Cloud density
        j["coverage"]   = c->coverage;
        j["density"]    = c->density;
        j["cloud_type"] = c->cloudType;

        // Wind
        uz::json::WriteVec3(j, "wind_velocity", c->windVelocity);
        j["detail_wind_scale"] = c->detailWindScale;

        // Absorption & Scattering
        j["extinction"]        = c->extinction;
        j["scattering_albedo"] = c->scatteringAlbedo;
        j["phase_g"]           = c->phaseG;
        j["phase_g2"]          = c->phaseG2;
        j["phase_blend"]       = c->phaseBlend;

        // Multi-scatter approximation
        j["ambient_multiplier"] = c->ambientMultiplier;

        // Ray march parameters
        j["tracing_max_distance_km"] = c->tracingMaxDistanceKm;
        j["primary_steps"]           = c->primarySteps;
        j["light_steps"]             = c->lightSteps;

        // Control
        j["enabled"] = c->enabled;

        return true;
    }
);
