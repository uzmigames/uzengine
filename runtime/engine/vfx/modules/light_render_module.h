#pragma once

/// LightRenderModule -- particles that emit transient point lights.
///
/// Unlike compute-shader modules (gravity, turbulence, etc.), this module
/// does NOT contribute HLSL to the particle compute pass. Instead, after the
/// compute dispatch, the VFX system performs an async GPU->CPU readback of the
/// particle positions/colors buffer (one frame of latency) and creates
/// transient point lights in the light system for the closest N alive particles.
///
/// # Design Rationale
/// Placing light injection in a post-compute readback rather than a separate
/// GPU pass avoids the complexity of a second UAV and keeps the light system
/// decoupled from the VFX backend. The one-frame readback latency is
/// imperceptible for typical particle effects (fire, sparks, muzzle flash).
///
/// # Ownership
/// `LightRenderModule` is owned by the emitter's `EmitterModuleStack` via
/// `unique_ptr<IVFXModule>`. Readback buffers are managed externally by the
/// VFX system and passed into `ProcessReadback()` each frame.
///
/// # Thread Safety
/// Not thread-safe. All methods are called from the main/render thread only.

#include "vfx_module.h"

#include "engine/math/vector3.h"
#include "engine/math/vector4.h"

#include <cstdint>
#include <vector>
#include <json.hpp>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// LightParticleData -- per-particle light info for GPU->CPU readback
// ─────────────────────────────────────────────────────────────────────────────

/// Per-particle light entry read back from the GPU after the compute pass.
///
/// Mirrors the GPU-side particle attribute layout so the readback buffer can
/// be reinterpreted directly as an array of `LightParticleData` with no copy.
///
/// # Memory Layout
/// | Offset | Field    | Size |
/// |--------|----------|------|
/// | 0      | position | 12 B |
/// | 12     | _pad     |  4 B |  (implicit -- Vec3 is 12 B, Vec4 starts at 16)
/// | 16     | color    | 16 B |
///
/// Total: 32 bytes (aligned to 16 for GPU buffer compatibility).
///
/// The `color` field stores premultiplied RGBA: each channel is already
/// multiplied by the emitter's intensity so the light system needs no
/// additional scale.
struct LightParticleData {
    /// World-space position of the particle at the time of readback.
    Vec3 position;

    float _pad = 0.0f; ///< Explicit pad to 16-byte alignment for GPU/CPU ABI match.

    /// Premultiplied RGBA color (R, G, B, A). RGB is pre-scaled by intensity.
    Vec4 color;
};

// Compile-time size assertion: 32 bytes total.
static_assert(sizeof(LightParticleData) == 32,
              "LightParticleData must be 32 bytes for GPU readback ABI");

// ─────────────────────────────────────────────────────────────────────────────
// LightRenderModuleConfig -- configuration for particle-emitted lights
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the `LightRenderModule`.
///
/// All fields have sensible defaults suitable for a small flame or spark.
/// `maxLights` is clamped to [1, 256] by `ParseConfig()` to bound the
/// readback buffer size and light system overhead.
struct LightRenderModuleConfig {
    /// Attenuation radius of each particle point light, in world units (meters).
    /// Must be > 0. Default: 5.0 meters.
    float radius = 5.0f;

    /// Intensity multiplier applied to the particle color before light injection.
    /// Must be >= 0. Default: 1.0 (no scaling beyond the particle color).
    float intensity = 1.0f;

    /// Whether particle lights cast shadows.
    ///
    /// Shadow-casting lights are significantly more expensive. Enable only for
    /// hero particle effects where shadow fidelity justifies the cost.
    bool castShadow = false;

    /// Maximum simultaneous particle lights created per emitter per frame.
    ///
    /// Limits both the readback buffer size and the number of lights injected.
    /// Clamped to [1, 256] during config parsing.
    uint32_t maxLights = 64;
};

// ─────────────────────────────────────────────────────────────────────────────
// LightRenderModule -- particles that emit point lights
// ─────────────────────────────────────────────────────────────────────────────

