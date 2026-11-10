/// UzEngine VolumetricFogComponent — ECS data for the froxel volumetric fog.
///
/// Describes the participating medium the froxel grid (VolumetricFogPass, 160x90x64) is filled
/// with: an exponential-height extinction field, its single-scattering albedo, the
/// Henyey-Greenstein phase asymmetry and the grid's depth range. Only the first enabled entity
/// with this component is used (singleton semantics, like HeightFogComponent). Rendering is
/// gated by `r.VolumetricFog` (frame_atmosphere_passes.cpp).
///
/// # Medium
/// ```
/// extinction(h)  = density * globalDensity * extinctionScale * exp(-heightFalloff * (h - heightOffset) [m])
/// inscatter      = albedo * extinction * (sun * HG(phaseG) * shadow + local lights + sky ambient) + emissive
/// ```
/// Units: distances in cm (engine units), `heightFalloff` per metre, `density` per cm.
///
/// Without this component the renderer falls back to a HeightFogComponent whose
/// `enableVolumetricFog` is set (the pre-existing way to author the froxel fog).
///
/// @ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering in Frostbite"
/// @ref Wronski 2014 "Volumetric Fog: Unified Compute Shader Based Solution to Atmospheric Scattering"

#pragma once

#include "vector3.h"
#include "../core/dirty_flags.h"

namespace uz {

struct VolumetricFogComponent : public CBDirtyMixin {
    /// Base extinction coefficient at `heightOffset`, per cm. 0.0005 = a 20 m mean free path
    /// (transmittance 0.04 across the 64 m default grid): a clearly foggy atrium.
    float density = 0.0005f;

    /// Global multiplier on `density` (a quick way to scale a whole scene's fog). Default 1.
    float globalDensity = 1.0f;

    /// Single-scattering albedo of the medium (linear RGB). White = no absorption.
    Vec3 albedo = {1.0f, 1.0f, 1.0f};

    /// Henyey-Greenstein asymmetry in [-1, 1]. Positive = forward scattering (light shafts
    /// brighten when looking toward the light). Default 0.5.
    float phaseG = 0.5f;

    /// Exponential height falloff of the extinction, per metre above `heightOffset`.
    /// 0.1 = density e-folds every 10 m. 0 = uniform fog.
    float heightFalloff = 0.1f;

    /// World-space Z (cm) where the extinction is `density`; it decays above, and is clamped
    /// to `density` below.
    float heightOffset = 0.0f;

    /// Multiplier on the extinction only (scattering keeps `density`): > 1 darkens the fog.
    float extinctionScale = 1.0f;

    /// Self-emission of the medium (linear RGB radiance per unit density). Default black.
    Vec3 emissive = {0.0f, 0.0f, 0.0f};

    /// Far edge of the froxel grid (cm). Scene depth beyond it uses the last slice; the
    /// analytic height fog starts there so the two do not overlap. Default 64 m.
    float distance = 6400.0f;

    /// Near edge of the froxel grid (cm). 0 = the camera near plane.
    float startDistance = 0.0f;

    /// Distance (cm) over which the fog fades in from the camera (against the near "white blob").
    float nearFadeInDistance = 300.0f;

    /// Multiplier on the point and spot lights' in-scatter (UE's VolumetricScatteringIntensity, here
    /// per medium): their 1/d^2 radiance is bright on nearby surfaces but integrates over a short
    /// path in the fog, so a glow usually needs several x. Default 1 (physically consistent).
    float localLightScattering = 1.0f;

    /// Scale of the sky-light ambient in-scatter (the sky irradiance SH L0 times the sky light
    /// intensity). 0 = the fog is lit by the sun and the local lights only.
    float ambientIntensity = 1.0f;

    /// Whether this medium is rendered.
    bool enabled = true;
};

} // namespace uz
