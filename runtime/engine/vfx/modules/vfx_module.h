#pragma once

/// VFX Module -- abstract base for composable GPU particle behavior modules.
///
/// Each module represents one piece of particle behavior (gravity, noise, drag,
/// color-over-life, etc.) that can be composed into a full simulation shader.
/// Modules provide both a CPU-side execution hook (dispatch prep) and an HLSL
/// snippet that the ModuleCompositor stitches into a single compute shader.
///
/// # Design
/// Inspired by UE5 Niagara's module stack: each module declares its parameters
/// (for UI/serialization) and emits a self-contained HLSL function body.
/// The compositor concatenates snippets and wraps them in a dispatch kernel.
///
/// # Thread Safety
/// Not thread-safe. Modules are owned and invoked by the VFXSystem on the
/// main/render thread only.

#include "engine/math/vector3.h"
#include "engine/math/vector4.h"

#include <json.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace uz {

// ---------------------------------------------------------------------------
// ExecuteContext -- per-dispatch data passed to CPU-side module execution
// ---------------------------------------------------------------------------

/// Per-dispatch context passed to VFX modules during CPU-side execution.
///
/// Contains particle count, delta time, frame number, GPU buffer descriptor
/// indices (SRV/UAV), and a pointer to module-specific constant buffer data.
/// Valid only for the duration of `IVFXModule::Execute()` -- do not store.
struct ExecuteContext {
    uint32_t numParticles = 0;   ///< Active particles in this dispatch.
    float dt = 0.0f;             ///< Frame delta time in seconds (>= 0).
    uint32_t frameNumber = 0;    ///< Monotonic frame number (wraps at max).
    uint32_t positionsUav = 0;   ///< Bindless index: positions UAV.
    uint32_t velocitiesUav = 0;  ///< Bindless index: velocities UAV.
    uint32_t agesUav = 0;        ///< Bindless index: ages UAV.
    uint32_t lifespansSrv = 0;   ///< Bindless index: lifespans SRV (read-only).
    uint32_t colorsUav = 0;      ///< Bindless index: colors UAV.
    uint32_t sizesUav = 0;       ///< Bindless index: sizes UAV.
    const void* parameterData = nullptr; ///< Module-specific cbuffer data (may be null).
};

// ---------------------------------------------------------------------------
// ParameterType -- types supported by module parameters
// ---------------------------------------------------------------------------

/// Describes the data type of a VFX module parameter.
///
/// Used by the editor UI to generate appropriate widgets and by the
/// serialization system to read/write parameter values.
enum class ParameterType : uint8_t {
    Float,  ///< Single float scalar.
    Vec3,   ///< 3D float vector (position, direction, color RGB, etc.).
    Vec4,   ///< 4D float vector (RGBA color, quaternion, etc.).
    Int,    ///< 32-bit signed integer.
    Bool,   ///< Boolean (serialized as uint32_t in constant buffer).
    Curve,  ///< Reference to a VFXCurve asset (evaluated at particle age).
};

// ---------------------------------------------------------------------------
// ParameterDef -- module parameter declaration
// ---------------------------------------------------------------------------

/// Declares a single parameter exposed by a VFX module.
///
/// Modules return a vector of these from `GetParameters()` to describe
/// their configurable inputs. The editor uses this for UI generation,
/// and the serializer uses it to read/write parameter values.
///
/// Default values are stored in a union. Only the member matching `type`
/// is meaningful -- reading others is undefined behavior.
///
/// # Examples
/// ```cpp
/// ParameterDef def;
/// def.name = "Gravity";
/// def.type = ParameterType::Vec3;
/// def.defaultVec3 = Vec3{0.0f, 0.0f, -981.0f};
/// ```
struct ParameterDef {
    /// Display name shown in the editor (must be a string literal or
    /// pointer with static lifetime).
    const char* name = nullptr;

    /// Data type of this parameter.
    ParameterType type = ParameterType::Float;

    /// Default values -- only the field matching `type` is valid.
    union {
        float defaultFloat;
        int32_t defaultInt;
        bool defaultBool;
    };

