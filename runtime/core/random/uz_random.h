#pragma once

/// UzEngine Pseudo-Random Number Generator
///
/// Implements PCG (Permuted Congruential Generator) — XSH-RR variant:
/// - **32-bit output**: uint32_t per call (fast to use)
/// - **64-bit state**: Small memory footprint (2x uint64_t)
/// - **Statistically excellent**: Passes BigCrush test suite (unlike LCG/MT19937)
/// - **Seedable**: Deterministic for reproducible simulations
/// - **Fast**: Single multiply + rotate per number (~CPU cycle per output)
///
/// # Reference
/// "PCG: A Family of Simple Fast Space-Efficient Statistically Good
///  Algorithms for Random Number Generation" by M.E. O'Neill
/// https://www.pcg-random.org/
///
/// # Use Cases
/// - Procedural generation (deterministic if seeded)
/// - Shuffle/sampling (better distribution than rand())
/// - Physics randomization (bullets, particle variation)
/// - NOT for cryptography (use /dev/urandom or libsodium)
///
/// # Example
/// ```cpp
/// PCGRandom rng(seed_from_config);
/// for (int i = 0; i < 100; ++i) {
///     uint32_t random = rng.Next();
///     float  normalize = rng.Float();  // [0, 1)
/// }
/// ```

#include <cstdint>
#include <cmath>

namespace uz {

/// PCG random number generator.
///
/// # Thread Safety
/// NOT thread-safe: each thread needs its own instance.
/// Use thread-local storage for parallel loops.
class PCGRandom {
public:
    /// Constructs seeded RNG from a single seed value.
    ///
    /// # Arguments
    /// * `seed` - 64-bit seed (default uses hardcoded constant)
    ///
    /// # Behavior
    /// Initializes m_state and m_inc, then advances state twice (warmup).
    /// This ensures even poor seeds produce good output immediately.
    explicit PCGRandom(uint64_t seed = 0x853C49E6748FEA9BULL) {
        m_state = 0;
        m_inc = (seed << 1u) | 1u;
        Next();
        m_state += seed;
        Next();
    }

    /// Constructs seeded RNG with separate seed and stream parameters.
    ///
    /// # Arguments
    /// * `seed` - Initial state seed
    /// * `seq` - Stream identifier (allows multiple independent streams from same seed)
    ///
    /// # Use Case
    /// Parallel loops: each thread uses same seed but different seq.
    /// Guarantees independent random streams across threads.
    PCGRandom(uint64_t seed, uint64_t seq) {
        m_state = 0;
        m_inc = (seq << 1u) | 1u;
        Next();
        m_state += seed;
        Next();
    }

    /// Re-seeds the RNG with a new seed value.
    ///
    /// # Arguments
    /// * `seed` - New 64-bit seed
    void Seed(uint64_t seed) {
        m_state = 0;
        m_inc = (seed << 1u) | 1u;
        Next();
        m_state += seed;
        Next();
    }

    uint32_t Next() {
        uint64_t oldstate = m_state;
        m_state = oldstate * 6364136223846793005ULL + m_inc;
        uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31));
    }

    // [0, 1)
    float Float() {
        return static_cast<float>(Next()) / 4294967296.0f;
    }

    // [0, 1)
    double Double() {
        return static_cast<double>(Next()) / 4294967296.0;
    }

    // [min, max)
    float Range(float min, float max) {
        return min + Float() * (max - min);
    }

    // [min, max] inclusive
    int32_t Int(int32_t min, int32_t max) {
        if (min >= max) return min;
        uint32_t range = static_cast<uint32_t>(max - min + 1);
        return min + static_cast<int32_t>(Next() % range);
    }

    // [0, max) exclusive
    uint32_t UInt(uint32_t max) {
        if (max == 0) return 0;
        return Next() % max;
    }

    bool Bool() { return (Next() & 1) != 0; }

    // Random unit vector on sphere surface (Marsaglia method)
    void UnitSphere(float& x, float& y, float& z) {
        float u, v, s;
        do {
            u = Range(-1.0f, 1.0f);
            v = Range(-1.0f, 1.0f);
            s = u * u + v * v;
        } while (s >= 1.0f || s == 0.0f);
        float factor = 2.0f * std::sqrt(1.0f - s);
        x = u * factor;
        y = v * factor;
        z = 1.0f - 2.0f * s;
    }

    // Random point inside unit disk
    void UnitDisk(float& x, float& y) {
        do {
            x = Range(-1.0f, 1.0f);
            y = Range(-1.0f, 1.0f);
        } while (x * x + y * y >= 1.0f);
    }

private:
    uint64_t m_state;
    uint64_t m_inc;
};

} // namespace uz
