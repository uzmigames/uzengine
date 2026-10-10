#include "vfx_trail_decal.h"

#include <cmath>

namespace uz {

namespace {

float Distance(const Vec3& a, const Vec3& b) noexcept {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

Vec3 Normalize(const Vec3& v) noexcept {
    const float l = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    return (l > 1e-6f) ? Vec3{v.x/l, v.y/l, v.z/l} : Vec3{1.0f, 0.0f, 0.0f};
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// VfxShouldEmitTrailDecal — pure predicate
// ─────────────────────────────────────────────────────────────────────────────

bool VfxShouldEmitTrailDecal(const VfxTrailDecalConfig& cfg,
                              const VfxTrailDecalParticleState& state,
                              float now) noexcept {
    if (cfg.cadenceValue <= 0.0f) return false;
    if (!state.primed)             return false;

    if (cfg.cadence == VfxTrailDecalCadence::Distance) {
        return state.accumulatedDistance >= cfg.cadenceValue;
    }
    // Time mode.
    return (now - state.lastEmitTime) >= cfg.cadenceValue;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxTrailDecalEmitter::TryEmit
// ─────────────────────────────────────────────────────────────────────────────

bool VfxTrailDecalEmitter::TryEmit(uint64_t particleId, const Vec3& pos,
                                    float now, VfxTrailDecalEvent& outEvent) {
    auto& state = states_[particleId];

    if (!state.primed) {
        // First sample for this particle — prime the state.
        state.prevPos            = pos;
        state.lastEmitPos        = pos;
        state.lastEmitTime       = now;
        state.accumulatedDistance = 0.0f;
        state.primed             = true;
        // skipFirstSample (default true) means we never emit on the first
        // sample even if cadenceValue is 0 — authors almost always want the
        // first mark after travel, not at the spawn point.
        return false;
    }

    // Accumulate distance since last emit.
    state.accumulatedDistance += Distance(pos, state.prevPos);
    state.prevPos = pos;

    if (!VfxShouldEmitTrailDecal(config_, state, now)) {
        return false;
    }

    // Build the event. Forward = (current - lastEmit) normalised; center =
    // midpoint of the swept segment so adjacent decals butt cleanly.
    const Vec3 segment = {pos.x - state.lastEmitPos.x,
                          pos.y - state.lastEmitPos.y,
                          pos.z - state.lastEmitPos.z};
    outEvent.forward  = Normalize(segment);
    outEvent.centerPos = {
        (pos.x + state.lastEmitPos.x) * 0.5f,
        (pos.y + state.lastEmitPos.y) * 0.5f,
        (pos.z + state.lastEmitPos.z) * 0.5f,
    };
    outEvent.width    = config_.width;
    outEvent.length   = config_.length;
    outEvent.lifetime = config_.lifetime;
    outEvent.material = config_.decalMaterial;

    // Reset state for the next segment.
    state.lastEmitPos         = pos;
    state.lastEmitTime        = now;
    state.accumulatedDistance = 0.0f;
    return true;
}

} // namespace uz
