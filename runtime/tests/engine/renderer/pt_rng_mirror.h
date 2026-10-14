/// Test-only CPU translation of path_tracer_ps.hlsl's Wang RNG and sphere sampler.
/// Unsigned integer operations are bit-exact; CPU transcendental results are
/// checked with tolerances rather than assumed bit-identical to GPU intrinsics.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace uz::pt_mirror {

struct Vec3 {
    float x;
    float y;
    float z;
};

inline std::uint32_t WangHash(std::uint32_t& seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

inline float Rand01(std::uint32_t& state) {
    return static_cast<float>(WangHash(state)) / 4294967296.0f;
}

inline Vec3 RandUnitVector(std::uint32_t& state) {
    const float z = Rand01(state) * 2.0f - 1.0f;
    const float a = Rand01(state) * 6.28318530718f;
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return {r * std::cos(a), r * std::sin(a), z};
}

/// Mirrors PT_TracePath's roulette step; termination contributes zero radiance.
inline bool RussianRoulette(Vec3& throughput, std::uint32_t& state) {
    const float p = std::clamp(
        std::max(throughput.x, std::max(throughput.y, throughput.z)), 0.0f, 1.0f);
    if (Rand01(state) > p) return false;
    const float divisor = std::max(p, 1e-3f);
    throughput.x /= divisor;
    throughput.y /= divisor;
    throughput.z /= divisor;
    return true;
}

} // namespace uz::pt_mirror
