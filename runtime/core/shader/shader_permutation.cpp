/// UzEngine Shader Permutation System — Implementation
///
/// Parses `#pragma uz_permutation` directives embedded in HLSL source and
/// expands them into all valid compile-time variants.
///
/// Parsing strategy:
///   `ParsePermutations` scans the source line-by-line with `std::istringstream`,
///   looking for the two-token prefix "#pragma uz_permutation".  The third token
///   becomes the define name; any remaining tokens on the same line are the
///   enumeration values.  A directive with no values is treated as a boolean
///   and automatically receives values {"0", "1"}.
///
/// Cartesian product:
///   `ExpandPermutations` builds variants iteratively — it multiplies the
///   running variant list by each new permutation axis, producing N×M variants
///   per step.  Defines within each variant are sorted by name so the resulting
///   string key is deterministic regardless of directive order in source.
///
/// Explosion guard:
///   The total variant count is checked against `kMaxPermutationVariants` before
///   any allocation.  If exceeded, `ExpandPermutations` returns an error result
///   rather than allocating potentially millions of entries.

#include "shader_permutation.h"

#include <algorithm>
#include <sstream>

#include "log.h"

UZ_DEFINE_LOG_CATEGORY(Shader, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ParsePermutations — scan source for #pragma uz_permutation directives
// ─────────────────────────────────────────────────────────────────────────────

std::vector<ShaderPermutation> ParsePermutations(const std::string& source) {
    std::vector<ShaderPermutation> result;
    std::istringstream stream(source);
    std::string line;

    while (std::getline(stream, line)) {
        std::istringstream lineStream(line);
        std::string token;

        // Expect: #pragma uz_permutation NAME [VAL1 VAL2 ...]
        if (!(lineStream >> token) || token != "#pragma") {
            continue;
        }
        if (!(lineStream >> token) || token != "uz_permutation") {
            continue;
        }

        std::string name;
        if (!(lineStream >> name)) {
            continue;
        }

        ShaderPermutation perm;
        perm.name = std::move(name);

        // Collect remaining tokens as enum values
        std::string value;
        while (lineStream >> value) {
            perm.values.push_back(std::move(value));
        }

        // No values means bool permutation
        if (perm.values.empty()) {
            perm.values = {"0", "1"};
        }

        UZ_LOG(Shader, Debug, "Parsed permutation '{}' with {} values", perm.name, perm.values.size());
        result.push_back(std::move(perm));
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// VariantCount — product of all axis sizes
// ─────────────────────────────────────────────────────────────────────────────

size_t VariantCount(const std::vector<ShaderPermutation>& perms) {
    size_t count = 1;
    for (const auto& perm : perms) {
        count *= perm.values.size();
    }
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
// ExpandPermutations — Cartesian product with explosion guard
// ─────────────────────────────────────────────────────────────────────────────

PermutationExpandResult ExpandPermutations(const std::vector<ShaderPermutation>& perms) {
    PermutationExpandResult result;

    const size_t totalCount = VariantCount(perms);
    if (totalCount > kMaxPermutationVariants) {
        result.error = "Permutation explosion: " + std::to_string(totalCount)
                     + " variants exceeds maximum of " + std::to_string(kMaxPermutationVariants);
        UZ_LOG(Shader, Error, "{}", result.error);
        return result;
    }

    if (perms.empty()) {
        // One empty variant — the base shader with no defines
        result.variants.emplace_back();
        return result;
    }

    // Build Cartesian product iteratively
    result.variants.emplace_back();

    for (const auto& perm : perms) {
        std::vector<std::vector<std::pair<std::string, std::string>>> expanded;
        expanded.reserve(result.variants.size() * perm.values.size());

        for (const auto& existing : result.variants) {
            for (const auto& value : perm.values) {
                auto variant = existing;
                variant.emplace_back(perm.name, value);
                expanded.push_back(std::move(variant));
            }
        }

        result.variants = std::move(expanded);
    }

    // Sort each variant's defines by name for deterministic hashing
    for (auto& variant : result.variants) {
        std::sort(variant.begin(), variant.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// FormatDefines — deterministic string representation for cache keys
// ─────────────────────────────────────────────────────────────────────────────

std::string FormatDefines(const std::vector<std::pair<std::string, std::string>>& defines) {
    std::string result;
    for (size_t i = 0; i < defines.size(); ++i) {
        if (i > 0) {
            result += ';';
        }
        result += defines[i].first;
        result += '=';
        result += defines[i].second;
    }
    return result;
}

} // namespace uz
