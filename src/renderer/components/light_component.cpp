// light_component.cpp — Directional light extraction (RHI-agnostic).
//
// Uses engine math types. Reads DirectionalLightData from ComponentStore.

#include "light_component.h"

#include "engine/scene/scene.h"
#include "engine/scene/scene_node.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"

#include <cmath>

namespace uz {

LightState light_collect_primary(const Scene* scene) {
    LightState state{};

    // Default: white sunlight from above-right.
    state.direction = {0.0f, -0.707f, 0.707f, 0.0f};
    state.color     = {1.0f, 1.0f, 1.0f, 0.0f};
    state.intensity = 1.0f;

    if (!scene) return state;

    // Find the first active DirectionalLight node.
    for (uint32_t i = 1; i < scene->next_id; ++i) {
        const SceneNode& node = scene->nodes[i];
        if (!node.alive || node.type != NodeType::DirectionalLight) continue;
        if (!(node.flags & NODE_FLAG_ACTIVE)) continue;

        // Extract forward direction from world transform (Z column in LH).
        const Mat4& wt = node.world_transform;
        const float fx = wt.m[2][0];
        const float fy = wt.m[2][1];
        const float fz = wt.m[2][2];

        const float len = std::sqrt(fx * fx + fy * fy + fz * fz);
        if (len > 1e-6f) {
            state.direction = {fx / len, fy / len, fz / len, 0.0f};
        } else {
            state.direction = {0.0f, -1.0f, 0.0f, 0.0f};
        }

        // Read color/intensity from ComponentStore.
        const DirectionalLightData* ld =
            component_get<DirectionalLightData>(&scene->components, i);
        if (ld) {
            state.color     = ld->color;
            state.intensity = ld->intensity;
        }

        break; // Use first active light.
    }

    return state;
}

} // namespace uz
