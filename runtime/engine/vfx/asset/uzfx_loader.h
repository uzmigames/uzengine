#pragma once

/// UzEngine UzfxLoader — Loads .uzfx emitter definitions from JSON files.
///
/// Parses the .uzfx format into `UzfxDesc` and can instantiate a live
/// `ParticleEmitter` with a fully configured module stack from the
/// parsed descriptor.

#include "uzfx_format.h"

#include <string>

namespace uz {

class IRHIDevice;
class ParticleBudget;
struct ParticleEmitter;

/// Loads and instantiates .uzfx particle emitter assets.
class UzfxLoader {
public:
    /// Parses a .uzfx JSON file into a descriptor.
    ///
    /// # Arguments
    /// * `jsonData` — Raw JSON string content of the .uzfx file.
    ///
    /// # Returns
    /// Parsed `UzfxDesc`. On parse error, returns default-constructed desc
    /// with empty name (caller should check `desc.name.empty()`).
    [[nodiscard]] static UzfxDesc Load(const std::string& jsonData);

    /// Instantiates a ParticleEmitter from a parsed descriptor.
    ///
    /// Creates the emitter, configures its module stack from the descriptor's
    /// module entries, and initializes GPU buffers via the device.
    ///
    /// # Arguments
    /// * `desc` — Parsed .uzfx descriptor.
    /// * `device` — RHI device for GPU buffer allocation.
    /// * `budget` — Global particle budget for allocation.
    ///
    /// # Returns
    /// `true` if the emitter was successfully created and initialized.
    [[nodiscard]] static bool Instantiate(
        const UzfxDesc& desc,
        ParticleEmitter& outEmitter,
        IRHIDevice* device,
        ParticleBudget& budget);
};

} // namespace uz
