/// vfx_emitter_component.cpp — Serialization for VfxEmitterComponent.
///
/// Registers a type-erased serializer entry so that the scene loader can
/// dynamically create and serialize VfxEmitterComponent by the JSON key
/// "vfx_emitter" without hardcoded dispatch.

#include "vfx_emitter_component.h"
#include "component_serializer.h"
#include "world.h"
#include "../core/json_helpers.h"

#include <json.hpp>
#include <entt/entt.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

UZ_REGISTER_COMPONENT_SERIALIZER(vfx_emitter, uz::VfxEmitterComponent,
    // Deserialize
    [](uz::World& world, uz::Entity entity, const void* jsonPtr) {
        const auto& j = *static_cast<const nlohmann::json*>(jsonPtr);
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        auto& c = world.GetRegistry().emplace<uz::VfxEmitterComponent>(entt);

        const uz::VfxEmitterComponent defaults{};

        c.assetPath     = uz::json::String(j, "asset", "");
        c.maxParticles  = uz::json::Uint(j, "max_particles", defaults.maxParticles);
        c.emissionRate  = uz::json::Float(j, "emission_rate", defaults.emissionRate);
        c.lifetime      = uz::json::Float(j, "lifetime", defaults.lifetime);
        c.gravity       = uz::json::Vec3FromArray(j, "gravity", defaults.gravity);
        c.damping       = uz::json::Float(j, "damping", defaults.damping);
        c.spawnVelocity = uz::json::Vec3FromArray(j, "spawn_velocity", defaults.spawnVelocity);
        c.emitting      = uz::json::Bool(j, "emitting", defaults.emitting);
        c.looping       = uz::json::Bool(j, "looping", defaults.looping);
    },
    // Serialize
    [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
        auto& j = *static_cast<nlohmann::json*>(jsonPtr);
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        const auto* c = world.GetRegistry().try_get<uz::VfxEmitterComponent>(entt);
        if (!c) return false;

        if (!c->assetPath.empty())  j["asset"]          = c->assetPath;
        j["max_particles"]  = c->maxParticles;
        j["emission_rate"]  = c->emissionRate;
        j["lifetime"]       = c->lifetime;
        uz::json::WriteVec3(j, "gravity", c->gravity);
        j["damping"]        = c->damping;
        uz::json::WriteVec3(j, "spawn_velocity", c->spawnVelocity);
        j["emitting"]       = c->emitting;
        j["looping"]        = c->looping;
        return true;
    }
);
