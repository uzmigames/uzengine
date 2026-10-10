#pragma once

/// VFX Stack — the 7-stage execution pipeline owned by each emitter.
///
/// Phase 11.1.0. A `VfxStack` holds a `VfxStackEntry` for each of the
/// 7 Niagara-parity stages. `ParticleEmitter` embeds one `stack`
/// member; the editor writes to it via the 11.2 stack panel widget,
/// the module compositor reads from it to stitch HLSL fragments.
///
/// # Invariants
/// - Exactly 7 entries, indexed by `static_cast<uint8_t>(VfxStage)`.
/// - Each entry's `stage` field matches its array slot.
/// - An empty stack (every entry empty) is a valid legacy asset and
///   means "use the flat inline-config path" — identical to pre-11.1
///   behaviour.
///
/// @ref UE5 `UNiagaraStack`
/// @ref engine/vfx/stack/vfx_stack_entry.h

#include "vfx_stack_entry.h"
#include "vfx_stage.h"

#include <array>
#include <cstdint>

namespace uz {

/// Ordered 7-stage module pipeline. Indexed by `VfxStage`.
class VfxStack {
public:
    /// Constructs an empty stack — every entry is empty but has its
    /// `stage` field set to match its slot.
    VfxStack() noexcept {
        for (uint8_t i = 0; i < kVfxStageCount; ++i) {
            m_entries[i].stage = static_cast<VfxStage>(i);
        }
    }

    /// Returns the entry for a stage. Slot index is
    /// `static_cast<uint8_t>(stage)`.
    [[nodiscard]] VfxStackEntry& GetEntry(VfxStage stage) noexcept {
        return m_entries[static_cast<uint8_t>(stage)];
    }

    /// Const variant for read-only access (composer, serializer).
    [[nodiscard]] const VfxStackEntry& GetEntry(VfxStage stage) const noexcept {
        return m_entries[static_cast<uint8_t>(stage)];
    }

    /// Returns the raw entry array. Index with a numeric `VfxStage`.
    [[nodiscard]] std::array<VfxStackEntry, kVfxStageCount>& Entries() noexcept {
        return m_entries;
    }

    [[nodiscard]] const std::array<VfxStackEntry, kVfxStageCount>& Entries() const noexcept {
        return m_entries;
    }

    /// Total number of module instances across every stage.
    [[nodiscard]] size_t TotalModuleCount() const noexcept {
        size_t n = 0;
        for (const auto& e : m_entries) n += e.modules.size();
        return n;
    }

    /// Returns true when every stage is empty (legacy asset, falls back
    /// to inline config path).
    [[nodiscard]] bool IsEmpty() const noexcept {
        for (const auto& e : m_entries) {
            if (!e.modules.empty()) return false;
        }
        return true;
    }

    /// Empties every stage without changing the `stage` fields.
    void Clear() noexcept {
        for (auto& e : m_entries) e.modules.clear();
    }

    /// Locates a module by its stable `instanceId` across every stage.
    ///
    /// # Out parameters
    /// - `outStage`    — stage containing the module (unchanged on miss).
    /// - `outIndex`    — slot within the stage (unchanged on miss).
    ///
    /// # Returns
    /// `true` on found. `false` when no module carries that id.
    [[nodiscard]] bool FindInstance(const std::string& instanceId,
                                      VfxStage& outStage,
                                      size_t& outIndex) const noexcept {
        for (uint8_t i = 0; i < kVfxStageCount; ++i) {
            const size_t idx = m_entries[i].Find(instanceId);
            if (idx != static_cast<size_t>(-1)) {
                outStage = static_cast<VfxStage>(i);
                outIndex = idx;
                return true;
            }
        }
        return false;
    }

private:
    std::array<VfxStackEntry, kVfxStageCount> m_entries{};
};

} // namespace uz
