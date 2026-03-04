#pragma once

// light_component.h — Directional light data extraction (RHI-agnostic).
//
// Uses engine math types (Vec3) — no DirectX/Vulkan/Metal dependencies.

#include "engine/core/math.h"

namespace uz {

struct Scene;

// ============================================================================
// LightState — primary directional light parameters (RHI-agnostic)
// ============================================================================

struct LightState {
    Vec3  direction;    // World-space light direction (normalized)
    Vec3  color;        // Light color (RGB)
    float intensity;    // Raw intensity multiplier
};

// ============================================================================
// Light query functions
// ============================================================================

/// Collect the primary directional light from the scene.
/// Finds the first active DirectionalLight node, extracts direction from
/// the world transform, and reads color/intensity from the ComponentStore.
///
/// If no DirectionalLight exists, returns a default (white, from above).
LightState light_collect_primary(const Scene* scene);

} // namespace uz
