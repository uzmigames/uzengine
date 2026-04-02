#pragma once

/// UzEngine .uzvfx Asset Format — Multi-emitter VFX system definitions.
///
/// Inspired by Niagara's System → Emitter → Module hierarchy:
///   - **System**: root container, one per .uzvfx file. Holds shared params
///     and N emitters that compose a complete visual effect.
///   - **Emitter**: independent particle pool with its own spawn rate,
///     lifetime, capacity, and ordered module stack.
///   - **Module**: discrete behavior (spawn shape, gravity, turbulence,
///     collision, etc.) applied in stack order.
///
/// # Backward Compatibility
/// Files with a single "emitter" key (no "emitters" array) are treated as
/// a system with one emitter. Flat-format presets (no "emitter"/"modules"
/// sections) are also supported via fallback parsing.
///
/// # Example .uzvfx (multi-emitter fire effect)
/// ```json
/// {
///   "name": "Campfire",
///   "version": 2,
///   "emitters": [
///     {
///       "name": "Flames",
///       "max_particles": 5000,
///       "emission_rate": 400,
///       "lifetime": 1.0,
///       "looping": true,
///       "modules": {
///         "spawn": {
///           "type": "spawn",
///           "velocity": [0, 0, 300],
///           "spread_angle": 45,
///           "lifespan_min": 0.3,
///           "lifespan_max": 0.8
///         },
///         "update": [
///           { "type": "update", "gravity": [0, 0, 200], "drag": 0.3 },
///           { "type": "turbulence", "strength": 150, "frequency": 0.8 }
///         ],
///         "render": {
///           "type": "sprite",
///           "width": 200,
///           "height": 200,
///           "texture": "vfx/fireball_6x6.png",
///           "blend_mode": "additive",
///           "flipbook_rows": 6,
///           "flipbook_cols": 6,
///           "flipbook_fps": 24
///         }
///       }
///     },
///     {
///       "name": "Sparks",
///       "max_particles": 2000,
///       "emission_rate": 200,
///       "lifetime": 2.0,
///       "looping": true,
///       "modules": {
///         "spawn": {
///           "type": "spawn",
///           "velocity": [0, 0, 800],
///           "spread_angle": 30,
///           "lifespan_min": 0.5,
///           "lifespan_max": 1.5
///         },
///         "update": [
///           { "type": "update", "gravity": [0, 0, -981], "drag": 0.02 }
///         ],
///         "render": {
///           "type": "sprite",
///           "width": 40,
///           "height": 40,
///           "blend_mode": "additive"
///         }
///       }
///     }
///   ]
/// }
/// ```
///
/// # Module Types
/// | Type          | Phase  | Description                                      |
/// |---------------|--------|--------------------------------------------------|
/// | spawn         | Spawn  | Initial position/velocity/shape/lifespan         |
/// | update        | Update | Gravity + drag (base physics)                    |
/// | turbulence    | Update | Curl noise force field                           |
/// | attractor     | Update | Point attractor / repulsor                       |
/// | orbit         | Update | Circular orbit around axis                       |
/// | collision     | Update | Ground plane / depth buffer collision             |
/// | vector_field  | Update | 3D procedural vector field advection             |
/// | decay         | Update | Fade-out at end of life                          |
/// | sprite        | Render | Billboard quad (optional flipbook SubUV)         |
/// | mesh          | Render | Instanced mesh per particle                      |
/// | light         | Render | Per-particle dynamic point light                 |

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// A single module entry: type name + raw JSON params blob.
struct UzfxModuleEntry {
    std::string type;       ///< Module type (e.g., "spawn", "turbulence", "sprite").
    std::string paramsJson; ///< Raw JSON string of module-specific parameters.
};

/// Per-emitter descriptor within a system.
struct UzfxEmitterDesc {
    std::string name;                         ///< Human-readable emitter name.
    uint32_t    maxParticles  = 10000;        ///< Particle pool capacity.
    float       emissionRate  = 100.0f;       ///< Particles per second.
    float       lifetime      = 2.0f;         ///< Base particle lifespan (seconds).
    bool        looping       = true;         ///< Restart emission after all particles die.

    UzfxModuleEntry              spawnModule;   ///< Single spawn module config.
    std::vector<UzfxModuleEntry> updateModules; ///< Ordered update module stack.
    UzfxModuleEntry              renderModule;  ///< Single render module config.
};

/// Root system descriptor — one per .uzvfx file.
/// Contains N emitters that together form a complete VFX effect.
struct UzfxDesc {
    std::string name;                         ///< System name (e.g., "Campfire").
    uint32_t    version = 2;                  ///< Format version.

    std::vector<UzfxEmitterDesc> emitters;    ///< All emitters in this system.

    // ── Legacy compat (version 1, single emitter) ───────────────────────
    // When parsing version 1 files with a single "emitter" key,
    // the loader creates one UzfxEmitterDesc and pushes it into `emitters`.
};

} // namespace uz
