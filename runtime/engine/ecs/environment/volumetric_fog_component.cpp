/// volumetric_fog_component.cpp — Serialization for VolumetricFogComponent.
///
/// Registers a type-erased serializer entry so that the scene loader can
/// dynamically create and serialize VolumetricFogComponent by the JSON key
/// "volumetric_fog" without hardcoded dispatch.

#include "volumetric_fog_component.h"
#include "component_serializer.h"
#include "world.h"
#include "../core/json_helpers.h"

#include <json.hpp>
#include <entt/entt.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

UZ_REGISTER_COMPONENT_SERIALIZER(volumetric_fog, uz::VolumetricFogComponent,
    // Deserialize
    [](uz::World& world, uz::Entity entity, const void* jsonPtr) {
        const auto& j = *static_cast<const nlohmann::json*>(jsonPtr);
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        auto& c = world.GetRegistry().emplace<uz::VolumetricFogComponent>(entt);

        const uz::VolumetricFogComponent defaults{};

        // Medium
        c.density            = uz::json::Float(j, "density", defaults.density);
        c.globalDensity      = uz::json::Float(j, "global_density", defaults.globalDensity);
        c.albedo             = uz::json::Vec3FromArray(j, "albedo", defaults.albedo);
        c.phaseG             = uz::json::Float(j, "phase_g", defaults.phaseG);
        c.heightFalloff      = uz::json::Float(j, "height_falloff", defaults.heightFalloff);
        c.heightOffset       = uz::json::Float(j, "height_offset", defaults.heightOffset);
        c.extinctionScale    = uz::json::Float(j, "extinction_scale", defaults.extinctionScale);
        c.emissive           = uz::json::Vec3FromArray(j, "emissive", defaults.emissive);

        // Grid range
        c.distance           = uz::json::Float(j, "distance", defaults.distance);
        c.startDistance      = uz::json::Float(j, "start_distance", defaults.startDistance);
        c.nearFadeInDistance = uz::json::Float(j, "near_fade_in_distance", defaults.nearFadeInDistance);

        // Lighting
        c.ambientIntensity   = uz::json::Float(j, "ambient_intensity", defaults.ambientIntensity);
        c.localLightScattering = uz::json::Float(j, "local_light_scattering", defaults.localLightScattering);

        // Control
        c.enabled = uz::json::Bool(j, "enabled", defaults.enabled);
    },
    // Serialize
    [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        const auto* c = world.GetRegistry().try_get<uz::VolumetricFogComponent>(entt);
        if (!c) return false;
        auto& j = *static_cast<nlohmann::json*>(jsonPtr);

        // Medium
        j["density"]          = c->density;
        j["global_density"]   = c->globalDensity;
        uz::json::WriteVec3(j, "albedo", c->albedo);
        j["phase_g"]          = c->phaseG;
        j["height_falloff"]   = c->heightFalloff;
        j["height_offset"]    = c->heightOffset;
        j["extinction_scale"] = c->extinctionScale;
        uz::json::WriteVec3(j, "emissive", c->emissive);

        // Grid range
        j["distance"]              = c->distance;
        j["start_distance"]        = c->startDistance;
        j["near_fade_in_distance"] = c->nearFadeInDistance;

        // Lighting
        j["ambient_intensity"] = c->ambientIntensity;
        j["local_light_scattering"] = c->localLightScattering;

        // Control
        j["enabled"] = c->enabled;

        return true;
    }
);
