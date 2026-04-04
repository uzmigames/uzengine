/// decal_component.cpp — Serialization for DecalComponent.
///
/// Creates a Material asset (domain=Decal) from the scene file's texture
/// paths and material properties, then assigns it to the DecalComponent.
/// DecalComponent references a MaterialInterface, NOT raw textures.

#include "decal_component.h"
#include "component_serializer.h"
#include "world.h"
#include "../core/json_helpers.h"

#include "engine/renderer/material/material_asset.h"

#include <json.hpp>
#include <entt/entt.hpp>

#include <memory>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Static storage for scene-loaded decal materials (same pattern as scene_loader)
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<std::unique_ptr<uz::Material>> s_decalMaterials;

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

        // ── Create a Decal material from scene file properties ───────────
        auto mat = std::make_unique<uz::Material>();
        mat->materialDomain = uz::EMaterialDomain::Decal;

        // Texture paths — same naming as ParseMaterialComponent in scene_loader
        mat->albedoPath    = uz::json::String(j, "albedo", "");
        mat->normalPath    = uz::json::String(j, "normal", "");
        mat->roughnessPath = uz::json::String(j, "roughness", "");
        mat->metallicPath  = uz::json::String(j, "metallic", "");
        mat->aoPath        = uz::json::String(j, "ao", "");
        mat->emissivePath  = uz::json::String(j, "emissive", "");
        mat->opacityPath   = uz::json::String(j, "opacity_mask", "");

        // Material properties
        mat->opacity             = uz::json::Float(j, "opacity", 1.0f);
        mat->normalIntensity     = uz::json::Float(j, "normal_blend", 1.0f);
        mat->roughnessMultiplier = uz::json::Float(j, "roughness", 0.5f);
        mat->metallicMultiplier  = uz::json::Float(j, "metallic", 0.0f);

        // Decal blend mode (string → enum)
        const std::string blendStr = uz::json::String(j, "decal_blend_mode", "DBuffer_ColorNormalRoughness");
        if (blendStr == "Translucent")                  mat->decalBlendMode = uz::EDecalBlendMode::Translucent;
        else if (blendStr == "Stain")                   mat->decalBlendMode = uz::EDecalBlendMode::Stain;
        else if (blendStr == "Normal")                  mat->decalBlendMode = uz::EDecalBlendMode::Normal;
        else if (blendStr == "DBuffer_Color")           mat->decalBlendMode = uz::EDecalBlendMode::DBuffer_Color;
        else if (blendStr == "DBuffer_ColorNormal")     mat->decalBlendMode = uz::EDecalBlendMode::DBuffer_ColorNormal;
        else if (blendStr == "DBuffer_ColorRoughness")  mat->decalBlendMode = uz::EDecalBlendMode::DBuffer_ColorRoughness;
        else if (blendStr == "DBuffer_Normal")          mat->decalBlendMode = uz::EDecalBlendMode::DBuffer_Normal;
        else if (blendStr == "DBuffer_NormalRoughness") mat->decalBlendMode = uz::EDecalBlendMode::DBuffer_NormalRoughness;
        else if (blendStr == "DBuffer_Roughness")       mat->decalBlendMode = uz::EDecalBlendMode::DBuffer_Roughness;
        else                                            mat->decalBlendMode = uz::EDecalBlendMode::DBuffer_ColorNormalRoughness;

        // Assign material to component (non-owning pointer)
        c.material = mat.get();

        // Transfer ownership to static storage
        s_decalMaterials.push_back(std::move(mat));

        // ── Projection / sorting (component-level, not material) ─────────
        c.priority       = uz::json::Int(j, "priority", defaults.priority);
        c.fadeDistance    = uz::json::Float(j, "fade_distance", defaults.fadeDistance);
        c.angleFadeStart = uz::json::Float(j, "angle_fade_start", defaults.angleFadeStart);
        c.enabled        = uz::json::Bool(j, "enabled", defaults.enabled);
    },
    // Serialize
    [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        const auto* c = world.GetRegistry().try_get<uz::DecalComponent>(entt);
        if (!c) return false;
        auto& j = *static_cast<nlohmann::json*>(jsonPtr);

        // Write material properties if material is set
        if (c->material) {
            const auto& albedo = c->material->GetTexturePath("Albedo");
            const auto& normal = c->material->GetTexturePath("Normal");
            const auto& rough  = c->material->GetTexturePath("Roughness");
            const auto& metal  = c->material->GetTexturePath("Metallic");

            if (!albedo.empty()) j["albedo"]    = albedo;
            if (!normal.empty()) j["normal"]    = normal;
            if (!rough.empty())  j["roughness"] = rough;
            if (!metal.empty())  j["metallic"]  = metal;

            j["opacity"]      = c->material->GetOpacity();
            j["normal_blend"] = c->material->GetNormalIntensity();

            j["decal_blend_mode"] = uz::DecalBlendModeToString(c->material->GetDecalBlendMode());
        }

        // Projection / sorting
        j["priority"]         = c->priority;
        j["fade_distance"]    = c->fadeDistance;
        j["angle_fade_start"] = c->angleFadeStart;
        j["enabled"]          = c->enabled;

        return true;
    }
);
