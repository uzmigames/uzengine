/// UzEngine Shader Permutation System
///
/// Parses `#pragma uz_permutation` directives from shader source and generates
/// the Cartesian product of all permutation axes. Each variant produces a unique
/// set of compile defines that feeds into the content-hash for cache keying.
///
/// # Permutation explosion guard
/// Maximum 256 variants per shader. Exceeding this limit returns an error
/// instead of silently generating thousands of variants.
///
/// # Usage
/// ```hlsl
/// #pragma uz_permutation HAS_NORMAL_MAP              // bool: 2 variants
/// #pragma uz_permutation SHADOW_QUALITY LOW MEDIUM HIGH  // enum: 3 variants
/// // Total: 2 x 3 = 6 variants
/// ```

#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace uz {

/// Maximum number of permutation variants allowed per shader.
///
/// Prevents combinatorial explosion from `#pragma uz_permutation` directives.
/// A shader declaring axes whose Cartesian product exceeds this limit will
/// return an error from `ExpandPermutations()` instead of silently generating
/// thousands of variants.
static constexpr size_t kMaxPermutationVariants = 256;

/// A single permutation axis parsed from a `#pragma uz_permutation` directive.
///
/// # Bool vs Enum
/// - Bool permutation (name only): `#pragma uz_permutation HAS_NORMAL_MAP`
///   produces `values = {"0", "1"}`.
/// - Enum permutation (name + values): `#pragma uz_permutation SHADOW_QUALITY LOW MEDIUM HIGH`
///   produces `values = {"LOW", "MEDIUM", "HIGH"}`.
struct ShaderPermutation {
    std::string name;                ///< Define name (e.g., "HAS_NORMAL_MAP")
    std::vector<std::string> values; ///< Possible values. Bool: {"0","1"}. Enum: {"LOW","MEDIUM","HIGH"}
};

/// Result of expanding permutations into concrete variant define sets.
///
/// On success, `variants` contains the full Cartesian product of all axes,
/// each variant represented as a sorted list of (name, value) pairs for
/// deterministic hashing. On failure, `error` describes what went wrong.
struct PermutationExpandResult {
    /// Each variant is a sorted list of (define_name, define_value) pairs.
    /// The outer vector has one entry per variant; the inner vector has one
    /// entry per permutation axis.
    std::vector<std::vector<std::pair<std::string, std::string>>> variants;

    /// Non-empty if expansion failed (e.g., exceeded `kMaxPermutationVariants`).
    std::string error;

    /// Returns true if expansion succeeded (no error).
    [[nodiscard]] bool Ok() const { return error.empty(); }
};

/// Parses all `#pragma uz_permutation` directives from shader source text.
///
/// # Arguments
/// * `source` — Full HLSL/GLSL source text.
///
/// # Returns
/// Permutations in source order. Empty vector if none found.
[[nodiscard]] std::vector<ShaderPermutation> ParsePermutations(const std::string& source);

/// Computes the total number of variants from a set of permutation axes.
///
/// Multiplies the value count of each axis together (Cartesian product size).
/// Returns 1 for an empty permutation list (the "no permutations" base variant).
///
/// # Arguments
/// * `perms` — Permutation axes parsed from `#pragma uz_permutation` directives.
///
/// # Returns
/// Product of all axis value counts. Always >= 1 (empty list yields 1).
[[nodiscard]] size_t VariantCount(const std::vector<ShaderPermutation>& perms);

/// Expands permutation axes into the full Cartesian product of define sets.
///
/// Each variant is a sorted vector of (name, value) pairs for deterministic
/// hashing. The sort is lexicographic by define name, ensuring identical
/// define sets always produce the same hash regardless of parse order.
///
/// # Arguments
/// * `perms` — Permutation axes to expand. May be empty (yields one empty variant).
///
/// # Returns
/// `PermutationExpandResult` with `Ok() == true` and populated `variants` on
/// success, or `Ok() == false` with a descriptive `error` string if the total
/// variant count exceeds `kMaxPermutationVariants`.
[[nodiscard]] PermutationExpandResult ExpandPermutations(const std::vector<ShaderPermutation>& perms);

/// Formats a variant's define pairs into a deterministic semicolon-separated string.
///
/// Example output: `"HAS_NORMAL_MAP=0;SHADOW_QUALITY=LOW"`.
/// The input pairs are assumed to already be sorted by name (as produced by
/// `ExpandPermutations()`).
///
/// # Arguments
/// * `defines` — Sorted list of (define_name, define_value) pairs for one variant.
///
/// # Returns
/// Semicolon-separated `"NAME=VALUE;..."` string. Empty string for an empty input.
[[nodiscard]] std::string FormatDefines(const std::vector<std::pair<std::string, std::string>>& defines);

} // namespace uz
