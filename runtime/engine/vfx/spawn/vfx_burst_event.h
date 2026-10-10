#pragma once

/// Niagara-parity time-stamped spawn burst event.
///
/// Phase 11.8.0. A `VfxBurstEvent` schedules a one-shot emission of
/// `count` particles at a specific absolute emitter time (seconds since
/// registration). Complements the per-frame `spawnRateCurve` (phase
/// 11.7) which drives continuous emission — bursts are the spike layer
/// that fires discrete "puff" events (muzzle flash, impact shower,
/// spell-cast on-hit shower).
///
/// # Ordering invariant
///
/// `ParticleEmitter::bursts` is kept sorted by `time` ascending. The
/// helper `VfxBurstList` namespace below maintains the invariant on
/// every mutation (add / update / move / remove). A monotonic
/// `burstCursor` on the emitter advances past fired entries each frame
/// so the per-tick scan is O(number of bursts firing this frame), not
/// O(bursts).
///
/// # JSON schema
///
/// ```json
/// { "bursts": [ { "t": 0.2, "n": 300 }, { "t": 0.4, "n": 150 } ] }
/// ```
///
/// @ref UE5 `FNiagaraSpawnInfo::Burst`
/// @ref docs/engine/25-vfx/48-burst-events.md

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace uz {

/// One scheduled burst: `count` particles at `time` seconds since
/// emitter registration.
struct VfxBurstEvent {
    float    time  = 0.0f;   ///< Absolute emitter time (seconds).
    uint32_t count = 0u;     ///< Particle count to emit on fire.

    [[nodiscard]] bool operator==(const VfxBurstEvent& o) const noexcept {
        return time == o.time && count == o.count;
    }
};

/// Sorted-ordering invariant helpers. Stateless — every helper takes a
/// `std::vector<VfxBurstEvent>&` so the same invariants apply whether
/// the owner is `ParticleEmitter`, a test harness, or a snapshot.
namespace VfxBurstList {

/// Inserts `burst` into `out` so the vector stays sorted ascending by
/// `time`. Returns the index of the newly inserted entry.
inline size_t Add(std::vector<VfxBurstEvent>& out,
                   const VfxBurstEvent& burst) {
    auto it = std::upper_bound(
        out.begin(), out.end(), burst,
        [](const VfxBurstEvent& a, const VfxBurstEvent& b) {
            return a.time < b.time;
        });
    const size_t idx = static_cast<size_t>(it - out.begin());
    out.insert(it, burst);
    return idx;
}

/// Removes the entry at `index`. Returns true when a mutation occurred.
inline bool Remove(std::vector<VfxBurstEvent>& out, size_t index) {
    if (index >= out.size()) return false;
    out.erase(out.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

/// Updates the entry at `index` with `updated`. Re-sorts if `time`
/// changed such that the entry no longer respects the ordering. Returns
/// the new index (may differ from the input after a re-sort), or
/// `SIZE_MAX` on out-of-range.
inline size_t Update(std::vector<VfxBurstEvent>& out, size_t index,
                      const VfxBurstEvent& updated) {
    if (index >= out.size()) return static_cast<size_t>(-1);
    out[index] = updated;
    // Re-sort lazily — vectors are small (typical < 16 entries).
    std::sort(out.begin(), out.end(),
              [](const VfxBurstEvent& a, const VfxBurstEvent& b) {
                  return a.time < b.time;
              });
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i].time == updated.time && out[i].count == updated.count) {
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

/// Counts how many entries fire in `(cursor, currentTime]` — i.e.
/// whose `time` is ≤ `currentTime` starting at index `cursor`. The
/// out-params `outFiredCount` / `outNewCursor` let callers consume
/// the fired entries and advance the cursor atomically.
inline void AdvanceCursor(const std::vector<VfxBurstEvent>& bursts,
                           size_t cursor, float currentTime,
                           uint32_t& outFiredCount, size_t& outNewCursor) {
    outFiredCount = 0u;
    outNewCursor  = cursor;
    while (outNewCursor < bursts.size() &&
           bursts[outNewCursor].time <= currentTime) {
        outFiredCount += bursts[outNewCursor].count;
        ++outNewCursor;
    }
}

} // namespace VfxBurstList

} // namespace uz
