/// UzEngine Pass Inspector — Visual GPU pipeline debugger.
///
/// Registers intermediate render targets from each pass and provides:
/// 1. `vis <target>` — fullscreen visualization of any registered target.
/// 2. `pass.validate` — readback-based anomaly detection (zeros, NaN, Inf).
/// 3. `pass.stats` — per-pass GPU timing and readback stats.
/// 4. `pass.dump` — full pipeline snapshot to PNG files.
/// 5. `pass.toggle` — enable/disable individual passes at runtime.
/// 6. `pass.compare` — side-by-side A/B comparison with/without a pass.
/// 7. `pass.grid` — NxM grid visualization of all render targets.
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
#include <unordered_set>
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

/// Channel selector for the pass-dump readback. Lets a single packed RT
/// be dumped as multiple distinct grayscale views (e.g. albedo.a -> AO,
/// normalRT.b -> metallic, normalRT.a -> roughness). When set to All the
/// dump writes the texture as RGBA verbatim (default).
enum class DumpChannel : uint8_t {
    All = 0,  ///< RGBA passthrough (default).
    R   = 1,  ///< Red only, replicated to RGB.
    G   = 2,  ///< Green only, replicated to RGB.
    B   = 3,  ///< Blue only, replicated to RGB.
    A   = 4,  ///< Alpha only, replicated to RGB.
};

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
    /// Pass-dump channel selector. All = full RGBA; R/G/B/A = single
    /// channel as grayscale (lets us split packed GBuffer RTs into
    /// "gbuffer.ao", "gbuffer.metallic", "gbuffer.roughness" views).
    DumpChannel      dumpChannel = DumpChannel::All;
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
// PassTimingEntry — per-pass GPU timing result
// ─────────────────────────────────────────────────────────────────────────────

/// GPU timing and readback stats for a single pass.
struct PassTimingEntry {
    std::string name;      ///< Pass name.
    float       gpuTimeMs; ///< GPU time in milliseconds (0 if timing unavailable).
    float       minVal;    ///< Min channel value from readback sampling.
    float       maxVal;    ///< Max channel value from readback sampling.
    float       avgVal;    ///< Average channel value from readback sampling.
};

// ─────────────────────────────────────────────────────────────────────────────
// PassGridConfig — layout configuration for pass.grid
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the NxM pass grid visualization.
struct PassGridConfig {
    uint32_t columns = 4;  ///< Number of columns in the grid.
    uint32_t rows    = 4;  ///< Number of rows in the grid.
};

// ─────────────────────────────────────────────────────────────────────────────
// DiagnosticReport — structured diagnostic output for automation
// ─────────────────────────────────────────────────────────────────────────────

/// Complete diagnostic report for one frame, serializable to JSON.
///
/// Designed for machine consumption by LLM agents. The `ToJSON()` method
/// produces a compact single-line JSON string with a well-defined schema.
/// Agents parse stdout for the `DIAG_JSON:` prefix followed by this JSON.
///
/// # Schema
/// ```json
/// {
///   "frame": <uint>,
///   "targetCount": <uint>,
///   "hasFailure": <bool>,    // true if any target has NaN or Inf
///   "hasWarning": <bool>,    // true if any target is all-zeros
///   "targets": [{ "name": <str>, "status": <str>, "min": <float>, "max": <float>, "avg": <float> }],
///   "timing": [{ "name": <str>, "gpuMs": <float> }]
/// }
/// ```
struct DiagnosticReport {
    uint32_t frameNumber = 0;              ///< Frame at which diagnostics were collected.
    uint32_t targetCount = 0;              ///< Number of registered render targets.
    std::vector<ValidationResult> targets; ///< Per-target validation results.
    std::vector<PassTimingEntry> timing;   ///< Per-pass GPU timing (may be empty).
    bool hasAnyFailure = false;            ///< True if any target has NaN or Inf.
    bool hasAnyWarning = false;            ///< True if any target is all-zeros.

