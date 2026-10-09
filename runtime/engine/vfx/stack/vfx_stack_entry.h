#pragma once

/// VFX stack entry — one stage's ordered list of module instances.
///
/// Phase 11.1.0. A `VfxStackEntry` represents a single execution slot
/// in the 7-stage pipeline (System Spawn / System Update / Emitter
/// Spawn / Emitter Update / Particle Spawn / Particle Update / Render)
/// and owns the ordered module list that runs in that slot. The order
/// is the **execution order** — modules execute top to bottom per tick.
///
/// # Invariants
///
/// - `modules` ordering reflects execution order.
/// - No duplicate `instanceId`s across modules in the entry (enforced
///   at add time; undefined otherwise).
/// - `instanceId` is never mutated after push (stable reference for
///   undo/redo and editor selection).
///
/// # Thread safety
///
/// Editor-thread-only mutation. The render thread snapshots the list at
/// frame start via `Snapshot()` (returns a shallow copy).
///
/// @ref UE5 `FNiagaraStackModuleEntry`
/// @ref engine/vfx/stack/vfx_module_instance.h

#include "vfx_module_instance.h"
#include "vfx_stage.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Ordered list of module instances for one stack stage.
struct VfxStackEntry {
    /// Which stage this entry belongs to. Redundant with the `VfxStack`
    /// array index but kept here for log messages and standalone use.
    VfxStage stage = VfxStage::ParticleUpdate;

    /// Module instances in execution order.
    std::vector<VfxModuleInstance> modules;

    /// Appends a fresh instance at the end.
    ///
    /// # Returns
    /// The index of the newly inserted module in `modules`.
    size_t Add(VfxModuleInstance instance) {
        modules.push_back(std::move(instance));
        return modules.size() - 1u;
    }

    /// Inserts an instance at `index`. Clamps to `[0, modules.size()]`.
    /// Returns the index where the instance was actually inserted.
    size_t InsertAt(size_t index, VfxModuleInstance instance) {
        const size_t pos = (index > modules.size()) ? modules.size() : index;
        modules.insert(modules.begin() + static_cast<std::ptrdiff_t>(pos),
                       std::move(instance));
        return pos;
    }

    /// Removes the module at `index`. Returns `true` when a module was
    /// removed, `false` on out-of-range (no mutation).
    bool Remove(size_t index) {
        if (index >= modules.size()) return false;
        modules.erase(modules.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    /// Moves the module at `index` one slot towards the beginning of the
    /// list (earlier execution). Returns `true` on success, `false` when
    /// `index` is 0 or out-of-range.
    bool MoveUp(size_t index) {
        if (index == 0u || index >= modules.size()) return false;
        std::swap(modules[index - 1u], modules[index]);
        return true;
    }

    /// Moves the module at `index` one slot towards the end of the list
    /// (later execution). Returns `true` on success, `false` at end or
    /// out-of-range.
    bool MoveDown(size_t index) {
        if (index + 1u >= modules.size()) return false;
        std::swap(modules[index], modules[index + 1u]);
        return true;
    }

    /// Reorders by swapping slots `a` and `b`. Out-of-range indices
    /// are a no-op (returns false).
    bool Swap(size_t a, size_t b) {
        if (a == b) return true;
        if (a >= modules.size() || b >= modules.size()) return false;
        std::swap(modules[a], modules[b]);
        return true;
    }

    /// Returns the index of the first module with matching `instanceId`,
    /// or `SIZE_MAX` when not found.
    [[nodiscard]] size_t Find(const std::string& instanceId) const noexcept {
        for (size_t i = 0; i < modules.size(); ++i) {
            if (modules[i].instanceId == instanceId) return i;
        }
        return static_cast<size_t>(-1);
    }

    /// Returns the number of modules in the entry.
    [[nodiscard]] size_t Size() const noexcept { return modules.size(); }

    /// Returns true when the entry contains no modules.
    [[nodiscard]] bool IsEmpty() const noexcept { return modules.empty(); }

    /// Removes every module instance. Does not alter `stage`.
    void Clear() noexcept { modules.clear(); }
};

} // namespace uz