    /// Default Vec3 value (used when `type == ParameterType::Vec3`).
    /// Stored outside the union because Vec3 has a non-trivial constructor.
    Vec3 defaultVec3{};

    /// Default Vec4 value (used when `type == ParameterType::Vec4`).
    Vec4 defaultVec4{};

    /// Initializes the union to zero.
    ParameterDef() : defaultFloat(0.0f) {}
};

// ---------------------------------------------------------------------------
// IVFXModule -- abstract interface for composable particle behavior
// ---------------------------------------------------------------------------

/// Abstract base for a composable VFX particle behavior module.
///
/// Each module encapsulates one aspect of particle simulation (e.g., gravity,
/// drag, color-over-life). Modules are created by `ModuleRegistry` and composed
/// into compute shaders by `ModuleCompositor`.
///
/// # Implementing a Module
/// 1. Override `GetName()` with a unique, stable identifier (e.g., "Gravity").
/// 2. Override `GetShaderCode()` returning an HLSL snippet that operates on
///    particle attributes via the standard UAV/SRV bindings.
/// 3. Override `GetParameters()` listing all configurable inputs.
/// 4. Override `Execute()` for any CPU-side dispatch prep (e.g., uploading
///    per-frame constants). Most modules leave this empty.
///
/// # Thread Safety
/// Not thread-safe. All methods called from the main/render thread.
class IVFXModule {
public:
    virtual ~IVFXModule() = default;

    /// Returns the unique name of this module (e.g., "Gravity", "ColorOverLife").
    ///
    /// The name is used for registration, serialization, and UI display.
    /// Must be a stable identifier -- changing it breaks saved assets.
    ///
    /// # Returns
    /// Null-terminated string with static lifetime.
    [[nodiscard]] virtual const char* GetName() const noexcept = 0;

    /// Returns the HLSL code snippet for this module's particle behavior.
    ///
    /// The snippet is stitched into a compute shader by `ModuleCompositor`.
    /// It may reference standard particle UAVs (positions, velocities, ages,
    /// colors, sizes) and the module's constant buffer via `cbModule`.
    ///
    /// # Returns
    /// Non-empty string view containing valid HLSL code.
    [[nodiscard]] virtual std::string_view GetShaderCode() const = 0;

    /// Returns the list of configurable parameters for this module.
    ///
    /// Used by the editor to generate UI widgets and by the serializer
    /// to persist parameter values. Order matters -- it defines the
    /// constant buffer layout.
    ///
    /// # Returns
    /// Vector of parameter definitions. May be empty if the module
    /// has no configurable inputs.
    [[nodiscard]] virtual std::vector<ParameterDef> GetParameters() const = 0;

    /// Applies configuration from a JSON object to the module's public fields.
    ///
    /// Called during asset loading when a "config" block is present for
    /// this module in the .uzfx file. Modules override this to map JSON
    /// keys to their public fields. The default implementation is a no-op
    /// for modules that have no configurable parameters.
    ///
    /// # Parameters
    /// - `config`: JSON object with key-value pairs matching parameter names.
    virtual void ApplyJsonConfig(const nlohmann::json& config) { (void)config; }

    /// CPU-side execution hook called before GPU dispatch.
    ///
    /// Modules use this to upload per-frame constants, update parameter
    /// data, or perform any CPU-side bookkeeping. Most simple modules
    /// (e.g., constant gravity) can leave the body empty.
    ///
    /// # Parameters
    /// - `ctx`: Dispatch context with particle count, dt, and GPU handles.
    ///   Valid only for the duration of this call.
    virtual void Execute(const ExecuteContext& ctx) = 0;

    // Non-copyable, movable.
    IVFXModule() = default;
    IVFXModule(const IVFXModule&) = delete;
    IVFXModule& operator=(const IVFXModule&) = delete;
    IVFXModule(IVFXModule&&) = default;
    IVFXModule& operator=(IVFXModule&&) = default;
};

} // namespace uz
