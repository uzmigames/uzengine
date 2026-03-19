/// UzEngine Pass Inspector — Visual GPU pipeline debugger.
///
/// Registers intermediate render targets from each pass and provides:
/// 1. `vis <target>` — fullscreen visualization of any registered target.
/// 2. `pass.validate` — readback-based anomaly detection (zeros, NaN, Inf).
///
/// # Integration
/// Each pass in frame_lighting.cpp / frame_postprocess.cpp calls
/// `RegisterTarget()` after dispatch. At the end of the post-process chain,
/// if `IsVisActive()`, the vis fullscreen pass replaces final output.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.
///
/// # Lifecycle
/// Created by SceneRenderer. `BeginFrame()` resets per-frame registrations.
/// Targets persist only for the current frame.

#pragma once

#include "rhi_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;

// ─────────────────────────────────────────────────────────────────────────────
// DisplayMode — how to interpret the texture data for visualization
// ─────────────────────────────────────────────────────────────────────────────

/// Controls how the vis fullscreen shader interprets and displays a texture.
enum class VisDisplayMode : uint32_t {
    Color     = 0,  ///< RGBA direct display (sRGB output).
    Grayscale = 1,  ///< Single channel as (R,R,R,1).
    Depth     = 2,  ///< Linearize reversed-Z depth for display.
    HDR       = 3,  ///< False-color for values > 1.0.
    Normal    = 4,  ///< Decode octahedral normal, map [-1,1] to [0,1].
    Velocity  = 5,  ///< Motion vectors as color (RG → RGB).
};

// ─────────────────────────────────────────────────────────────────────────────
// RegisteredTarget — metadata for a single intermediate render target
// ─────────────────────────────────────────────────────────────────────────────

/// Describes a single registered intermediate render target.
struct RegisteredTarget {
    std::string      name;      ///< Dotted name (e.g., "gbuffer.albedo", "ssgi.trace").
    RHITextureHandle texture;   ///< GPU texture handle.
    uint32_t         srvIndex;  ///< Bindless SRV heap index.
    VisDisplayMode   mode;      ///< How to visualize this target.
    uint32_t         width;     ///< Texture width in texels.
    uint32_t         height;    ///< Texture height in texels.
    /// Resource state the texture is expected to be in after the frame ends.
    /// Used by Validate() to issue correct barrier transitions for readback.
    /// Defaults to ShaderResource since most targets end the frame as SRV.
    RHIResourceState lastState = RHIResourceState::ShaderResource;
};

// ─────────────────────────────────────────────────────────────────────────────
// ValidationResult — per-target anomaly detection result
// ─────────────────────────────────────────────────────────────────────────────

/// Result of readback-based validation for a single registered target.
struct ValidationResult {
    std::string name;      ///< Target name.
    bool        isZero;    ///< True if all sampled texels are zero.
    bool        hasNaN;    ///< True if any sampled texel contains NaN.
    bool        hasInf;    ///< True if any sampled texel contains Inf.
    float       minVal;    ///< Minimum value across all sampled channels.
    float       maxVal;    ///< Maximum value across all sampled channels.
    float       avgVal;    ///< Average value across all sampled channels.
};

// ─────────────────────────────────────────────────────────────────────────────
// PassInspector
// ─────────────────────────────────────────────────────────────────────────────

/// Visual GPU pipeline debugger that can visualize any intermediate render target.
///
/// # Usage in the render loop
/// ```cpp
/// // At start of frame:
/// passInspector.BeginFrame();
///
/// // After each pass dispatch:
/// passInspector.RegisterTarget("gbuffer.albedo", tex, srv, VisDisplayMode::Color, w, h);
///
/// // After tonemap, before present:
/// if (passInspector.IsVisActive()) {
///     // Render vis fullscreen pass instead of normal output
/// }
/// ```
class PassInspector {
public:
    PassInspector() = default;
    ~PassInspector() = default;

