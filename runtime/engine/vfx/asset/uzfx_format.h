#pragma once

/// UzEngine .uzfx Asset Format — Declarative VFX emitter definitions.
///
/// JSON-based format that describes a complete particle emitter configuration
/// including spawn parameters, module stack, and render settings. Loaded by
/// `UzfxLoader` and used to construct `ParticleEmitter` + `EmitterModuleStack`
/// instances at runtime.
///
/// # Example .uzfx file
/// ```json
/// {
///   "name": "Fire Effect",
///   "version": 1,
///   "emitter": {
///     "max_particles": 10000,
///     "emission_rate": 500.0,
///     "lifetime": 2.0,
///     "looping": true
///   },
///   "modules": {
///     "spawn": {
///       "type": "spawn",
///       "velocity": [0.0, 8.0, 0.0],
///       "spread_angle": 30.0,
///       "lifespan_min": 0.5,
///       "lifespan_max": 2.0
///     },
///     "update": [
///       {
///         "type": "update",
///         "gravity": [0.0, -3.0, 0.0],
///         "drag": 0.98
///       },
///       {
///         "type": "turbulence",
///         "strength": 2.0,
///         "frequency": 0.5,
///         "octaves": 3
///       },
///       {
///         "type": "decay"
///       }
///     ],
///     "render": {
///       "type": "sprite",
///       "width": 0.5,
///       "height": 0.5,
///       "texture": "textures/fire_atlas.png",
///       "blend_mode": "additive"
///     }
///   }
/// }
/// ```

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Parsed .uzfx emitter configuration (intermediate representation).
///
/// Populated by `UzfxLoader::Load()`, consumed by `UzfxLoader::Instantiate()`
/// to create a live `ParticleEmitter` with configured module stack.
struct UzfxDesc {
    std::string name;
    uint32_t version = 1;

    // ── Emitter base params ──────────────────────────────────────────────
    uint32_t maxParticles  = 10000;
    float    emissionRate  = 100.0f;
    float    lifetime      = 2.0f;
    bool     looping       = true;

    // ── Module descriptors ───────────────────────────────────────────────
    /// Each module entry: type string + key-value params as JSON blob.
    struct ModuleEntry {
        std::string type;                              ///< Module type name (e.g., "spawn", "update", "turbulence").
        std::string paramsJson;                        ///< Raw JSON string of module-specific parameters.
    };

    ModuleEntry              spawnModule;               ///< Single spawn module config.
    std::vector<ModuleEntry> updateModules;             ///< Ordered list of update modules.
    ModuleEntry              renderModule;              ///< Single render module config.
};

} // namespace uz
