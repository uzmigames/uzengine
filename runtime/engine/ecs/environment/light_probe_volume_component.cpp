/// light_probe_volume_component.cpp — Serialization for LightProbeVolumeComponent.
///
/// Registers a type-erased serializer entry so that the scene loader can
/// dynamically create and serialize LightProbeVolumeComponent by the JSON
/// key "light_probe_volume" without hardcoded dispatch.

#include "light_probe_volume_component.h"
#include "component_serializer.h"
#include "world.h"
#include "../core/json_helpers.h"

#include <json.hpp>
#include <entt/entt.hpp>

UZ_REGISTER_COMPONENT_SERIALIZER(light_probe_volume, uz::LightProbeVolumeComponent,
    [](uz::World& world, uz::Entity entity, const void* jsonPtr) {
        const auto& j = *static_cast<const nlohmann::json*>(jsonPtr);
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        auto& c = world.GetRegistry().emplace<uz::LightProbeVolumeComponent>(entt);

        const uz::LightProbeVolumeComponent defaults{};

        c.gridDimensionsX = uz::json::Uint(j, "grid_dimensions_x", defaults.gridDimensionsX);
        c.gridDimensionsY = uz::json::Uint(j, "grid_dimensions_y", defaults.gridDimensionsY);
        c.gridDimensionsZ = uz::json::Uint(j, "grid_dimensions_z", defaults.gridDimensionsZ);

        c.extentMin       = uz::json::Vec3FromArray(j, "extent_min", defaults.extentMin);
        c.extentMax       = uz::json::Vec3FromArray(j, "extent_max", defaults.extentMax);

        c.autoFitScene    = uz::json::Bool(j, "auto_fit_scene", defaults.autoFitScene);
        c.autoFitPadding  = uz::json::Float(j, "auto_fit_padding", defaults.autoFitPadding);

        c.enabled         = uz::json::Bool(j, "enabled", defaults.enabled);
        c.needsRebuild    = true;
    },
    [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        const auto* c = world.GetRegistry().try_get<uz::LightProbeVolumeComponent>(entt);
        if (!c) return false;
        auto& j = *static_cast<nlohmann::json*>(jsonPtr);

        j["grid_dimensions_x"] = c->gridDimensionsX;
        j["grid_dimensions_y"] = c->gridDimensionsY;
        j["grid_dimensions_z"] = c->gridDimensionsZ;
        uz::json::WriteVec3(j, "extent_min", c->extentMin);
        uz::json::WriteVec3(j, "extent_max", c->extentMax);
        j["auto_fit_scene"]    = c->autoFitScene;
        j["auto_fit_padding"]  = c->autoFitPadding;
        j["enabled"]           = c->enabled;
        return true;
    }
);
