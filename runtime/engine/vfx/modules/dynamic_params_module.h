#pragma once

/// VFX Dynamic Parameters Module -- per-particle Vec4 payload with temporal evolution.
///
/// Provides a configurable Vec4 parameter attached to each particle that can
/// evolve over the particle's lifetime. Three evolve modes are supported:
///
/// - **Constant** (0): The value remains at `defaultValue` for the particle's
///   entire lifetime. Use for static per-particle material inputs (e.g., a fixed
///   tint or UV offset).
///
/// - **Lerp-to-Zero** (1): The value linearly interpolates from `defaultValue`
///   toward zero over the particle's lifetime, scaled by `evolveRate`. At
///   normalizedAge = 1 and evolveRate = 1, the value reaches exactly zero.
///   Use for fade-out effects (e.g., emissive intensity decay).
///
/// - **Age-Normalized** (2): The value is scaled by the particle's normalized
///   age (age / lifespan), ramping from zero at birth to `defaultValue * evolveRate`
///   at death. Use for ramp-in effects (e.g., growing distortion).
///
/// The HLSL snippet for this module is empty -- the actual GPU computation is
/// handled by the particle_update_cs.hlsl shader via the constant buffer fields
/// (cbDynamicParamsDefault, cbDynamicParamsEvolveMode, cbDynamicParamsEvolveRate).
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.
///
/// @ref GDC 2014 "Destiny's Multithreaded Rendering Architecture" -- per-particle
///      payload concept for material-driven parameters.

#include "vfx_module.h"
#include "engine/math/vector4.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DynamicParamsModuleConfig -- per-particle Vec4 payload configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration data for `DynamicParamsModule`.
///
/// | Field        | Default         | Description                                      |
/// |--------------|-----------------|--------------------------------------------------|
/// | defaultValue | (0, 0, 0, 0)    | Initial Vec4 payload assigned at particle spawn.  |
/// | evolveMode   | 0 (Constant)    | 0=constant, 1=lerp-to-zero, 2=age-normalized.    |
/// | evolveRate   | 1.0             | Scale factor for temporal evolution speed.         |
///
/// @ref GDC 2014 "Destiny's Multithreaded Rendering Architecture" -- per-particle
///      payload concept for material-driven dynamic parameters.
struct DynamicParamsModuleConfig {
    /// Initial Vec4 payload assigned at particle spawn. Each component can
    /// drive a different material parameter (e.g., x=emissive, y=dissolve,
    /// z=distortion, w=custom).
    Vec4 defaultValue{0.0f, 0.0f, 0.0f, 0.0f};

    /// Temporal evolution mode:
    /// - 0: Constant -- value stays at defaultValue for the particle's lifetime.
    /// - 1: Lerp-to-zero -- linearly interpolates toward zero over lifetime.
    /// - 2: Age-normalized -- scales by (age / lifespan), ramping from zero.
    uint8_t evolveMode = 0;

    /// Scale factor applied to the temporal evolution. Higher values accelerate
    /// the evolution. Only meaningful when evolveMode != 0.
    float evolveRate = 1.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// DynamicParamsModule -- per-particle material payload module
// ─────────────────────────────────────────────────────────────────────────────

/// Per-particle Vec4 payload module with configurable temporal evolution.
///
/// Implements `IVFXModule`. The module emits no inline HLSL -- the GPU
/// computation is handled directly by the particle update compute shader
/// via constant buffer fields populated from this module's config.
///
/// # Example
/// ```cpp
/// DynamicParamsModuleConfig cfg;
/// cfg.defaultValue = Vec4{1.0f, 0.0f, 0.5f, 0.0f};
/// cfg.evolveMode   = 1; // lerp-to-zero
/// cfg.evolveRate   = 2.0f;
/// auto module = std::make_unique<DynamicParamsModule>(cfg);
/// stack.Push(std::move(module));
/// ```
class DynamicParamsModule final : public IVFXModule {
public:
    /// Default-constructs the module with `DynamicParamsModuleConfig` defaults.
    ///
    /// Required for `ModuleRegistry::Register<DynamicParamsModule>()` which
    /// instantiates modules via default construction before JSON config is
    /// applied at load time.
    DynamicParamsModule();

    /// Constructs the module with the given configuration.
    ///
    /// # Parameters
    /// - `config`: Dynamic params configuration. Copied into the module.
    explicit DynamicParamsModule(const DynamicParamsModuleConfig& config);

    ~DynamicParamsModule() override = default;

    // -- IVFXModule interface -------------------------------------------------

    /// Returns `"dynamic_params"`.
    ///
    /// This name is used for module registration, serialization, and UI display.
    /// It is stable -- renaming it would break saved VFX assets.
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns an empty string view.
    ///
    /// This module has no inline HLSL snippet -- the GPU computation is
    /// handled by the particle update compute shader via constant buffer
    /// fields (cbDynamicParamsDefault, cbDynamicParamsEvolveMode,
    /// cbDynamicParamsEvolveRate).
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns parameter definitions for dynamic params configuration.
    ///
    /// Exposes: default_value (Vec4), evolve_mode (Int), evolve_rate (Float).
    ///
    /// # Returns
    /// Vector of three `ParameterDef` entries. Never empty.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// Applies configuration from a JSON object.
    ///
    /// Accepted keys (all optional -- missing keys use config defaults):
    /// | Key               | Type          | Description                        |
    /// |-------------------|---------------|------------------------------------|
    /// | `"default_value"` | array[4]      | Four floats for the Vec4 payload.  |
    /// | `"evolve_mode"`   | int (0-2)     | Temporal evolution mode.           |
    /// | `"evolve_rate"`   | float (>= 0)  | Evolution speed scale factor.      |
    ///
    /// # Parameters
    /// - `config`: A `nlohmann::json` object. Non-object values log a VFX
    ///   warning and leave the module unchanged.
    void ApplyJsonConfig(const nlohmann::json& config) override;

    /// No-op. All dynamic params logic runs on the GPU via constant buffer.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration access -------------------------------------------------

    /// Returns the current dynamic params configuration.
    ///
    /// The reference is valid for the module's lifetime.
    [[nodiscard]] const DynamicParamsModuleConfig& GetConfig() const noexcept;

    // -- JSON deserialization (static) ----------------------------------------

    /// Parses a `DynamicParamsModuleConfig` from a JSON object.
    ///
    /// All fields are optional -- absent keys retain default values.
    /// Out-of-range evolve_mode is clamped to [0, 2] with a VFX warning.
    /// Negative evolve_rate is clamped to 0 with a VFX warning.
    ///
    /// # Parameters
    /// - `j`: A `nlohmann::json` object. Non-object values return defaults
    ///   with a VFX warning log.
    ///
    /// # Returns
    /// A fully populated `DynamicParamsModuleConfig`.
    [[nodiscard]] static DynamicParamsModuleConfig ParseConfig(const nlohmann::json& j);

    /// Current module configuration. Public for direct access by the compositor
    /// when uploading constant buffer data.
    DynamicParamsModuleConfig config;

private:
    // Config is stored in the public `config` member above (matching the task
    // specification). The public field allows the ModuleCompositor to read
    // config values directly without getter overhead.
};

} // namespace uz
