#pragma once

// StringId — compile-time and runtime interned string hash.
//
// Uses FNV-1a 32-bit for hashing. The `_sid` user-defined literal produces a
// consteval StringId, allowing compile-time hashing with zero runtime cost.
//
// Runtime interning via string_id(const char*) also registers the original
// string in a global table so that string_id_lookup() can return it for debug
// output. The interning table is thread-safe (guarded by a mutex).
//
// Collision probability: FNV-1a 32-bit has a birthday-problem collision at
// ~77k strings. For engines with larger string namespaces, consider upgrading
// to 64-bit FNV-1a and a uint64_t hash field.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace uz {

// ============================================================================
// StringId
// ============================================================================

struct StringId {
    uint32_t hash;

    constexpr StringId() noexcept : hash(0) {}
    constexpr explicit StringId(uint32_t h) noexcept : hash(h) {}

    [[nodiscard]] constexpr bool operator==(StringId o) const noexcept { return hash == o.hash; }
    [[nodiscard]] constexpr bool operator!=(StringId o) const noexcept { return hash != o.hash; }
    [[nodiscard]] constexpr bool operator<(StringId o)  const noexcept { return hash < o.hash; }

    /// True if this is not the empty/zero StringId.
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return hash != 0; }
};

// ============================================================================
// Hash support for std::unordered_map / std::unordered_set
// ============================================================================

struct StringIdHash {
    [[nodiscard]] size_t operator()(StringId id) const noexcept {
        return static_cast<size_t>(id.hash);
    }
};

// ============================================================================
// FNV-1a 32-bit — constexpr implementation
// ============================================================================

namespace detail {

inline constexpr uint32_t kFnv1aOffset = 2166136261u;
inline constexpr uint32_t kFnv1aPrime  = 16777619u;

constexpr uint32_t fnv1a_32(const char* str, size_t len) noexcept {
    uint32_t hash = kFnv1aOffset;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint8_t>(str[i]);
        hash *= kFnv1aPrime;
    }
    return hash;
}

} // namespace detail

// ============================================================================
// Compile-time literal: "my_string"_sid
// ============================================================================

consteval StringId operator""_sid(const char* str, size_t len) {
    return StringId{detail::fnv1a_32(str, len)};
}

// ============================================================================
// Runtime interning — registers the string for debug lookup
// ============================================================================

/// Compute hash and register the string for later debug lookup.
StringId string_id(const char* str);

/// Compute hash and register the string for later debug lookup.
StringId string_id(std::string_view str);

/// Look up the original string that was registered for this hash.
/// Returns "<unknown>" if the string was never registered (e.g. consteval-only).
const char* string_id_lookup(StringId id);

} // namespace uz
