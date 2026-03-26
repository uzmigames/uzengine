#pragma once

/// VFX Graph Validator -- structural correctness checks for loaded VFX graphs.
///
/// Validates a VFXGraph after loading to catch configuration errors early
/// (before the graph is instantiated on an emitter). Reports all issues in
/// a single pass rather than failing on the first error.
///
/// # Thread Safety
/// Stateless -- safe to call from any thread with a const VFXGraph reference.

#include <string>
#include <vector>

namespace uz {

class VFXGraph;

// ─────────────────────────────────────────────────────────────────────────────
// VFXValidationResult -- collected errors and warnings
// ─────────────────────────────────────────────────────────────────────────────

/// Result of validating a VFXGraph.
///
/// Contains separate lists of errors (graph is unusable) and warnings
/// (graph will work but may behave unexpectedly). `valid` is true only
/// when `errors` is empty.
struct VFXValidationResult {
    bool valid = true;                   ///< True if no errors were found.
    std::vector<std::string> errors;     ///< Critical issues (graph is broken).
    std::vector<std::string> warnings;   ///< Non-critical issues (graph will work).
};

// ─────────────────────────────────────────────────────────────────────────────
// ValidateVFXGraph -- public validation entry point
// ─────────────────────────────────────────────────────────────────────────────

/// Validates a loaded VFXGraph for structural correctness.
///
/// Performs the following checks:
/// - At least one module exists.
/// - No duplicate parameter names.
/// - No duplicate event names.
/// - Parameters have valid types (Float, Vec3, Vec4).
/// - Events have positive emission counts (> 0).
/// - Events have positive velocity scales (> 0).
/// - Emitter max_particles > 0.
/// - Emitter lifespan_default > 0.
///
/// # Parameters
/// - `graph`: The VFXGraph to validate. Must be a fully loaded graph.
///
/// # Returns
/// A VFXValidationResult with `valid == true` if all checks pass.
/// Otherwise, `errors` and/or `warnings` contain human-readable messages.
///
/// # Side Effects
/// Logs each error via `UZ_LOG(VFX, Error, ...)`.
[[nodiscard]] VFXValidationResult ValidateVFXGraph(const VFXGraph& graph);

} // namespace uz
