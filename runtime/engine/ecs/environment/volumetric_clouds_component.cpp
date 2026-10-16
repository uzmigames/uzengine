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
        uz::VolumetricCloudsComponent c{};

        const uz::VolumetricCloudsComponent defaults{};

        c.weatherTexturePath = uz::json::String(j, "weather_texture_path", defaults.weatherTexturePath);
        if (const auto it = j.find("layers"); it != j.end()) {
            if (!it->is_array() || it->empty() || it->size() > c.layers.size()) {
                throw nlohmann::json::out_of_range::create(
                    401, "volumetric_clouds.layers must contain 1 to 4 layers", &j);
            }
            c.layerCount = static_cast<uint32_t>(it->size());
            for (uint32_t i = 0; i < c.layerCount; ++i) {
                const auto& layerJson = (*it)[i];
                auto& layer = c.layers[i];
                if (!layerJson.is_object()) {
                    throw nlohmann::json::type_error::create(
                        302, "volumetric_clouds.layers entries must be objects", &layerJson);
                }
                if (const auto channel = layerJson.find("channel"); channel != layerJson.end()) {
                    if (!channel->is_number_unsigned() || channel->get<uint64_t>() > 3) {
                        throw nlohmann::json::out_of_range::create(
                            401, "volumetric_clouds layer channel must be an integer from 0 to 3", &layerJson);
                    }
                    layer.channel = channel->get<uint32_t>();
                }
                layer.altitude = uz::json::Float(layerJson, "altitude", layer.altitude);
                layer.height = uz::json::Float(layerJson, "height", layer.height);
                if (!(layer.height > 0.0f)) {
                    throw nlohmann::json::out_of_range::create(
                        401, "volumetric_clouds layer height must be positive", &layerJson);
                }
                layer.densityScale = uz::json::Float(layerJson, "density_scale", layer.densityScale);
                layer.shapeAmount = uz::json::Float(layerJson, "shape_amount", layer.shapeAmount);
                layer.detailAmount = uz::json::Float(layerJson, "detail_amount", layer.detailAmount);
                layer.weatherExponent = uz::json::Float(layerJson, "weather_exponent", layer.weatherExponent);
                layer.shapeAlteringBias = uz::json::Float(layerJson, "shape_altering_bias", layer.shapeAlteringBias);
                if (const auto profile = layerJson.find("density_profile"); profile != layerJson.end()) {
                    layer.densityProfile.exp = uz::json::Float(*profile, "exp", layer.densityProfile.exp);
                    layer.densityProfile.expExponent = uz::json::Float(*profile, "exp_exponent", layer.densityProfile.expExponent);
                    layer.densityProfile.linear = uz::json::Float(*profile, "linear", layer.densityProfile.linear);
                    layer.densityProfile.constant = uz::json::Float(*profile, "constant", layer.densityProfile.constant);
                }
            }
            c.useExplicitLayers = true;
        }

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
        c.powderScale = uz::json::Float(j, "powder_scale", defaults.powderScale);
        c.powderExponent = uz::json::Float(j, "powder_exponent", defaults.powderExponent);
        c.multiScatterOctaves = uz::json::Uint(j, "multi_scatter_octaves", defaults.multiScatterOctaves);

        // Ray march parameters
        c.tracingMaxDistanceKm = uz::json::Float(j, "tracing_max_distance_km", defaults.tracingMaxDistanceKm);
        c.primarySteps         = uz::json::Uint(j, "primary_steps", defaults.primarySteps);
        c.lightSteps           = uz::json::Uint(j, "light_steps", defaults.lightSteps);
        c.minStepSize = uz::json::Float(j, "min_step_size", defaults.minStepSize);
        c.maxStepSize = uz::json::Float(j, "max_step_size", defaults.maxStepSize);
        c.perspectiveStepScale = uz::json::Float(j, "perspective_step_scale", defaults.perspectiveStepScale);
        c.jitterScale = uz::json::Float(j, "jitter_scale", defaults.jitterScale);
        c.shadowCascadeCount = uz::json::Uint(j, "shadow_cascade_count", defaults.shadowCascadeCount);
        c.shadowResolution = uz::json::Uint(j, "shadow_resolution", defaults.shadowResolution);
        c.shadowDistance = uz::json::Float(j, "shadow_distance", defaults.shadowDistance);

        // Control
        c.enabled = uz::json::Bool(j, "enabled", defaults.enabled);
        world.GetRegistry().emplace<uz::VolumetricCloudsComponent>(entt, std::move(c));
    },
    // Serialize
    [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        const auto* c = world.GetRegistry().try_get<uz::VolumetricCloudsComponent>(entt);
        if (!c) return false;
        auto& j = *static_cast<nlohmann::json*>(jsonPtr);

        j["weather_texture_path"] = c->weatherTexturePath;
        if (c->useExplicitLayers) {
            if (c->layerCount == 0 || c->layerCount > c->layers.size()) {
                throw nlohmann::json::out_of_range::create(
                    401, "volumetric_clouds.layerCount must be from 1 to 4", &j);
            }
            j["layers"] = nlohmann::json::array();
            for (uint32_t i = 0; i < c->layerCount; ++i) {
                const auto& layer = c->layers[i];
                auto layerJson = nlohmann::json::object();
                layerJson["channel"] = layer.channel;
                layerJson["altitude"] = layer.altitude;
                layerJson["height"] = layer.height;
                layerJson["density_scale"] = layer.densityScale;
                layerJson["shape_amount"] = layer.shapeAmount;
                layerJson["detail_amount"] = layer.detailAmount;
                layerJson["weather_exponent"] = layer.weatherExponent;
                layerJson["shape_altering_bias"] = layer.shapeAlteringBias;
                auto& profile = layerJson["density_profile"];
                profile["exp"] = layer.densityProfile.exp;
                profile["exp_exponent"] = layer.densityProfile.expExponent;
                profile["linear"] = layer.densityProfile.linear;
                profile["constant"] = layer.densityProfile.constant;
                j["layers"].push_back(std::move(layerJson));
            }
        } else {
            j.erase("layers");
        }

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
        j["powder_scale"] = c->powderScale;
        j["powder_exponent"] = c->powderExponent;
        j["multi_scatter_octaves"] = c->multiScatterOctaves;

        // Ray march parameters
        j["tracing_max_distance_km"] = c->tracingMaxDistanceKm;
        j["primary_steps"]           = c->primarySteps;
        j["light_steps"]             = c->lightSteps;
        j["min_step_size"] = c->minStepSize;
        j["max_step_size"] = c->maxStepSize;
        j["perspective_step_scale"] = c->perspectiveStepScale;
        j["jitter_scale"] = c->jitterScale;
        j["shadow_cascade_count"] = c->shadowCascadeCount;
        j["shadow_resolution"] = c->shadowResolution;
        j["shadow_distance"] = c->shadowDistance;

        // Control
        j["enabled"] = c->enabled;

        return true;
    }
);
