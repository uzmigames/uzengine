/// VFX Wind Zone — Accumulate Wind Forces from Multiple Zones
///
/// Defines the WindZone struct and an accumulation function that sums force
/// contributions from all active wind zones. Zones with radius=0 are treated as
/// global (infinite) zones affecting all particles. Zones with radius>0 use a
/// linear distance falloff.
///
/// Gust animation uses a sin() wave per zone, matching the Niagara Wind Force
/// module's approach: gusts add on top of the base strength (saturated to avoid
/// negative gust contribution).
///
/// @ref Stam 1999 "Stable Fluids" — wind field accumulation model
/// @ref Niagara Wind Force module (GDC 2019) — multi-zone accumulation + gust
/// @constants windZone: strength (m/s), gustAmplitude (m/s), gustFrequency (Hz)
/// @deviations Linear radial falloff instead of smooth step for GPU efficiency.
///             Gusts clamped to positive-only via saturate() to avoid subtracting
///             from base strength (matches Niagara behavior, not Stam's pure fluid).

#ifndef VFX_WIND_HLSLI
#define VFX_WIND_HLSLI

/// GPU mirror of the CPU WindZone struct (48 bytes = 3 float4).
/// Must match the C++ struct in runtime/engine/vfx/modules/wind_zone.h exactly.
struct WindZone {
    float3 position;        ///< World-space center of influence. 0 = global.
    float  radius;          ///< Influence radius (world units). 0 = global (infinite).
    float3 direction;       ///< Normalized wind direction.
    float  strength;        ///< Base wind speed (m/s).
    float  gustFrequency;   ///< Gusts per second (Hz).
    float  gustAmplitude;   ///< Peak gust speed added to base strength (m/s).
    float2 _pad;            ///< Padding to 48 bytes.
};

/// Accumulates wind force on a particle from all active wind zones.
///
/// For each zone:
///   - Global zones (radius <= 0): falloff = 1.0 (full strength everywhere).
///   - Local zones (radius > 0): skip if dist > radius; linear falloff otherwise.
///   - Gust: sin(time * freq * 2*pi) * amplitude, clamped to [0, amplitude] so
///     gusts only add to the base strength, never subtract.
///
/// # Parameters
/// - `particlePos` : Current particle world-space position.
/// - `time`        : Elapsed simulation time in seconds (for gust animation).
/// - `zones`       : Structured buffer of WindZone descriptors.
/// - `count`       : Number of active wind zones [0..kMaxWindZones].
///
/// # Returns
/// Accumulated wind force vector (m/s^2 equivalent when multiplied by dt to
/// get velocity change per step). Caller adds `result * dt` to particle velocity.
///
/// @ref Stam 1999 "Stable Fluids" — wind field accumulation
/// @ref Stam 1999 — gust = sin(freq * 2pi * t) * amplitude
float3 AccumulateWindForce(float3 particlePos, float time,
                            StructuredBuffer<WindZone> zones, uint count)
{
    float3 totalForce = float3(0.0f, 0.0f, 0.0f);

    for (uint i = 0u; i < count; ++i) {
        WindZone zone = zones[i];

        float falloff = 1.0f;

        if (zone.radius > 0.0f) {
            // Local zone: compute linear distance falloff.
            float dist = length(particlePos - zone.position);

            // Skip this zone if the particle is outside its influence radius.
            if (dist > zone.radius) {
                continue;
            }

            // Linear falloff: full force at center, zero at radius boundary.
            falloff = 1.0f - saturate(dist / zone.radius);
        }
        // else: global zone (radius <= 0), falloff stays 1.0.

        // Gust: a sinusoidal oscillation added on top of the base wind strength.
        // saturate() clamps the result to [0, 1] so gusts only augment the base.
        // @ref Stam 1999 — gust = sin(freq * 2pi * t) * amplitude
        float gustPhase = zone.gustFrequency * 6.28318530718f * time;  // 2*pi
        float gust = saturate(sin(gustPhase)) * zone.gustAmplitude;

        // Accumulate: direction * (base + gust) * spatial falloff
        totalForce += zone.direction * (zone.strength + gust) * falloff;
    }

    return totalForce;
}

#endif // VFX_WIND_HLSLI
