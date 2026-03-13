#pragma once

/// UzEngine FNV-1a Hash Utility
///
/// Provides a lightweight, constexpr-friendly FNV-1a hash for use in hash maps,
/// content-addressed caches, and descriptor key hashing throughout the engine.
///
/// FNV-1a is chosen over xxHash for small, fixed-layout struct keys where the
/// simplicity of a header-only constexpr implementation outweighs raw throughput.
/// For large byte buffers (shader source, file content), prefer xxHash instead.
///
/// # Thread Safety
/// All functions are pure — no shared state, safe to call from any thread.

#include <cstddef>
#include <cstdint>

namespace uz {

/// FNV-1a 64-bit offset basis.
static constexpr uint64_t kFnv64OffsetBasis = 14695981039346656037ULL;

/// FNV-1a 64-bit prime.
static constexpr uint64_t kFnv64Prime = 1099511628211ULL;

/// Mixes a single 64-bit value into an FNV-1a hash state.
///
/// # Arguments
/// * `hash` — Current hash state (initialize with `kFnv64OffsetBasis`).
/// * `value` — Value to mix in.
///
/// # Returns
/// Updated hash state.
[[nodiscard]] constexpr uint64_t FnvMix(uint64_t hash, uint64_t value) noexcept {
    hash ^= value;
    hash *= kFnv64Prime;
    return hash;
}

/// Hashes a null-terminated C string using FNV-1a (byte-by-byte).
///
/// # Arguments
/// * `hash` — Current hash state.
/// * `str` — Null-terminated string to hash. Must not be null.
///
/// # Returns
/// Updated hash state after hashing all bytes of the string.
[[nodiscard]] inline uint64_t FnvMixString(uint64_t hash, const char* str) noexcept {
    for (const char* p = str; *p; ++p) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(*p));
        hash *= kFnv64Prime;
    }
    return hash;
}

} // namespace uz
