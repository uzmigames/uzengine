#pragma once

/// Named GPU simulation stage — Niagara-parity
/// `UNiagaraSimulationStageBase`.
///
/// UzEngine emitters currently run one fixed spawn + update CS pair.
/// Complex simulations — fluid solvers, cellular automata, multi-pass
/// force accumulation — need multiple compute dispatches per emitter,
/// each with its own shader and its own set of inputs/outputs. Niagara
/// expresses this as an ordered list of simulation stages; UzEngine's
/// equivalent lives in this header.
///
/// # Stage model
///
/// Each stage declares:
///   - A name (editor + logging identifier).
///   - A compute-shader path (resolved via the shader pipeline).
///   - An iteration count (relaxation solvers run the same stage many
///     times per frame).
///   - A read set and a write set — lists of SoA channel names the stage
///     samples / outputs. The render graph uses these to schedule
///     barriers automatically.
///
/// # Stage ordering
///
/// Stages execute in declaration order. Dependencies between stages are
/// expressed implicitly via read/write set intersections — the render
/// graph infers UAV barriers when a later stage reads what an earlier
/// stage wrote. Authors do not wire barriers by hand.
///
/// # Canonical JSON
///
/// Emitter `.uzfx` files embed an optional `stages` array:
///
/// ```json
/// "stages": [
///   { "name": "Curl",   "cs": "/engine/vfx/curl_field_cs.hlsl",
///     "iterations": 1,
///     "reads":  ["positions", "velocities"],
///     "writes": ["velocityDelta"] },
///   { "name": "Advect", "cs": "/engine/vfx/advect_cs.hlsl",
///     "iterations": 2,
///     "reads":  ["velocityDelta"],
///     "writes": ["positions"] }
/// ]
/// ```
///
/// @ref UE5 UNiagaraSimulationStageBase
/// @ref UE5 FSimulationStageData

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace uz {

class IVFS;

/// Declaration of a single simulation stage. All fields are authored;
/// runtime resources (compiled PSO, RG resource IDs) live elsewhere.
struct VfxSimulationStage {
    /// Editor / logging identifier. Must be non-empty and unique within
    /// an emitter's stages list.
    std::string name;

    /// Shader path resolved through the shader pipeline (`/engine/vfx/...`).
    std::string csPath;

    /// How many times this stage dispatches per frame. 1 covers most
    /// cases; relaxation solvers (pressure, Jacobi iterations) use 4-16.
    /// Clamped to `>= 1` at load time.
    uint32_t iterationCount = 1u;

    /// SoA channel names this stage reads. Produces SRV bindings at
    /// bind time plus a read dependency for barrier inference.
    std::vector<std::string> readSet;

    /// SoA channel names this stage writes. Produces UAV bindings and
    /// write dependencies for barrier inference.
    std::vector<std::string> writeSet;

    /// True when both sets have at least one entry (common sanity check).
    [[nodiscard]] bool HasReadsOrWrites() const noexcept {
        return !readSet.empty() || !writeSet.empty();
    }
};

/// Ordered list of simulation stages owned by one emitter. Empty list =
/// default behaviour (single implicit Update stage, matching pre-7.6.0).
struct VfxSimulationStageList {
    std::vector<VfxSimulationStage> stages;

    /// Default-behaviour shortcut — when `true` the render graph skips
    /// the stage machinery entirely and runs the classic single Update
    /// dispatch. Derived from the list being empty; exposed as an
    /// accessor so callers don't reach into the vector.
    [[nodiscard]] bool IsImplicitSingleStage() const noexcept {
        return stages.empty();
    }

    /// Resolves the names of stages that depend on `producerStage` — any
    /// later stage whose `readSet` intersects `producerStage.writeSet`.
    /// Returned in declaration order. Used by the render-graph builder
    /// to emit UAV barriers only where needed.
    [[nodiscard]] std::vector<std::string> FindReadersOf(size_t producerIndex) const;
};

/// Load / save `.uzfx`-embedded stage list as a standalone JSON snippet.
/// The emitter-level loader will call these when parsing its `stages`
/// array; standalone helpers are also useful for unit tests + editor UI.
struct VfxSimulationStageAsset {
    [[nodiscard]] static bool LoadFromJson(const std::string& jsonStr,
                                           VfxSimulationStageList& outList);

    [[nodiscard]] static std::string SaveToJson(const VfxSimulationStageList& list);
};

} // namespace uz
