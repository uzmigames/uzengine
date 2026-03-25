/// height_fog_component.cpp — Serialization for HeightFogComponent.
///
/// Registers a type-erased serializer entry so that the scene loader can
/// dynamically create and serialize HeightFogComponent by the JSON key
/// "height_fog" without hardcoded dispatch.

#include "height_fog_component.h"
#include "component_serializer.h"
#include "world.h"
#include "../core/json_helpers.h"

#include <json.hpp>
#include <entt/entt.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

UZ_REGISTER_COMPONENT_SERIALIZER(height_fog, uz::HeightFogComponent,
    // Deserialize
    [](uz::World& world, uz::Entity entity, const void* jsonPtr) {
        const auto& j = *static_cast<const nlohmann::json*>(jsonPtr);
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        auto& c = world.GetRegistry().emplace<uz::HeightFogComponent>(entt);

        const uz::HeightFogComponent defaults{};

        // Primary fog layer
        c.color          = uz::json::Vec3FromArray(j, "color", defaults.color);
        c.density        = uz::json::Float(j, "density", defaults.density);
        c.heightFalloff  = uz::json::Float(j, "height_falloff", defaults.heightFalloff);
        c.baseHeight     = uz::json::Float(j, "base_height", defaults.baseHeight);
        c.maxOpacity     = uz::json::Float(j, "max_opacity", defaults.maxOpacity);
        c.startDistance  = uz::json::Float(j, "start_distance", defaults.startDistance);
        c.fogCutoffDistance = uz::json::Float(j, "fog_cutoff_distance", defaults.fogCutoffDistance);

        // Second fog layer
        c.secondFogDensity       = uz::json::Float(j, "second_fog_density", defaults.secondFogDensity);
        c.secondFogHeightFalloff = uz::json::Float(j, "second_fog_height_falloff", defaults.secondFogHeightFalloff);
        c.secondFogBaseHeight    = uz::json::Float(j, "second_fog_base_height", defaults.secondFogBaseHeight);

        // Directional inscattering
        c.directionalInscatteringColor         = uz::json::Vec3FromArray(j, "directional_inscattering_color", defaults.directionalInscatteringColor);
        c.directionalInscatteringExponent      = uz::json::Float(j, "directional_inscattering_exponent", defaults.directionalInscatteringExponent);
        c.directionalInscatteringStartDistance = uz::json::Float(j, "directional_inscattering_start_distance", defaults.directionalInscatteringStartDistance);

        // Volumetric fog
        c.enableVolumetricFog = uz::json::Bool(j, "enable_volumetric_fog", defaults.enableVolumetricFog);

        // Control
        c.enabled = uz::json::Bool(j, "enabled", defaults.enabled);
    },
    // Serialize
    [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        const auto* c = world.GetRegistry().try_get<uz::HeightFogComponent>(entt);
        if (!c) return false;
        auto& j = *static_cast<nlohmann::json*>(jsonPtr);

        // Primary fog layer
        uz::json::WriteVec3(j, "color", c->color);
        j["density"]             = c->density;
        j["height_falloff"]      = c->heightFalloff;
        j["base_height"]         = c->baseHeight;
        j["max_opacity"]         = c->maxOpacity;
        j["start_distance"]      = c->startDistance;
        j["fog_cutoff_distance"] = c->fogCutoffDistance;

        // Second fog layer
        j["second_fog_density"]        = c->secondFogDensity;
        j["second_fog_height_falloff"] = c->secondFogHeightFalloff;
        j["second_fog_base_height"]    = c->secondFogBaseHeight;

        // Directional inscattering
        uz::json::WriteVec3(j, "directional_inscattering_color", c->directionalInscatteringColor);
        j["directional_inscattering_exponent"]       = c->directionalInscatteringExponent;
        j["directional_inscattering_start_distance"] = c->directionalInscatteringStartDistance;

        // Volumetric fog
        j["enable_volumetric_fog"] = c->enableVolumetricFog;

        // Control
        j["enabled"] = c->enabled;

        return true;
    }
);
