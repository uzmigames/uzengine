/// sky_light_component.cpp — Serialization for SkyLightComponent.
///
/// Registers a type-erased serializer entry so that the scene loader can
/// dynamically create and serialize SkyLightComponent by the JSON key
/// "sky_light" without hardcoded dispatch.

#include "sky_light_component.h"
#include "component_serializer.h"
#include "world.h"
#include "../core/json_helpers.h"

#include <json.hpp>
#include <entt/entt.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

UZ_REGISTER_COMPONENT_SERIALIZER(sky_light, uz::SkyLightComponent,
    // Deserialize
    [](uz::World& world, uz::Entity entity, const void* jsonPtr) {
        const auto& j = *static_cast<const nlohmann::json*>(jsonPtr);
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        auto& c = world.GetRegistry().emplace<uz::SkyLightComponent>(entt);

        const uz::SkyLightComponent defaults{};

        // Intensity & Color
        c.intensity                  = uz::json::Float(j, "intensity", defaults.intensity);
        c.lightColor                 = uz::json::Vec3FromArray(j, "light_color", defaults.lightColor);
        c.indirectLightingIntensity  = uz::json::Float(j, "indirect_lighting_intensity", defaults.indirectLightingIntensity);

        // Source
        {
            auto it = j.find("cubemap_path");
            if (it != j.end() && it->is_string()) {
                c.cubemapPath = it->get<std::string>();
            }
        }
        if (!c.cubemapPath.empty()) {
            c.sourceType = uz::ESkyLightSourceType::SpecifiedCubemap;
        } else {
            c.sourceType = static_cast<uz::ESkyLightSourceType>(
                uz::json::Int(j, "source_type", static_cast<int32_t>(defaults.sourceType)));
        }
        c.realTimeCapture  = uz::json::Bool(j, "real_time_capture", defaults.realTimeCapture);
        c.cubemapResolution = uz::json::Uint(j, "cubemap_resolution", defaults.cubemapResolution);

        // Lower Hemisphere
        c.lowerHemisphereIsBlack = uz::json::Bool(j, "lower_hemisphere_is_black", defaults.lowerHemisphereIsBlack);
        c.lowerHemisphereColor   = uz::json::Vec3FromArray(j, "lower_hemisphere_color", defaults.lowerHemisphereColor);

        // Occlusion
        c.occlusionMaxDistance = uz::json::Float(j, "occlusion_max_distance", defaults.occlusionMaxDistance);
        c.minOcclusion         = uz::json::Float(j, "min_occlusion", defaults.minOcclusion);
        c.occlusionTint        = uz::json::Vec3FromArray(j, "occlusion_tint", defaults.occlusionTint);
        c.occlusionExponent    = uz::json::Float(j, "occlusion_exponent", defaults.occlusionExponent);

        // Control
        c.enabled = uz::json::Bool(j, "enabled", defaults.enabled);
    },
    // Serialize
    [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        const auto* c = world.GetRegistry().try_get<uz::SkyLightComponent>(entt);
        if (!c) return false;
        auto& j = *static_cast<nlohmann::json*>(jsonPtr);

        // Intensity & Color
        j["intensity"]                   = c->intensity;
        uz::json::WriteVec3(j, "light_color", c->lightColor);
        j["indirect_lighting_intensity"] = c->indirectLightingIntensity;

        // Source
        if (!c->cubemapPath.empty()) {
            j["cubemap_path"] = c->cubemapPath;
        }
        j["source_type"]        = static_cast<int32_t>(c->sourceType);
        j["real_time_capture"]  = c->realTimeCapture;
        j["cubemap_resolution"] = c->cubemapResolution;

        // Lower Hemisphere
        j["lower_hemisphere_is_black"] = c->lowerHemisphereIsBlack;
        uz::json::WriteVec3(j, "lower_hemisphere_color", c->lowerHemisphereColor);

        // Occlusion
        j["occlusion_max_distance"] = c->occlusionMaxDistance;
        j["min_occlusion"]          = c->minOcclusion;
        uz::json::WriteVec3(j, "occlusion_tint", c->occlusionTint);
        j["occlusion_exponent"]     = c->occlusionExponent;

        // Control
        j["enabled"] = c->enabled;

        return true;
    }
);