    PassInspector(const PassInspector&) = delete;
    PassInspector& operator=(const PassInspector&) = delete;
    PassInspector(PassInspector&&) noexcept = default;
    PassInspector& operator=(PassInspector&&) noexcept = default;

    // ── Per-frame lifecycle ──────────────────────────────────────────────

    /// Clears all registered targets for the new frame.
    /// Must be called at the start of each `RenderFrame()`.
    void BeginFrame();

    // ── Target registration ──────────────────────────────────────────────

    /// Registers an intermediate render target for visualization and validation.
    ///
    /// Called by each pass after dispatch. The texture must be in SRV-readable
    /// state (or will be transitioned by the render graph before the vis pass).
    ///
    /// # Arguments
    /// * `name` — Dotted identifier (e.g., "gbuffer.albedo", "ssgi.trace").
    /// * `texture` — GPU texture handle. Must be valid.
    /// * `srvIndex` — Bindless SRV heap index for the texture.
    /// * `mode` — Display mode for visualization.
    /// * `width` — Texture width in texels.
    /// * `height` — Texture height in texels.
    /// * `state` — Resource state the texture is expected to be in at frame end.
    ///   Defaults to ShaderResource. Must be accurate for Validate() readback.
    void RegisterTarget(const char* name, RHITextureHandle texture, uint32_t srvIndex,
                        VisDisplayMode mode, uint32_t width, uint32_t height,
                        RHIResourceState state = RHIResourceState::ShaderResource);

    // ── Visualization control ────────────────────────────────────────────

    /// Sets the visualization target by name (from CLI `--vis` or console command).
    ///
    /// The name must match a registered target exactly (case-sensitive).
    /// If no target with this name is registered, visualization is silently
    /// deferred until the name becomes available.
    ///
    /// # Arguments
    /// * `target` — Target name (e.g., "ssgi.trace"). Empty string disables vis.
    void SetVisTarget(const char* target);

    /// Disables visualization — resumes normal rendering output.
    void ClearVis();

    /// Returns true if visualization is active (a target name is set).
    [[nodiscard]] bool IsVisActive() const noexcept { return !m_visTargetName.empty(); }

    /// Returns the currently selected visualization target, or nullptr if none
    /// matches a registered target this frame.
    [[nodiscard]] const RegisteredTarget* GetVisTarget() const noexcept;

    /// Returns the visualization target name (may not match any registered target).
    [[nodiscard]] const std::string& GetVisTargetName() const noexcept { return m_visTargetName; }

    // ── Target listing ──────────────────────────────────────────────────

    /// Returns all targets registered this frame.
    [[nodiscard]] const std::vector<RegisteredTarget>& GetTargets() const noexcept { return m_targets; }

    // ── Validation ──────────────────────────────────────────────────────

    /// Performs readback-based validation on all registered targets.
    ///
    /// For each target, copies a 4x4 texel block from the texture center
    /// to a staging buffer, maps it, and computes min/max/avg + checks for
    /// zeros, NaN, and Inf.
    ///
    /// The caller must ensure the GPU is idle before calling this (fence wait).
    ///
    /// # Arguments
    /// * `device` — RHI device for staging buffer creation and mapping.
    /// * `cmdList` — Command list in recording state for copy commands.
    ///
    /// # Returns
    /// One ValidationResult per registered target.
    [[nodiscard]] std::vector<ValidationResult> Validate(IRHIDevice* device, IRHICommandList* cmdList);

    /// Returns true if pass validation is enabled (from CLI `--pass-validate`).
    [[nodiscard]] bool IsValidationEnabled() const noexcept { return m_validationEnabled; }

    /// Enables or disables pass validation.
    void SetValidationEnabled(bool enabled) { m_validationEnabled = enabled; }

private:
    std::vector<RegisteredTarget> m_targets;   ///< Targets registered this frame.
    std::string m_visTargetName;               ///< Currently selected vis target name (empty = off).
    bool m_validationEnabled = false;          ///< True if pass.validate is active.
};

} // namespace uz
