#pragma once

/// VFX Snapshot Viewer Widget — interactive inspection of a `.uzvfxsnap`.
///
/// Phase 9.8.0 (viewer side). Runs the data-model side of the snapshot
/// viewer without requiring a window/renderer: loads a `.uzvfxsnap` file,
/// lets the caller select a channel + scrub through particles, and
/// computes min/max/average stats for the selected channel.
///
/// A full GPU point-cloud viewer requires a mini-renderer that is a
/// separate engineering task (vfx_viewport_app can be extended to host
/// it). This widget provides the stateful model that the host renders
/// against — orbital camera state, channel selector, per-particle data
/// cursor.
///
/// # Model pipeline
///
/// ```
///  VfxSnapshotLoadFromFile(path)
///         │
///         ▼
///  VfxSnapshotViewerWidget::SetSnapshot(snap)
///     ├─ populates ChannelNames()
///     ├─ selects channel 0 by default
///     └─ computes ChannelStats() for the selected channel
///         │
///         ▼
///  Host renders channel picker, particle cursor, stats overlay.
/// ```
///
/// @ref engine/vfx/baker/vfx_snapshot.h (data model + loader)
/// @ref docs/engine/25-vfx/36-emitter-snapshot.md

#include "engine/vfx/baker/vfx_snapshot.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Channel statistics (min / max / average per component)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-channel aggregate across all alive particles.
struct VfxSnapshotChannelStats {
    uint32_t componentCount = 0; ///< 1 (Float/Uint32/Int32/Bool/Half) or 2/3/4 (Float2/3/4).
    float    minValue[4] = { 0, 0, 0, 0 };
    float    maxValue[4] = { 0, 0, 0, 0 };
    float    avgValue[4] = { 0, 0, 0, 0 };
    uint32_t sampleCount = 0;     ///< Number of samples included.
};

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────

class VfxSnapshotViewerWidget {
public:
    VfxSnapshotViewerWidget() = default;

    /// Loads a snapshot file into the widget. Returns false on load
    /// failure; the widget's state is reset to empty.
    bool LoadFromFile(const std::string& path, std::string* outError = nullptr);

    /// Accepts an already-loaded snapshot (ownership transferred).
    void SetSnapshot(VfxSnapshot snapshot);

    /// Clears the loaded snapshot and all derived state.
    void Clear() noexcept;

    /// Returns true when a snapshot is loaded.
    [[nodiscard]] bool HasSnapshot() const noexcept { return loaded_; }

    /// Returns the loaded snapshot (empty when not loaded).
    [[nodiscard]] const VfxSnapshot& Snapshot() const noexcept { return snapshot_; }

    /// Returns the list of channel names present in the snapshot.
    [[nodiscard]] const std::vector<std::string>& ChannelNames() const noexcept {
        return channelNames_;
    }

    /// Returns the index of the currently selected channel (UINT32_MAX
    /// when none selected).
    [[nodiscard]] uint32_t SelectedChannelIndex() const noexcept { return selectedChannel_; }

    /// Selects a channel by index. Recomputes stats.
    void SelectChannel(uint32_t channelIndex);

    /// Returns the stats for the selected channel. Zero counts when no
    /// channel is selected.
    [[nodiscard]] const VfxSnapshotChannelStats& SelectedChannelStats() const noexcept {
        return stats_;
    }

    // ── Particle cursor ─────────────────────────────────────────────────

    /// Returns the index of the particle currently under the inspection
    /// cursor (for per-particle readouts in the host UI).
    [[nodiscard]] uint32_t ParticleCursor() const noexcept { return particleCursor_; }

    /// Sets the particle cursor. Clamped to `[0, AliveCount()-1]`.
    void SetParticleCursor(uint32_t index) noexcept;

    /// Returns the number of alive particles in the snapshot.
    [[nodiscard]] uint32_t AliveCount() const noexcept;

    // ── Orbital camera state (for future GPU viewer) ───────────────────

    /// Camera orbit angles (radians) + distance (metres). The host reads
    /// these to drive the point-cloud view matrix.
    float orbitYaw     = 0.0f;
    float orbitPitch   = 0.0f;
    float orbitDistance = 10.0f;

    /// Tumble the camera by (dYaw, dPitch) radians.
    void Tumble(float dYaw, float dPitch) noexcept;

    /// Dolly in/out (positive = zoom in, negative = zoom out).
    void Dolly(float delta) noexcept;

private:
    /// Recomputes `stats_` from the selected channel.
    void RecomputeStats();

    VfxSnapshot                 snapshot_{};
    bool                        loaded_           = false;
    std::vector<std::string>    channelNames_;
    uint32_t                    selectedChannel_  = UINT32_MAX;
    uint32_t                    particleCursor_   = 0;
    VfxSnapshotChannelStats     stats_{};
};

} // namespace uz
