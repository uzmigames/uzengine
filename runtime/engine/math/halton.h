/// Halton Low-Discrepancy Sequence Generator
///
/// Generates quasi-random point sequences for temporal jittering (TAA/TSR),
/// importance sampling, and any application needing well-distributed samples.
///
/// @source UE5 Math/Halton.h — Halton 1964, "Radical-inverse quasi-random point sequence"
/// @source UE5 SceneRendering.cpp:974-976 — TemporalJitterSequenceLength, TemporalJitterIndex
/// @source UE5 SceneVisibility.cpp:5240-5243 — HackAddTemporalAAProjectionJitter

#pragma once

#include <cstdint>

namespace uz {

/// Computes the Halton sequence value for a given index and base.
///
/// The Halton sequence produces a low-discrepancy sequence in [0, 1) by
/// reflecting the base-N representation of `index` about the decimal point.
/// For TAA jitter, use base 2 (X) and base 3 (Y) to get a 2D Halton(2,3)
/// sequence with good coverage.
///
/// @source UE5 Math/Halton.h:9 — constexpr float Halton(int32 Index, int32 Base)
///
/// # Parameters
/// - `index` — Sample index (1-based for meaningful results; 0 always returns 0).
/// - `base`  — Prime number base (2, 3, 5, 7, ...). Must be >= 2.
///
/// # Returns
/// Value in [0, 1).
///
/// # Example
/// ```cpp
/// // TAA 8-sample Halton(2,3) jitter, centered to [-0.5, 0.5]
/// float jitterX = uz::Halton(frameIndex + 1, 2) - 0.5f;
/// float jitterY = uz::Halton(frameIndex + 1, 3) - 0.5f;
/// ```
[[nodiscard]] constexpr float Halton(int32_t index, int32_t base)
{
    /// @source UE5 Math/Halton.h:9-21 — exact algorithm
    float result   = 0.0f;
    float invBase  = 1.0f / static_cast<float>(base);
    float fraction = invBase;
    while (index > 0) {
        result  += static_cast<float>(index % base) * fraction;
        index   /= base;
        fraction *= invBase;
    }
    return result;
}

/// Computes a 2D Halton(2,3) sample centered in [-0.5, 0.5].
///
/// This is the standard TAA/TSR jitter pattern used by UE5.
/// The sequence is 1-indexed (pass frameIndex + 1 for 0-based frame counts).
///
/// @source UE5 SceneRendering.cpp:974 — TemporalJitterSequenceLength
/// @source UE5 SceneVisibility.cpp:5240 — TemporalJitterPixels from Halton
///
/// # Parameters
/// - `index` — 1-based sample index. For cyclic jitter, use `(frameIndex % sequenceLength) + 1`.
/// - `outX`  — Output X jitter in [-0.5, 0.5].
/// - `outY`  — Output Y jitter in [-0.5, 0.5].
constexpr void Halton23(int32_t index, float& outX, float& outY)
{
    outX = Halton(index, 2) - 0.5f;
    outY = Halton(index, 3) - 0.5f;
}

/// Default TAA/TSR jitter sequence length.
///
/// @source UE5 SceneRendering.cpp:1717 — TemporalJitterSequenceLength
/// UE5 uses 8 for TAA, up to 32 for TSR high quality. 8 provides good
/// coverage for standard TAA with fast convergence.
inline constexpr int32_t kDefaultJitterSequenceLength = 8;

} // namespace uz