    /// Formats the report as a JSON string.
    /// Output is a single object printed to one continuous string (no pretty-print).
    /// NaN and Inf float values are encoded as JSON strings ("NaN", "Inf")
    /// since JSON does not support these as numeric literals.
    [[nodiscard]] std::string ToJSON() const;
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
                        RHIResourceState state = RHIResourceState::ShaderResource,
                        DumpChannel dumpChannel = DumpChannel::All);

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

    /// Validates a single pass by name. Returns empty vector if not found.
    [[nodiscard]] std::vector<ValidationResult> ValidateByName(
        IRHIDevice* device, IRHICommandList* cmdList, const std::string& passName);

    /// Returns true if pass validation is enabled (from CLI `--pass-validate`).
    [[nodiscard]] bool IsValidationEnabled() const noexcept { return m_validationEnabled; }

    /// Enables or disables pass validation.
    void SetValidationEnabled(bool enabled) { m_validationEnabled = enabled; }

    // ── Diagnostic Report ────────────────────────────────────────────────

    /// Runs validation and generates a complete diagnostic report.
    ///
    /// This is the primary entry point for automated diagnostics (`--diag`).
    /// Runs `Validate()`, collects timing data, and packages everything
    /// into a `DiagnosticReport` that can be serialized to JSON via `ToJSON()`.
    ///
    /// # Arguments
    /// * `device` — RHI device for readback buffer creation and mapping.
    /// * `cmdList` — Command list in recording state for copy commands.
    /// * `frameNumber` — Current frame number (included in report).
    ///
    /// # Returns
    /// Complete `DiagnosticReport` with validation + timing data.
    [[nodiscard]] DiagnosticReport GenerateReport(IRHIDevice* device, IRHICommandList* cmdList,
                                                   uint32_t frameNumber);

    // ── Pass Toggle (Phase 4) ────────────────────────────────────────────

    /// Toggles a pass on or off. When disabled, the pass dispatch is skipped.
    ///
    /// The pass name uses a category prefix (e.g., "ssgi", "gtao", "ssr", "fog",
    /// "bloom", "contact_shadow", "aerial_persp", "tonemap", "taa", "motionblur",
    /// "dof", "fxaa", "cas", "dfs").
    ///
    /// # Arguments
    /// * `passName` — Category name of the pass to toggle.
    void TogglePass(const std::string& passName);

    /// Returns true if a pass category is enabled. Passes not explicitly disabled
    /// are considered enabled by default.
    [[nodiscard]] bool IsPassEnabled(const std::string& passName) const;

    /// Explicitly enables or disables a pass category.
    ///
    /// When `enabled` is false the pass name is added to the disabled set.
    /// When `enabled` is true the pass name is removed from the disabled set
    /// (default state — all passes start enabled).
    ///
    /// # Arguments
    /// * `passName` — Category name of the pass (e.g., "bloom", "fog", "taa").
    /// * `enabled`  — True to enable, false to disable.
    void SetPassEnabled(const std::string& passName, bool enabled);

    /// Returns the set of disabled pass names for serialization.
    [[nodiscard]] const std::unordered_set<std::string>& GetDisabledPasses() const noexcept {
        return m_disabledPasses;
    }

    // ── Pass Compare (Phase 4) ───────────────────────────────────────────

    /// Enters comparison mode for the given pass name.
    ///
    /// When active, the renderer captures two frames — one with the pass
    /// enabled, one with it disabled — and composites them side-by-side.
    /// The comparison pass name is stored until ClearCompare() is called.
    ///
    /// # Arguments
    /// * `passName` — Category name of the pass to compare.
    void SetComparePass(const std::string& passName);

    /// Exits comparison mode.
    void ClearCompare();

    /// Returns true if comparison mode is active.
    [[nodiscard]] bool IsCompareActive() const noexcept { return !m_comparePassName.empty(); }

    /// Returns the pass being compared, or empty string if not in compare mode.
    [[nodiscard]] const std::string& GetComparePassName() const noexcept { return m_comparePassName; }

    /// Returns the current compare phase: 0 = render with pass disabled,
    /// 1 = render with pass enabled, 2 = composite result.
    [[nodiscard]] uint32_t GetComparePhase() const noexcept { return m_comparePhase; }

    /// Advances the compare phase. Called by the renderer after each sub-frame.
    void AdvanceComparePhase() { ++m_comparePhase; }

    /// Resets compare phase to 0. Called when starting a new comparison.
    void ResetComparePhase() { m_comparePhase = 0; }

    // ── Pass Stats (Phase 2) ─────────────────────────────────────────────

    /// Stores per-pass timing data collected from the GPU profiler.
    ///
    /// Called by the renderer at the end of each frame with the resolved
    /// timing results from the backend's GPU profiler.
    void SetTimingData(const std::vector<PassTimingEntry>& entries);

    /// Returns the most recent per-pass timing data.
    [[nodiscard]] const std::vector<PassTimingEntry>& GetTimingData() const noexcept {
        return m_timingData;
    }

    /// Enables or disables the stats overlay.
    void SetStatsOverlayEnabled(bool enabled) { m_statsOverlayEnabled = enabled; }

    /// Returns true if the pass stats overlay should be rendered.
    [[nodiscard]] bool IsStatsOverlayEnabled() const noexcept { return m_statsOverlayEnabled; }

    // ── Pass Dump (Phase 3) ──────────────────────────────────────────────

    /// Requests a full pipeline dump at the specified frame.
    ///
    /// All registered targets are copied to staging buffers and saved as
    /// PNG files in `.screenshots/pass-dump-f{frame}/`.
    ///
    /// # Arguments
    /// * `frame` — Frame number at which to capture. UINT32_MAX = next frame.
    void RequestDump(uint32_t frame = UINT32_MAX);

    /// Returns true if a dump is pending for the current frame.
    [[nodiscard]] bool IsDumpPending(uint32_t currentFrame) const noexcept;

    /// Clears the dump request after execution.
    void ClearDumpRequest();

    /// Returns the frame number of the pending dump, or UINT32_MAX if none.
    [[nodiscard]] uint32_t GetDumpFrame() const noexcept { return m_dumpFrame; }

    /// Performs the dump: reads back all targets and saves PNGs + INDEX.html.
    ///
    /// The caller must ensure the GPU is idle before calling this (fence wait).
    ///
    /// # Arguments
    /// * `device` — RHI device for staging buffer creation and mapping.
    /// * `cmdList` — Command list for copy commands.
    /// * `frameNumber` — Current frame number (used in directory name).
    /// * `outputDir` — Base output directory (e.g., ".screenshots").
    void ExecuteDump(IRHIDevice* device, IRHICommandList* cmdList,
                     uint32_t frameNumber, const std::string& outputDir);

    // ── Pass Grid (Phase 5) ──────────────────────────────────────────────

    /// Enables or disables grid visualization mode.
    void SetGridEnabled(bool enabled) { m_gridEnabled = enabled; }

    /// Returns true if grid mode is active.
    [[nodiscard]] bool IsGridEnabled() const noexcept { return m_gridEnabled; }

    /// Sets the grid layout (columns x rows).
    void SetGridConfig(const PassGridConfig& config) { m_gridConfig = config; }

    /// Returns the current grid configuration.
    [[nodiscard]] const PassGridConfig& GetGridConfig() const noexcept { return m_gridConfig; }

    /// Sets extended grid mode (shows additional targets like bloom mips, SSGI intermediate).
    void SetGridExtended(bool extended) { m_gridExtended = extended; }

    /// Returns true if extended grid mode is active.
    [[nodiscard]] bool IsGridExtended() const noexcept { return m_gridExtended; }

    /// Returns the maximum number of grid cells for the current configuration.
    [[nodiscard]] uint32_t GetGridCellCount() const noexcept {
        return m_gridConfig.columns * m_gridConfig.rows;
    }

    /// Returns the list of target names to display in the grid.
    /// Up to GetGridCellCount() targets are returned, in registration order.
    [[nodiscard]] std::vector<const RegisteredTarget*> GetGridTargets() const;

