#pragma once

/// UzEngine UzfxLoader — Loads .uzvfx system definitions from JSON files.
///
/// Supports three formats:
///   - **v2 multi-emitter**: `"emitters": [...]` array
///   - **v1 single-emitter**: `"emitter": {...}` + `"modules": {...}`
///   - **v0 flat presets**: all keys at root level (legacy preset editor)

#include "uzfx_format.h"

#include <string>

namespace uz {

class IRHIDevice;
class ParticleBudget;
struct ParticleEmitter;

/// Loads and instantiates .uzvfx particle system assets.
class UzfxLoader {
public:
    /// Parses a .uzvfx JSON file into a system descriptor.
    ///
    /// Handles all three format versions. Returns desc with empty name on error.
    [[nodiscard]] static UzfxDesc Load(const std::string& jsonData);

    /// Instantiates a single ParticleEmitter from one emitter descriptor.
    ///
    /// Configures module stack and initializes GPU buffers.
    [[nodiscard]] static bool InstantiateEmitter(
        const UzfxEmitterDesc& emitterDesc,
        ParticleEmitter& outEmitter,
        IRHIDevice* device,
        ParticleBudget& budget);

    /// Legacy: instantiate from a full UzfxDesc (uses first emitter).
    [[nodiscard]] static bool Instantiate(
        const UzfxDesc& desc,
        ParticleEmitter& outEmitter,
        IRHIDevice* device,
        ParticleBudget& budget);
};

} // namespace uz
