#pragma once

/// VFX Collision Module -- depth-buffer particle collision and response.
///
/// Detects when GPU particles penetrate the scene depth buffer and applies one
/// of three configurable responses: Kill (particle dies on contact), Bounce
/// (velocity reflected off the estimated surface normal), or Slide (velocity
/// projected onto the surface plane).
///
/// The HLSL snippet projects each particle's world-space position into screen
/// space, samples the depth buffer at the resulting UV, and compares the
/// particle depth against the scene depth. When the particle is at or behind
/// the surface (within `collisionOffset`), the response is applied.
///
/// Surface normals are estimated from the depth buffer gradient using a
/// cross-product of the horizontal and vertical tangents reconstructed via
/// inverse view-projection. This avoids a separate normal GBuffer read.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.

#include "vfx_module.h"

#include <string>
#include <json.hpp>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// CollisionResponse -- how particles react on surface contact
// ─────────────────────────────────────────────────────────────────────────────

/// Describes the particle response applied when depth-buffer collision is detected.
///
/// - `Kill`   removes the particle immediately (e.g., rain on a window).
/// - `Bounce` reflects velocity off the estimated surface normal and scales it
///            by `CollisionModuleConfig::bounceFactor` (e.g., sparks, pebbles).
/// - `Slide`  projects velocity onto the surface plane so the particle glides
///            along the surface (e.g., snow accumulating, water running).
enum class CollisionResponse : uint8_t {
    Kill   = 0, ///< Particle dies on contact -- age is set to lifespan.
    Bounce = 1, ///< Velocity reflected off the surface normal × bounceFactor.
    Slide  = 2, ///< Velocity projected onto the surface plane.
};

/// Converts a CollisionResponse enum value to its canonical string name.
///
/// # Returns
/// - `"kill"`, `"bounce"`, or `"slide"` for valid values.
/// - `"unknown"` for any value not defined in the enum.
[[nodiscard]] const char* CollisionResponseToString(CollisionResponse response) noexcept;

/// Parses a CollisionResponse from its string name.
///
/// Case-insensitive comparison against `"kill"`, `"bounce"`, and `"slide"`.
/// Falls back to `CollisionResponse::Bounce` with a VFX warning log for
/// unrecognized strings.
///
/// # Parameters
/// - `str`: One of `"kill"`, `"bounce"`, `"slide"` (case-insensitive).
///
/// # Returns
/// The matching `CollisionResponse`, or `Bounce` if the string is unrecognized.
[[nodiscard]] CollisionResponse CollisionResponseFromString(const std::string& str);

// ─────────────────────────────────────────────────────────────────────────────
// CollisionModuleConfig -- configuration for depth-buffer particle collision
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration data for `CollisionModule`.
///
/// All fields map directly to HLSL constant buffer entries uploaded each frame.
/// Defaults produce a realistic bounce response suitable for small debris particles.
///
/// | Field                  | Default | Description                                   |
/// |------------------------|---------|-----------------------------------------------|
/// | response               | Bounce  | How particles react on surface contact         |
/// | bounceFactor           | 0.6     | Velocity retention after bounce [0, 1]         |
/// | depthBufferCollision   | true    | Enable depth-buffer sampling (disable to debug)|
/// | collisionOffset        | 1.0     | Surface standoff to prevent z-fighting (cm)    |
/// | minBounceSpeed         | 10.0    | Below this speed (cm/s), Kill instead of Bounce|
struct CollisionModuleConfig {
    /// How particles react on surface contact.
    CollisionResponse response = CollisionResponse::Bounce;

    /// Fraction of pre-collision speed retained after a bounce [0, 1].
    /// 0 = completely inelastic (particle stops), 1 = perfectly elastic.
    float bounceFactor = 0.6f;

    /// When true the HLSL snippet samples the depth buffer each frame.
    /// Set to false only for debugging (particles pass through all geometry).
    bool depthBufferCollision = true;

    /// World-space distance (cm) a particle must penetrate before collision
    /// triggers. Small positive values prevent persistent z-fighting artefacts
    /// where the particle hovers exactly on the surface.
    float collisionOffset = 1.0f;

