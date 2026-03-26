/// sky_atmosphere_component.cpp — Serialization for SkyAtmosphereComponent.
///
/// Registers a type-erased serializer entry so that the scene loader can
/// dynamically create and serialize SkyAtmosphereComponent by the JSON key
/// "sky_atmosphere" without hardcoded dispatch.

#include "sky_atmosphere_component.h"
#include "component_serializer.h"
#include "world.h"
#include "../core/json_helpers.h"

#include <json.hpp>
#include <entt/entt.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

UZ_REGISTER_COMPONENT_SERIALIZER(sky_atmosphere, uz::SkyAtmosphereComponent,
    // Deserialize
    [](uz::World& world, uz::Entity entity, const void* jsonPtr) {
        const auto& j = *static_cast<const nlohmann::json*>(jsonPtr);
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        auto& c = world.GetRegistry().emplace<uz::SkyAtmosphereComponent>(entt);

        // Defaults match the struct's member initializers so the shared helpers
        // fall through correctly when a key is absent.
        const uz::SkyAtmosphereComponent defaults{};

        // Planet geometry
        c.bottomRadiusKm    = uz::json::Float(j, "bottom_radius_km",     defaults.bottomRadiusKm);
        c.atmosphereHeightKm = uz::json::Float(j, "atmosphere_height_km", defaults.atmosphereHeightKm);

        // Rayleigh scattering
        c.rayleighScattering      = uz::json::Vec3FromArray(j, "rayleigh_scattering", defaults.rayleighScattering);
        c.rayleighScatteringScale = uz::json::Float(j, "rayleigh_scattering_scale", defaults.rayleighScatteringScale);
        c.rayleighScaleHeightKm   = uz::json::Float(j, "rayleigh_scale_height_km", defaults.rayleighScaleHeightKm);

        // Mie scattering
        c.mieScattering      = uz::json::Float(j, "mie_scattering",       defaults.mieScattering);
        c.mieScatteringScale = uz::json::Float(j, "mie_scattering_scale", defaults.mieScatteringScale);
        c.mieAbsorption      = uz::json::Float(j, "mie_absorption",       defaults.mieAbsorption);
        c.mieAbsorptionScale = uz::json::Float(j, "mie_absorption_scale", defaults.mieAbsorptionScale);
        c.mieAnisotropy      = uz::json::Float(j, "mie_anisotropy",       defaults.mieAnisotropy);
        c.mieScaleHeightKm   = uz::json::Float(j, "mie_scale_height_km", defaults.mieScaleHeightKm);

        // Ground
        c.groundAlbedo = uz::json::Vec3FromArray(j, "ground_albedo", defaults.groundAlbedo);

        // Ozone
        c.ozoneAbsorption = uz::json::Vec3FromArray(j, "ozone_absorption", defaults.ozoneAbsorption);
        c.ozoneTipAltitude = uz::json::Float(j, "ozone_tip_altitude", defaults.ozoneTipAltitude);
        c.ozoneTipValue    = uz::json::Float(j, "ozone_tip_value",    defaults.ozoneTipValue);
        c.ozoneWidth       = uz::json::Float(j, "ozone_width",        defaults.ozoneWidth);

        // Multi-scattering
        c.multiScatteringFactor = uz::json::Float(j, "multi_scattering_factor", defaults.multiScatteringFactor);

        // Art direction
        c.skyLuminanceFactor   = uz::json::Vec3FromArray(j, "sky_luminance_factor", defaults.skyLuminanceFactor);
        c.heightFogContribution = uz::json::Float(j, "height_fog_contribution", defaults.heightFogContribution);

        // Transform mode
        c.transformMode = static_cast<uz::SkyAtmosphereComponent::TransformMode>(
            uz::json::Int(j, "transform_mode", static_cast<int32_t>(defaults.transformMode)));

        // Sun fallback
        c.sunIlluminance  = uz::json::Vec3FromArray(j, "sun_illuminance", defaults.sunIlluminance);
        c.sunAngularRadius = uz::json::Float(j, "sun_angular_radius", defaults.sunAngularRadius);
        c.sunDiskIntensity = uz::json::Float(j, "sun_disk_intensity", defaults.sunDiskIntensity);

        // Deprecated sky light fields
        c.skyLightIntensity = uz::json::Float(j, "sky_light_intensity", defaults.skyLightIntensity);
        c.skyLightColor     = uz::json::Vec3FromArray(j, "sky_light_color", defaults.skyLightColor);

        // Control
        c.enabled = uz::json::Bool(j, "enabled", defaults.enabled);
    },
    // Serialize
    [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        const auto* c = world.GetRegistry().try_get<uz::SkyAtmosphereComponent>(entt);
        if (!c) return false;
        auto& j = *static_cast<nlohmann::json*>(jsonPtr);

        // Planet geometry
        j["bottom_radius_km"]      = c->bottomRadiusKm;
        j["atmosphere_height_km"]  = c->atmosphereHeightKm;

        // Rayleigh
        uz::json::WriteVec3(j, "rayleigh_scattering", c->rayleighScattering);
        j["rayleigh_scattering_scale"] = c->rayleighScatteringScale;
        j["rayleigh_scale_height_km"]  = c->rayleighScaleHeightKm;

        // Mie
        j["mie_scattering"]       = c->mieScattering;
        j["mie_scattering_scale"] = c->mieScatteringScale;
        j["mie_absorption"]       = c->mieAbsorption;
        j["mie_absorption_scale"] = c->mieAbsorptionScale;
        j["mie_anisotropy"]       = c->mieAnisotropy;
        j["mie_scale_height_km"]  = c->mieScaleHeightKm;

        // Ground
        uz::json::WriteVec3(j, "ground_albedo", c->groundAlbedo);

        // Ozone
        uz::json::WriteVec3(j, "ozone_absorption", c->ozoneAbsorption);
        j["ozone_tip_altitude"] = c->ozoneTipAltitude;
        j["ozone_tip_value"]    = c->ozoneTipValue;
        j["ozone_width"]        = c->ozoneWidth;

        // Multi-scattering
        j["multi_scattering_factor"] = c->multiScatteringFactor;

        // Art direction
        uz::json::WriteVec3(j, "sky_luminance_factor", c->skyLuminanceFactor);
        j["height_fog_contribution"] = c->heightFogContribution;

        // Transform mode
        j["transform_mode"] = static_cast<int32_t>(c->transformMode);

        // Sun fallback
        uz::json::WriteVec3(j, "sun_illuminance", c->sunIlluminance);
        j["sun_angular_radius"] = c->sunAngularRadius;
        j["sun_disk_intensity"] = c->sunDiskIntensity;

        // Deprecated sky light
        j["sky_light_intensity"] = c->skyLightIntensity;
        uz::json::WriteVec3(j, "sky_light_color", c->skyLightColor);

        // Control
        j["enabled"] = c->enabled;

        return true;
    }
);
