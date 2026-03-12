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

#include <cmath>
#include <cstdint>

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

    /// Generates the next 32-bit pseudo-random number.
    ///
    /// Advances internal state using PCG XSH-RR permutation:
    /// multiply-increment LCG followed by xorshift and rotation.
    ///
    /// # Returns
    /// Uniformly distributed `uint32_t` in range `[0, 2^32)`.
    ///
    /// # Examples
    /// ```cpp
    /// PCGRandom rng(42);
    /// uint32_t value = rng.Next();  // deterministic given seed
    /// ```
    uint32_t Next() {
        uint64_t oldstate = m_state;
        m_state = oldstate * 6364136223846793005ULL + m_inc;
        uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31));
    }

    /// Generates a random float in the half-open range `[0.0, 1.0)`.
    ///
    /// # Returns
    /// Uniformly distributed `float` where `0.0 <= result < 1.0`.
    ///
    /// # Precision
    /// Uses 32-bit integer divided by 2^32. Effective precision is ~7 decimal digits.
    float Float() { return static_cast<float>(Next()) / 4294967296.0f; }

    /// Generates a random double in the half-open range `[0.0, 1.0)`.
    ///
    /// # Returns
    /// Uniformly distributed `double` where `0.0 <= result < 1.0`.
    ///
    /// # Precision
    /// Uses 32-bit integer divided by 2^32. Only 32 bits of randomness despite
    /// double precision — use two calls if full 52-bit mantissa is needed.
    double Double() { return static_cast<double>(Next()) / 4294967296.0; }

    /// Generates a random float in the half-open range `[min, max)`.
    ///
    /// # Arguments
    /// * `min` - Lower bound (inclusive)
    /// * `max` - Upper bound (exclusive)
    ///
    /// # Returns
    /// Uniformly distributed `float` where `min <= result < max`.
    ///
    /// # Examples
    /// ```cpp
    /// float speed = rng.Range(5.0f, 15.0f);  // random speed between 5 and 15
    /// ```
    float Range(float min, float max) { return min + Float() * (max - min); }

    /// Generates a random integer in the closed range `[min, max]`.
    ///
    /// # Arguments
    /// * `min` - Lower bound (inclusive)
    /// * `max` - Upper bound (inclusive)
    ///
    /// # Returns
    /// Uniformly distributed `int32_t` where `min <= result <= max`.
    /// Returns `min` if `min >= max`.
    ///
    /// # Examples
    /// ```cpp
    /// int damage = rng.Int(10, 25);  // random damage between 10 and 25
    /// ```
    int32_t Int(int32_t min, int32_t max) {
        if (min >= max)
            return min;
        uint32_t range = static_cast<uint32_t>(max - min + 1);
        return min + static_cast<int32_t>(Next() % range);
    }

    /// Generates a random unsigned integer in the half-open range `[0, max)`.
    ///
    /// # Arguments
    /// * `max` - Upper bound (exclusive)
    ///
    /// # Returns
    /// Uniformly distributed `uint32_t` where `0 <= result < max`.
    /// Returns `0` if `max == 0`.
    ///
    /// # Examples
    /// ```cpp
    /// uint32_t index = rng.UInt(array.size());  // random valid index
    /// ```
    uint32_t UInt(uint32_t max) {
        if (max == 0)
            return 0;
        return Next() % max;
    }

    /// Generates a random boolean (coin flip).
    ///
    /// # Returns
    /// `true` or `false` with approximately equal probability.
    bool Bool() { return (Next() & 1) != 0; }

    /// Generates a random unit vector uniformly distributed on the sphere surface.
    ///
    /// Uses the Marsaglia rejection method: generates pairs in `[-1,1]^2`,
    /// rejects those outside the unit disk, then maps to sphere coordinates.
    ///
    /// # Arguments
    /// * `x`, `y`, `z` - Output components of the unit vector (`x^2 + y^2 + z^2 == 1`)
    ///
    /// # Examples
    /// ```cpp
    /// float x, y, z;
    /// rng.UnitSphere(x, y, z);
    /// // Use as random direction for particle emission
    /// ```
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

    /// Generates a random point uniformly distributed inside the unit disk.
    ///
    /// Uses rejection sampling: generates pairs in `[-1,1]^2` and discards
    /// those outside the unit circle.
    ///
    /// # Arguments
    /// * `x`, `y` - Output coordinates where `x^2 + y^2 < 1`
    ///
    /// # Examples
    /// ```cpp
    /// float x, y;
    /// rng.UnitDisk(x, y);
    /// // Use for depth-of-field lens sampling
    /// ```
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