    /// Speed threshold (cm/s) below which Bounce falls back to Kill.
    /// Prevents particles from jittering endlessly at low energy.
    float minBounceSpeed = 10.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// CollisionModule -- depth-buffer collision for GPU particles
// ─────────────────────────────────────────────────────────────────────────────

/// Depth-buffer collision detection and response module for GPU particles.
///
/// Implements `IVFXModule`. `GetShaderCode()` returns an HLSL snippet that the
/// `ModuleCompositor` stitches into the particle update compute shader. The
/// snippet:
///   1. Projects the particle's world position into screen space using
///      `cbViewProj` from the per-frame constant buffer.
///   2. Samples `DepthBuffer` at the resulting screen UV via bilinear filtering.
///   3. Reconstructs the scene position from the depth sample and compares it
///      against the particle position (with `cbCollisionOffset` as a standoff).
///   4. On collision: applies the configured `CollisionResponse`:
///      - `Kill`   sets age to lifespan so DecayModule recycles the particle.
///      - `Bounce` reflects velocity with `ReflectVelocity()` and re-checks
///                 `minBounceSpeed` to avoid infinite micro-bounces.
///      - `Slide`  projects velocity onto the estimated surface plane with
///                 `ProjectOntoPlane()`.
///
/// Surface normals are estimated from the depth buffer gradient via the
/// `EstimateSurfaceNormal()` helper in `vfx_collision.hlsli`.
///
/// # HLSL Dependencies
/// The snippet assumes these names are accessible in the surrounding shader:
/// - `Positions[particleIdx]`   : `RWStructuredBuffer<float3>`
/// - `Velocities[particleIdx]`  : `RWStructuredBuffer<float3>`
/// - `Ages[particleIdx]`        : `RWStructuredBuffer<float>`
/// - `Lifespans[particleIdx]`   : `StructuredBuffer<float>`   (read-only)
/// - `cbViewProj`               : `float4x4`  view-projection matrix
/// - `cbInvViewProj`            : `float4x4`  inverse view-projection
/// - `cbScreenDimensions`       : `float2`    render target size in pixels
/// - `cbCollisionResponse`      : `uint`      0=Kill, 1=Bounce, 2=Slide
/// - `cbBounceFactor`           : `float`
/// - `cbMinBounceSpeed`         : `float`
/// - `cbCollisionOffset`        : `float`
/// - `cbCollisionEnabled`       : `uint`      0=disabled, 1=enabled
/// - `DepthBuffer`              : `Texture2D<float>` scene depth
/// - `LinearClampSampler`       : `SamplerState`
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.
///
/// # Example
/// ```cpp
/// CollisionModuleConfig cfg;
/// cfg.response      = CollisionResponse::Bounce;
/// cfg.bounceFactor  = 0.4f;
/// cfg.minBounceSpeed = 0.05f;
/// auto module = std::make_unique<CollisionModule>(cfg);
/// stack.Push(std::move(module));
/// ```
class CollisionModule final : public IVFXModule {
public:
    /// Default-constructs the module with `CollisionModuleConfig` defaults.
    ///
    /// Equivalent to `CollisionModule(CollisionModuleConfig{})`. Required for
    /// `ModuleRegistry::Register<CollisionModule>()` which instantiates modules
    /// via default construction before JSON config is applied at load time.
    CollisionModule();

    /// Constructs the module with the given configuration.
    ///
    /// # Parameters
    /// - `config`: Collision parameters. Copied into the module; changes to
    ///   the original struct after construction have no effect.
    explicit CollisionModule(const CollisionModuleConfig& config);

    ~CollisionModule() override = default;

    // -- IVFXModule interface -------------------------------------------------

    /// Returns the HLSL snippet for depth-buffer collision detection and response.
    ///
    /// The returned string_view points to a `static constexpr` buffer with
    /// program lifetime -- it is safe to keep beyond the module's lifetime.
    ///
    /// # Returns
    /// Non-empty HLSL source text. Never null.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns `"collision"`.
    ///
    /// This name is used for module registration, serialization, and UI display.
    /// It is stable -- renaming it would break saved VFX assets.
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns parameter definitions for collision configuration.
    ///
    /// Exposes: response (Int), bounce_factor (Float), depth_buffer_collision
    /// (Bool), collision_offset (Float), min_bounce_speed (Float).
    ///
    /// # Returns
    /// Vector of five `ParameterDef` entries. Never empty.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op. All collision logic runs entirely on the GPU.
    ///
    /// A future implementation might upload per-frame cbuffer data here;
    /// for now the compositor is responsible for binding collision constants.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration access -------------------------------------------------

    /// Returns the current collision configuration.
    ///
    /// The reference is valid for the module's lifetime.
    [[nodiscard]] const CollisionModuleConfig& GetConfig() const noexcept;

    // -- JSON deserialization -------------------------------------------------

    /// Parses a `CollisionModuleConfig` from a JSON object.
    ///
    /// Accepted keys (all optional -- missing keys use config defaults):
    /// | Key                       | Type   | Description                      |
    /// |---------------------------|--------|----------------------------------|
    /// | `"response"`              | string | `"kill"`, `"bounce"`, `"slide"`  |
    /// | `"bounce_factor"`         | float  | [0, 1]                           |
    /// | `"depth_buffer_collision"`| bool   |                                  |
    /// | `"collision_offset"`      | float  | metres, > 0 recommended          |
    /// | `"min_bounce_speed"`      | float  | m/s, >= 0                        |
    ///
    /// # Parameters
    /// - `j`: A `nlohmann::json` object (not array, not null). Passing a
    ///   non-object value logs a VFX warning and returns defaults.
    ///
    /// # Returns
    /// A fully populated `CollisionModuleConfig`. Unknown keys are silently
    /// ignored; out-of-range values are clamped with a VFX warning log.
    [[nodiscard]] static CollisionModuleConfig ParseConfig(const nlohmann::json& j);

private:
    CollisionModuleConfig config_;
};

} // namespace uz
