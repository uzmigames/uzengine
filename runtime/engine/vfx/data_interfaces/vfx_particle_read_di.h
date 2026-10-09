#pragma once

/// Cross-emitter particle attribute read — Niagara `UNiagaraDataInterfaceParticleRead`
/// parity.
///
/// Some effects need one emitter to observe another emitter's particles:
///   - Trailing fire emits sparks that track a "master" head particle.
///   - A shockwave pulses on the N brightest particles.
///   - A homing projectile reads the current position of its parent.
///
/// Without a cross-emitter read DI, these dependencies require CPU
/// readback round-trips. Niagara solves it with a GPU-side structured-
/// buffer binding: the reader emitter declares a `readEmitters` list at
/// bind time; the dispatcher patches the reader's CB with the source
/// emitters' SoA SRV indices so the follower CS can `g_sourcePositions[id]`
/// directly.
///
/// # Persistent ID dependency
///
/// Naive index-based reads (`ReadPosition(emitterSlot, index)`) break
/// when slot recycles race with the follower's sampling frame. The
/// robust variant keys off persistent IDs (see
/// phase7_7.4.0-vfx-persistent-particle-ids):
///
///   1. Writer emitter opts in via `ParticleEmitter::emitsPersistentIDs = true`.
///   2. Reader emitter requests reads by ID.
///   3. GPU sampler performs a bucketed lookup (id -> SoA slot) via an
///      index hashmap rebuilt each frame. Miss returns a configurable
///      sentinel (NaN or zero).
///
/// # CPU reference
///
/// This header ships the CPU API + in-memory bucketed index used by unit
/// tests and future CPU-preview tooling. The GPU upload + HLSL sampler
/// land in the follow-up.
///
/// @ref UE5 UNiagaraDataInterfaceParticleRead
/// @ref phase7_7.4.0-vfx-persistent-particle-ids

#include "vector3.h"
#include "vector4.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace uz {

/// Attribute snapshot returned by the sampler. Layout matches the
/// common SoA channels that every emitter populates.
struct VfxParticleReadSnapshot {
    Vec3 position {};
    Vec3 velocity {};
    Vec4 color    {0.0f, 0.0f, 0.0f, 0.0f};
    float size = 0.0f;
    float age  = 0.0f;

    /// True when the sample resolved to a live particle in the source
    /// emitter. False for missing IDs, dead slots, or stale reads — the
    /// numeric fields hold the caller-supplied defaults in that case.
    bool valid = false;
};

/// Default values returned on a miss. Callers that want NaN-poisoned
/// reads to bubble up through physics can set `position = {NaN, NaN, NaN}`.
struct VfxParticleReadDefaults {
    Vec3  position {};
    Vec3  velocity {};
    Vec4  color    {};
    float size = 0.0f;
    float age  = 0.0f;
};

/// CPU-side snapshot of a source emitter's SoA buffers.
///
/// The real runtime never copies SoA data CPU-side — this struct is the
/// reference bucket the HLSL sampler mirrors via bindless SRVs. Unit
/// tests populate the vectors directly so the algorithm can be
/// exercised without a GPU.
struct VfxParticleReadSource {
    std::vector<uint64_t> persistentIDs;  ///< IDs per live slot. Dead slots use 0.
    std::vector<Vec3>     positions;
    std::vector<Vec3>     velocities;
    std::vector<Vec4>     colors;
    std::vector<float>    sizes;
    std::vector<float>    ages;

    /// Hashmap persistent ID -> slot index into the parallel vectors.
    /// Rebuilt via `RebuildIdIndex`; `SampleByID` returns `valid = false`
    /// when the map hasn't been rebuilt or the ID is absent.
    std::unordered_map<uint64_t, uint32_t> idIndex;

    /// Rebuilds `idIndex` from `persistentIDs`. Call after the source
    /// emitter's readback lands or when tests mutate the SoA arrays.
    void RebuildIdIndex();

    /// Interpolated read by persistent ID. Returns a miss when the id is
    /// 0 (sentinel) or not in the map; `defaults` populate the numeric
    /// fields in that case so the caller never reads uninitialised data.
    [[nodiscard]] VfxParticleReadSnapshot SampleByID(
        uint64_t id,
        const VfxParticleReadDefaults& defaults = {}) const;

    /// Direct slot-index read — cheaper than SampleByID but unsafe across
    /// slot recycles. Use only when the slot index is proven stable for
    /// the sampling frame (e.g. within one dispatch).
    [[nodiscard]] VfxParticleReadSnapshot SampleBySlot(
        uint32_t slot,
        const VfxParticleReadDefaults& defaults = {}) const;

    /// Number of live slots (persistentIDs.size()).
    [[nodiscard]] uint32_t GetSlotCount() const noexcept {
        return static_cast<uint32_t>(persistentIDs.size());
    }
};

} // namespace uz
