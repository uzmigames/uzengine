/// UzEngine HeightFogComponent — ECS data for exponential height fog.
///
/// Stores fog parameters for a distance-based exponential height fog pass.
/// Attached to a scene entity to enable fog rendering. Only the first entity
/// with this component is used (singleton semantics via first-found gathering).
///
/// # Fog Model
/// Exponential height fog with distance attenuation:
/// ```
/// density(h) = fogDensity * exp(-fogHeightFalloff * (h - baseHeight))
/// fog(d, h)  = 1 - exp(-integral of density along view ray)
/// opacity    = min(fog, maxOpacity)
/// ```
///
/// # Usage
/// ```cpp
/// auto entity = world.CreateEntity();
/// auto& fog = world.AddComponent<HeightFogComponent>(entity);
/// fog.color = {0.7f, 0.75f, 0.85f};
/// fog.density = 0.02f;
/// ```

#pragma once

#include "vector3.h"

namespace uz {

/// Exponential height fog parameters for the fog compositing pass.
///
/// The fog pass reads scene depth, reconstructs world position, computes
/// fog density along the view ray, and blends the fog color over the scene.
struct HeightFogComponent {
    /// Fog color in linear space. Blended over the scene where fog is dense.
    Vec3 color = {0.7f, 0.75f, 0.85f};

    /// Base fog density factor. Higher = thicker fog. Typical range: 0.001 to 0.1.
    float density = 0.01f;

    /// Height falloff coefficient. Controls how quickly fog thins with altitude.
    /// Higher = fog concentrated closer to baseHeight. Typical: 0.2 to 2.0.
    float heightFalloff = 0.5f;

    /// World-space height at which fog density is at its maximum.
    float baseHeight = 0.f;

    /// Maximum fog opacity (0 to 1). Prevents fog from fully obscuring distant objects.
    float maxOpacity = 0.9f;

    /// Minimum distance from camera before fog begins. Objects closer are fog-free.
    float startDistance = 10.f;

    /// Whether the fog pass is enabled.
    bool enabled = true;
};

} // namespace uz
