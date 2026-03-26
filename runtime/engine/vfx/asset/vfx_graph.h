#pragma once

/// VFX Graph asset -- defines a complete particle effect from a .uzvfx file.
///
/// A VFXGraph describes the modules, parameters, and events that define a
/// particle effect. It is loaded from a .uzvfx JSON file and can be instantiated
/// on multiple emitters simultaneously.
///
/// # File Format (.uzvfx)
/// ```json
/// {
///   "format": "uzvfx",
///   "version": 1,
///   "emitter": { "max_particles": 10000, "lifespan_default": 5.0 },
///   "modules": [
///     { "type": "spawn", "config": { "rate": 500 } },
///     { "type": "update", "config": { "gravity": [0,-9.81,0] } },
///     { "type": "decay" }
///   ],
///   "parameters": [
///     { "name": "intensity", "type": "float", "default": 1.0, "min": 0.0, "max": 10.0 }
///   ],
///   "events": [
///     { "name": "burst", "emission_count": 100, "velocity_scale": 2.0 }
///   ]
/// }
/// ```
///
/// # Thread Safety
/// Not thread-safe. Loaded and accessed on the main thread only.

#include "engine/vfx/events/vfx_event.h"
#include "engine/vfx/modules/vfx_module.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace uz {

class ModuleRegistry;

// ─────────────────────────────────────────────────────────────────────────────
// VFXParameterDef -- exposed parameter with range constraints
// ─────────────────────────────────────────────────────────────────────────────

/// Parameter definition with min/max ranges for UI and validation.
///
/// Describes a single tweakable parameter exposed by a VFX graph. The editor
/// uses this to generate sliders/color pickers, and the runtime uses it to
/// validate parameter overrides at instantiation time.
struct VFXParameterDef {
    std::string name;                          ///< Unique parameter name.
    ParameterType type = ParameterType::Float; ///< Data type of this parameter.
    float defaultFloat = 0.0f;                 ///< Default value (Float type).
    Vec3 defaultVec3 = {};                     ///< Default value (Vec3 type).
    Vec4 defaultVec4 = {};                     ///< Default value (Vec4 type).
    float minValue = 0.0f;                     ///< Minimum value (Float type).
    float maxValue = 1.0f;                     ///< Maximum value (Float type).
};

// ─────────────────────────────────────────────────────────────────────────────
// VFXEmitterConfig -- emitter-level settings from the graph
// ─────────────────────────────────────────────────────────────────────────────

/// Emitter configuration from the graph asset.
///
/// Defines the particle pool size and default lifespan. These values are
/// used when instantiating emitters from this graph.
struct VFXEmitterConfig {
    uint32_t maxParticles = 10000;  ///< Maximum particle pool capacity.
    float lifespanDefault = 5.0f;   ///< Default particle lifespan in seconds.
};

// ─────────────────────────────────────────────────────────────────────────────
// VFXGraph -- loaded .uzvfx asset
// ─────────────────────────────────────────────────────────────────────────────

/// A loaded VFX graph representing a complete particle effect definition.
///
/// Owns all module instances, parameter definitions, and event definitions
/// parsed from a .uzvfx JSON file. Non-copyable due to module ownership,
/// but movable for container storage.
///
/// # Lifetime
/// Created via `Load()` or `LoadFromString()`, then referenced by emitters.
/// The graph must outlive all emitters that reference it.
class VFXGraph {
public:
    VFXGraph() = default;
    ~VFXGraph() = default;

    /// Non-copyable (owns modules via unique_ptr).
    VFXGraph(const VFXGraph&) = delete;
    VFXGraph& operator=(const VFXGraph&) = delete;

    /// Movable for container storage.
    VFXGraph(VFXGraph&&) noexcept = default;
    VFXGraph& operator=(VFXGraph&&) noexcept = default;

    /// Loads a VFXGraph from a .uzvfx JSON file on disk.
    ///
    /// Reads the file at `path`, parses JSON, and instantiates modules via
    /// the provided registry.
    ///
    /// # Parameters
    /// - `path`: Filesystem path to the .uzvfx file.
    /// - `registry`: Module factory used to instantiate modules by type name.
    ///
    /// # Returns
    /// - Owning pointer to the loaded graph on success.
    /// - `nullptr` if the file cannot be read or JSON is malformed.
    ///
    /// # Side Effects
    /// Logs `VFX/Error` on parse failure with the file path and reason.
    [[nodiscard]] static std::unique_ptr<VFXGraph> Load(
        const std::string& path, const ModuleRegistry& registry);

    /// Loads a VFXGraph from a JSON string (primarily for testing).
    ///
    /// # Parameters
    /// - `jsonStr`: UTF-8 JSON string in .uzvfx format.
    /// - `registry`: Module factory used to instantiate modules by type name.
    ///
    /// # Returns
    /// - Owning pointer to the loaded graph on success.
    /// - `nullptr` if JSON is malformed or format is not "uzvfx".
    ///
    /// # Side Effects
    /// Logs `VFX/Error` on parse failure with the specific reason.
    [[nodiscard]] static std::unique_ptr<VFXGraph> LoadFromString(
        const std::string& jsonStr, const ModuleRegistry& registry);

    // ── Accessors ────────────────────────────────────────────────────────────

    /// Returns the graph name (derived from filename or JSON).
    [[nodiscard]] const std::string& GetName() const noexcept { return name_; }

    /// Returns the .uzvfx format version.
    [[nodiscard]] uint32_t GetVersion() const noexcept { return version_; }

    /// Returns the emitter configuration (pool size, default lifespan).
    [[nodiscard]] const VFXEmitterConfig& GetEmitterConfig() const noexcept {
        return emitterConfig_;
    }

    /// Returns the ordered list of modules parsed from the graph.
    [[nodiscard]] const std::vector<std::unique_ptr<IVFXModule>>& GetModules() const noexcept {
        return modules_;
    }

    /// Returns the parameter definitions for this graph.
    [[nodiscard]] const std::vector<VFXParameterDef>& GetParameters() const noexcept {
        return parameters_;
    }

    /// Returns the event definitions for this graph.
    [[nodiscard]] const std::vector<VFXEventDef>& GetEvents() const noexcept {
        return events_;
    }

    /// Returns the filesystem path this graph was loaded from (empty if from string).
    [[nodiscard]] const std::string& GetSourcePath() const noexcept {
        return sourcePath_;
    }

private:
    std::string name_;                               ///< Graph name.
    std::string sourcePath_;                          ///< Source .uzvfx file path.
    uint32_t version_ = 0;                           ///< Format version.
    VFXEmitterConfig emitterConfig_;                  ///< Emitter pool config.
    std::vector<std::unique_ptr<IVFXModule>> modules_;///< Ordered module list.
    std::vector<VFXParameterDef> parameters_;         ///< Exposed parameters.
    std::vector<VFXEventDef> events_;                 ///< Named event triggers.
};

} // namespace uz
