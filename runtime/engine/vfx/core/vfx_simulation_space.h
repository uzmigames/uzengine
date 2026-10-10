#pragma once

/// Simulation-space mode shared between CPU configuration and GPU shaders.
///
/// UzEngine emitters run in one of two coordinate frames:
///   - `WorldSpace` (default): positions + velocities stored in world space.
///     Render pipeline is straightforward — the billboard VS skips a
///     world-from-local matrix multiply. Emitter motion does NOT drag
///     existing particles (trails lag behind fast movers).
///   - `LocalSpace`: positions + velocities stored in the emitter's local
///     frame. The billboard VS applies `worldFromLocal` before the view
///     transform; gravity / external forces are rotated into local space
///     at integration time so physics behaves identically to world-space
///     simulation.
///
/// Niagara exposes this via `UNiagaraEmitter::bLocalSpace`. UzEngine uses
/// a typed enum (mirroring the HLSL uint convention) so the spawn / update
/// CS sampler can branch without magic numbers.
///
/// @ref UE5 UNiagaraEmitter::bLocalSpace

#include <cstdint>

namespace uz {

/// Coordinate frame in which particle SoA channels are stored.
enum class VfxSimulationSpace : uint8_t {
    WorldSpace = 0,   ///< Default. Positions are world-space cm.
    LocalSpace = 1,   ///< Positions are in the emitter's local frame.
};

/// Human-readable name for logging + JSON round-trip.
[[nodiscard]] constexpr const char* VfxSimulationSpaceToString(VfxSimulationSpace s) noexcept {
    switch (s) {
        case VfxSimulationSpace::WorldSpace: return "WorldSpace";
        case VfxSimulationSpace::LocalSpace: return "LocalSpace";
    }
    return "Unknown";
}

/// Parses the canonical string form. Returns `true` on success.
[[nodiscard]] inline bool VfxSimulationSpaceFromString(const char* str,
                                                      VfxSimulationSpace& out) noexcept {
    if (!str) return false;
    // Character-by-character compare so we avoid <cstring> in headers.
    const char* ws = "WorldSpace";
    const char* ls = "LocalSpace";
    int i = 0;
    while (ws[i] && str[i] && ws[i] == str[i]) ++i;
    if (ws[i] == '\0' && str[i] == '\0') { out = VfxSimulationSpace::WorldSpace; return true; }
    i = 0;
    while (ls[i] && str[i] && ls[i] == str[i]) ++i;
    if (ls[i] == '\0' && str[i] == '\0') { out = VfxSimulationSpace::LocalSpace; return true; }
    return false;
}

} // namespace uz
