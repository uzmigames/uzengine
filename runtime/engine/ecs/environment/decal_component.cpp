/// decal_component.cpp — Serialization for DecalComponent.
///
/// Registers a type-erased serializer entry so that the scene loader can
/// dynamically create and serialize DecalComponent by the JSON key
/// "decal" without hardcoded dispatch.

#include "decal_component.h"
#include "component_serializer.h"
#include "world.h"
#include "../core/json_helpers.h"

#include <json.hpp>
#include <entt/entt.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

UZ_REGISTER_COMPONENT_SERIALIZER(decal, uz::DecalComponent,
    // Deserialize
    [](uz::World& world, uz::Entity entity, const void* jsonPtr) {
        const auto& j = *static_cast<const nlohmann::json*>(jsonPtr);
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        auto& c = world.GetRegistry().emplace<uz::DecalComponent>(entt);

        const uz::DecalComponent defaults{};

        // Projection volume
        c.extentsX = uz::json::Float(j, "extents_x", defaults.extentsX);
        c.extentsY = uz::json::Float(j, "extents_y", defaults.extentsY);
        c.extentsZ = uz::json::Float(j, "extents_z", defaults.extentsZ);

        // Textures — SRV indices are runtime-only; deserialized as UINT32_MAX
        // unless explicitly stored (e.g., for editor re-association by asset path).
        c.albedoTextureId   = uz::json::Uint(j, "albedo_texture_id", defaults.albedoTextureId);
        c.normalTextureId   = uz::json::Uint(j, "normal_texture_id", defaults.normalTextureId);
        c.materialTextureId = uz::json::Uint(j, "material_texture_id", defaults.materialTextureId);

        // Material properties
        c.opacity     = uz::json::Float(j, "opacity", defaults.opacity);
        c.normalBlend = uz::json::Float(j, "normal_blend", defaults.normalBlend);
        c.roughness   = uz::json::Float(j, "roughness", defaults.roughness);
        c.metallic    = uz::json::Float(j, "metallic", defaults.metallic);

        // Sorting & lifetime
        c.priority       = uz::json::Int(j, "priority", defaults.priority);
        c.fadeDistance    = uz::json::Float(j, "fade_distance", defaults.fadeDistance);
        c.angleFadeStart = uz::json::Float(j, "angle_fade_start", defaults.angleFadeStart);

        // Control
        c.enabled = uz::json::Bool(j, "enabled", defaults.enabled);
    },
    // Serialize
    [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        const auto* c = world.GetRegistry().try_get<uz::DecalComponent>(entt);
        if (!c) return false;
        auto& j = *static_cast<nlohmann::json*>(jsonPtr);

        // Projection volume
        j["extents_x"] = c->extentsX;
        j["extents_y"] = c->extentsY;
        j["extents_z"] = c->extentsZ;

        // Textures
        j["albedo_texture_id"]   = c->albedoTextureId;
        j["normal_texture_id"]   = c->normalTextureId;
        j["material_texture_id"] = c->materialTextureId;

        // Material properties
        j["opacity"]      = c->opacity;
        j["normal_blend"] = c->normalBlend;
        j["roughness"]    = c->roughness;
        j["metallic"]     = c->metallic;

        // Sorting & lifetime
        j["priority"]        = c->priority;
        j["fade_distance"]   = c->fadeDistance;
        j["angle_fade_start"] = c->angleFadeStart;

        // Control
        j["enabled"] = c->enabled;

        return true;
    }
);
