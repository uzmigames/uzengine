#pragma once

/// VFX stack stage — Niagara-parity 7-stage execution pipeline.
///
/// Phase 11.1.0. Niagara organises every effect into 7 fixed execution
/// stages run in this order every simulation tick:
///
///   1. SystemSpawn    — runs once when the system is instantiated.
///                        Populates system-scope parameters the children
///                        can reference.
///   2. SystemUpdate   — runs each tick at the VfxSystem level.
///                        Updates system-scope state (global age,
///                        aggregate budgets, parent transforms).
///   3. EmitterSpawn   — runs once when an emitter is instantiated
///                        inside the system. Fills emitter-scope
///                        parameters (emission rate, spawn shape,
///                        default colour).
///   4. EmitterUpdate  — per-tick at the emitter level. Advances
///                        lifecycle (loopCount, emitterDuration),
///                        computes dynamic spawn rate from curves.
///   5. ParticleSpawn  — runs for each newly-spawned particle in the
///                        current frame. Initialises per-particle
///                        attributes (position, velocity, lifetime).
///   6. ParticleUpdate — per-tick for every alive particle. Applies
///                        gravity, damping, curl, vortex, collision,
///                        age advancement.
///   7. Render         — consumed by the draw pipeline. Billboard /
///                        mesh / light / ribbon renderers register here.
///
/// The enum ordering is load-bearing: `static_cast<uint8_t>(stage)`
/// doubles as the execution slot and an array index into `VfxStack`.
///
/// @ref UE5 `ENiagaraScriptUsage` + `SNiagaraStack`
/// @ref docs/engine/25-vfx/44-niagara-stack-model.md

#include <cstdint>

namespace uz {

/// Niagara-parity execution stage. Array-index semantics:
/// `static_cast<uint8_t>(stage)` is the slot index in `VfxStack::entries`.
enum class VfxStage : uint8_t {
    SystemSpawn    = 0,
    SystemUpdate   = 1,
    EmitterSpawn   = 2,
    EmitterUpdate  = 3,
    ParticleSpawn  = 4,
    ParticleUpdate = 5,
    Render         = 6,
    Count          = 7,
};

/// Stable string for the stage (used for JSON round-trip, logs,
/// UI labels). Never returns null.
[[nodiscard]] constexpr const char* VfxStageName(VfxStage s) noexcept {
    switch (s) {
        case VfxStage::SystemSpawn:    return "SystemSpawn";
        case VfxStage::SystemUpdate:   return "SystemUpdate";
        case VfxStage::EmitterSpawn:   return "EmitterSpawn";
        case VfxStage::EmitterUpdate:  return "EmitterUpdate";
        case VfxStage::ParticleSpawn:  return "ParticleSpawn";
        case VfxStage::ParticleUpdate: return "ParticleUpdate";
        case VfxStage::Render:         return "Render";
        case VfxStage::Count:          return "Count";
    }
    return "Unknown";
}

/// Parses a stage name produced by `VfxStageName`. Returns `true` on
/// success, `false` on unknown / empty input (output unchanged).
[[nodiscard]] constexpr bool VfxStageFromName(const char* name, VfxStage& out) noexcept {
    if (name == nullptr) return false;
    struct Pair { const char* n; VfxStage v; };
    constexpr Pair table[] = {
        {"SystemSpawn",    VfxStage::SystemSpawn},
        {"SystemUpdate",   VfxStage::SystemUpdate},
        {"EmitterSpawn",   VfxStage::EmitterSpawn},
        {"EmitterUpdate",  VfxStage::EmitterUpdate},
        {"ParticleSpawn",  VfxStage::ParticleSpawn},
        {"ParticleUpdate", VfxStage::ParticleUpdate},
        {"Render",         VfxStage::Render},
    };
    for (const auto& p : table) {
        const char* a = p.n;
        const char* b = name;
        while (*a != '\0' && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') {
            out = p.v;
            return true;
        }
    }
    return false;
}

/// Number of stages in the pipeline. Equals `static_cast<int>(VfxStage::Count)`.
inline constexpr int kVfxStageCount = static_cast<int>(VfxStage::Count);

} // namespace uz