private:
    std::vector<RegisteredTarget> m_targets;   ///< Targets registered this frame.
    std::string m_visTargetName;               ///< Currently selected vis target name (empty = off).
    bool m_validationEnabled = false;          ///< True if pass.validate is active.

    // ── Pass Toggle ──────────────────────────────────────────────────────
    std::unordered_set<std::string> m_disabledPasses;  ///< Pass categories that have been toggled off.

    // ── Pass Compare ─────────────────────────────────────────────────────
    std::string m_comparePassName;  ///< Pass being compared (empty = not comparing).
    uint32_t m_comparePhase = 0;    ///< 0 = disabled frame, 1 = enabled frame, 2 = composite.

    // ── Pass Stats ───────────────────────────────────────────────────────
    std::vector<PassTimingEntry> m_timingData;    ///< Most recent per-pass timing.
    bool m_statsOverlayEnabled = false;           ///< True if stats overlay should render.

    // ── Pass Dump ────────────────────────────────────────────────────────
    uint32_t m_dumpFrame = UINT32_MAX;   ///< Frame to dump (UINT32_MAX = none pending).
    bool m_dumpNextFrame = false;        ///< True if dump requested for "current/next frame".

    // ── Pass Grid ────────────────────────────────────────────────────────
    bool m_gridEnabled = false;        ///< True if grid mode is active.
    bool m_gridExtended = false;       ///< True if showing the extended 6x4 grid.
    PassGridConfig m_gridConfig;       ///< Grid layout configuration.
};

} // namespace uz
