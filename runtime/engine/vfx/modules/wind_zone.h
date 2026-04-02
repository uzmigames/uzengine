/// VFX Wind Zone — CPU-side wind force field descriptor
///
/// A WindZone defines a local or global wind force field that affects particles
/// during the update compute pass. Multiple zones can be active simultaneously;
/// their contributions are accumulated in the GPU shader.
///
/// Zones with `radius == 0` are treated as global (infinite extent) and affect
/// all particles regardless of position. Zones with `radius > 0` apply a linear
/// distance falloff, reaching zero force at the boundary.
///
/// Gust animation uses a sinusoidal wave keyed to simulation time:
///   gust = saturate(sin(gustFrequency * 2*pi * time)) * gustAmplitude
///
/// @ref Stam 1999 "Stable Fluids" — wind field accumulation model
/// @ref Niagara Wind Force module (GDC 2019) — multi-zone + gust design

#pragma once

#include <cstdint>

namespace uz {

/// Maximum number of simultaneously active wind zones uploaded to the GPU.
/// Matches `kMaxWindZones` in vfx_wind.hlsli.
static constexpr uint32_t kMaxWindZones = 8u;

/// CPU-side wind zone descriptor. Uploaded to a GPU StructuredBuffer each frame
/// for consumption by particle_update_cs.hlsl via `AccumulateWindForce()`.
///
/// Layout: 48 bytes (3 float4) to satisfy GPU structured-buffer alignment.
/// Must match the HLSL `WindZone` struct in vfx_wind.hlsli exactly.
///
/// @ref Stam 1999 "Stable Fluids" — wind field parameters
struct WindZone {
    /// World-space center of influence.
    /// For global zones (radius == 0), this field is ignored.
    float position[3]   = {0.0f, 0.0f, 0.0f};

    /// Influence radius in world units.
    /// 0 = global zone (infinite extent, affects all particles).
    /// > 0 = local zone with linear falloff to zero at the boundary.
    float radius        = 20.0f;

    /// Normalized wind direction (world space).
    /// The GPU shader does NOT re-normalize — the caller must supply a unit vector.
    float direction[3]  = {1.0f, 0.0f, 0.0f};

    /// Base wind speed in m/s.
    /// This is the steady-state force applied at full falloff weight.
    float strength      = 3.0f;

    /// Gust frequency in oscillations per second (Hz).
    /// Controls how rapidly the gust sinusoid cycles.
    float gustFrequency = 0.5f;

    /// Peak gust speed added to `strength` at the wave crest (m/s).
    /// Gusts are saturated to positive-only so they augment, never subtract.
    float gustAmplitude = 1.5f;

    /// Alignment padding to reach 48 bytes (3 x float4).
    float _pad[2] = {0.0f, 0.0f};
};
static_assert(sizeof(WindZone) == 48u, "WindZone must be 48 bytes (3 x float4)");

} // namespace uz