/// Drives transient point light injection from particle positions and colors.
///
/// The module contributes no HLSL to the compute shader pass. Its role is
/// entirely post-compute: after the VFX system reads back particle data from
/// the GPU, it calls `ProcessReadback()` to obtain a list of `LightParticleData`
/// entries. The caller is responsible for injecting those into the light system.
///
/// # Typical Usage
/// ```cpp
/// // During system setup:
/// auto mod = std::make_unique<LightRenderModule>(config);
///
/// // After compute dispatch (one-frame latency readback):
/// const uint32_t bufSize = mod->GetReadbackBufferSize();
/// // ... GPU readback into readbackPtr ...
/// auto lights = mod->ProcessReadback(readbackPtr, bufSize, aliveCount);
/// for (const auto& light : lights) {
///     lightSystem.AddTransientPointLight(light.position,
///                                        light.color,
///                                        config.radius);
/// }
/// ```
///
/// # Notes
/// - `GetShaderCode()` intentionally returns an empty `std::string_view`.
///   The compositor must skip empty snippets rather than emitting blank lines.
/// - `GetParameters()` returns an empty vector -- configuration lives in
///   `LightRenderModuleConfig`, not in the constant-buffer parameter system.
/// - `Execute()` is a no-op: there is no per-frame CPU work before the dispatch.
class LightRenderModule final : public IVFXModule {
public:
    /// Constructs the module with the given configuration.
    ///
    /// # Parameters
    /// - `config`: Light emission configuration. Copied by value.
    explicit LightRenderModule(const LightRenderModuleConfig& config = {});
    ~LightRenderModule() override = default;

    // ── IVFXModule interface ────────────────────────────────────────────────

    /// Returns "light_render".
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns an empty string view -- this module contributes no compute HLSL.
    ///
    /// The `ModuleCompositor` must handle empty snippets gracefully.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns an empty parameter list -- configuration is via `LightRenderModuleConfig`.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op. Light injection happens in the post-compute readback phase, not here.
    void Execute(const ExecuteContext& ctx) override;

    // ── LightRenderModule-specific API ──────────────────────────────────────

    /// Returns the current configuration.
    ///
    /// # Returns
    /// Const reference valid for the lifetime of this module.
    [[nodiscard]] const LightRenderModuleConfig& GetConfig() const noexcept;

    /// Returns the size in bytes of the GPU readback buffer required by this module.
    ///
    /// Computed as `maxLights * sizeof(LightParticleData)`. Callers use this
    /// to allocate the readback buffer at emitter creation time and to validate
    /// the `dataSize` passed to `ProcessReadback()`.
    ///
    /// # Returns
    /// Buffer size in bytes. Always a multiple of 32.
    [[nodiscard]] uint32_t GetReadbackBufferSize() const noexcept;

    /// Extracts light data from a completed GPU->CPU readback buffer.
    ///
    /// Reads up to `min(aliveCount, config.maxLights)` `LightParticleData`
    /// entries from `readbackData`. Entries containing NaN or infinity in any
    /// position component are silently skipped to avoid injecting corrupt lights.
    ///
    /// # Parameters
    /// - `readbackData`: Pointer to the CPU-mapped readback buffer. Must not be
    ///                   null if `aliveCount > 0`. Contents are treated as a
    ///                   packed array of `LightParticleData` (32 bytes each).
    /// - `dataSize`:     Total byte size of `readbackData`. Used to guard against
    ///                   reading past the buffer. Must equal `GetReadbackBufferSize()`
    ///                   in normal operation; smaller values are tolerated but log
    ///                   a warning.
    /// - `aliveCount`:   Number of alive particles this frame (from the particle
    ///                   count buffer). Clamped to `config.maxLights` internally.
    ///
    /// # Returns
    /// Vector of validated `LightParticleData` entries ready for light injection.
    /// May be empty if `aliveCount == 0`, `readbackData == nullptr`, or all
    /// entries were filtered (NaN positions).
    ///
    /// # Side Effects
    /// Logs `VFX/Warning` if `dataSize < GetReadbackBufferSize()`.
    /// Logs `VFX/Warning` for each skipped NaN entry (throttled to first 4 per call).
    [[nodiscard]] std::vector<LightParticleData> ProcessReadback(
        const void* readbackData,
        uint32_t dataSize,
        uint32_t aliveCount) const;

    // ── JSON deserialization ─────────────────────────────────────────────────

    /// Parses a `LightRenderModuleConfig` from a JSON object.
    ///
    /// Recognized fields (all optional; missing fields use defaults):
    ///
    /// | JSON key      | C++ field          | Type   | Constraints           |
    /// |---------------|--------------------|--------|-----------------------|
    /// | "radius"      | `radius`           | float  | > 0; clamped to 0.01  |
    /// | "intensity"   | `intensity`        | float  | >= 0                  |
    /// | "cast_shadow" | `castShadow`       | bool   | —                     |
    /// | "max_lights"  | `maxLights`        | uint   | [1, 256]              |
    ///
    /// Invalid or out-of-range values log a `VFX/Warning` and fall back to
    /// the default for that field.
    ///
    /// # Parameters
    /// - `j`: JSON object containing light module configuration. Must be an object.
    ///
    /// # Returns
    /// Populated `LightRenderModuleConfig` with validated fields.
    [[nodiscard]] static LightRenderModuleConfig ParseConfig(const nlohmann::json& j);

private:
    /// Module configuration (radius, intensity, shadow, maxLights).
    LightRenderModuleConfig config_;
};

} // namespace uz
