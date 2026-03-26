/// reflection_probe_component.cpp — Serialization for ReflectionProbeComponent.
///
/// Registers a type-erased serializer entry so that the scene loader can
/// dynamically create and serialize ReflectionProbeComponent by the JSON key
/// "reflection_probe" without hardcoded dispatch.

#include "reflection_probe_component.h"
#include "component_serializer.h"
#include "world.h"
#include "../core/json_helpers.h"

#include <json.hpp>
#include <entt/entt.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

UZ_REGISTER_COMPONENT_SERIALIZER(reflection_probe, uz::ReflectionProbeComponent,
    // Deserialize
    [](uz::World& world, uz::Entity entity, const void* jsonPtr) {
        const auto& j = *static_cast<const nlohmann::json*>(jsonPtr);
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        auto& c = world.GetRegistry().emplace<uz::ReflectionProbeComponent>(entt);

        const uz::ReflectionProbeComponent defaults{};

        // Influence volume
        c.influenceRadius = uz::json::Float(j, "influence_radius", defaults.influenceRadius);
        c.blendDistance   = uz::json::Float(j, "blend_distance", defaults.blendDistance);

        // Capture configuration
        c.resolution = uz::json::Uint(j, "resolution", defaults.resolution);

        // Priority & state
        c.priority     = uz::json::Int(j, "priority", defaults.priority);
        c.enabled      = uz::json::Bool(j, "enabled", defaults.enabled);
        c.needsCapture = uz::json::Bool(j, "needs_capture", defaults.needsCapture);

        // Runtime SRV indices are NOT deserialized — they are filled by the
        // renderer's capture system at runtime.
    },
    // Serialize
    [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        const auto* c = world.GetRegistry().try_get<uz::ReflectionProbeComponent>(entt);
        if (!c) return false;
        auto& j = *static_cast<nlohmann::json*>(jsonPtr);

        // Influence volume
        j["influence_radius"] = c->influenceRadius;
        j["blend_distance"]   = c->blendDistance;

        // Capture configuration
        j["resolution"] = c->resolution;

        // Priority & state
        j["priority"]      = c->priority;
        j["enabled"]       = c->enabled;
        j["needs_capture"] = c->needsCapture;

        // Runtime SRV indices (prefilteredSrvIndex, irradianceSrvIndex) are NOT
        // serialized — they are transient GPU resource handles filled by the
        // capture system at runtime.

        return true;
    }
);
