#pragma once

/// VFX automatic thumbnail capture — headless snapshot on save.
///
/// When a VFX asset is saved, the editor triggers a headless render pass
/// that advances the effect to a configurable "peak frame" and captures a
/// PNG thumbnail. The asset browser loads the thumbnail at display time,
/// giving authors a visual preview without opening the full editor.
///
/// # Capture pipeline
///
/// 1. `VfxThumbnailCapturer::PrepareCapture(config)` validates config and
///    computes the target simulation time (`captureFrame / fps`).
/// 2. The caller advances the VFX simulation headless in a loop until
///    `IsReadyToCapture()` returns true (the simulation clock >= target).
/// 3. The caller snapshots the current frame via the render pipeline
///    (e.g. `--screenshot-stdout` or an off-screen RTV readback).
/// 4. `FinishCapture(pngBytes)` writes the thumbnail to the asset-derived
///    path (`fireball.uzvfxsys` → `fireball.thumb.png`).
///
/// # On-save hook pattern
///
/// The editor save handler should call:
/// ```cpp
/// VfxThumbnailConfig cfg;
/// cfg.assetPath = savedAssetPath;
/// auto capturer = VfxThumbnailCapturer();
/// capturer.PrepareCapture(cfg);
/// while (!capturer.IsReadyToCapture()) {
///     vfxSystem.Tick(capturer.GetStepDt());
///     capturer.AdvanceFrame();
/// }
/// auto png = renderer.CaptureFramePNG(capturer.GetResolution());
/// capturer.FinishCapture(png, cfg);
/// ```
///
/// # Path convention
///
/// `VfxThumbnailPathForAsset("vfx/fireball.uzvfxsys")` returns
/// `"vfx/fireball.thumb.png"`. The `.thumb.png` suffix is a stable
/// convention the browser resolver uses to locate thumbnails.
///
/// @ref UE5 UNiagaraThumbnailRenderer
/// @ref docs/engine/25-vfx/24-thumbnail-capture.md

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Path resolver
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the thumbnail path for a VFX asset path.
///
/// Replaces the file extension with `.thumb.png`. Handles any extension
/// (`.uzvfxsys`, `.uzfx`, `.uzvfxgraph`, etc.).
///
/// # Examples
/// ```
/// "vfx/fireball.uzvfxsys"  → "vfx/fireball.thumb.png"
/// "effects/smoke.uzfx"      → "effects/smoke.thumb.png"
/// "fire"                    → "fire.thumb.png"
/// ```
[[nodiscard]] std::string VfxThumbnailPathForAsset(const std::string& assetPath);

// ─────────────────────────────────────────────────────────────────────────────
// Capture config
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for a single thumbnail capture.
struct VfxThumbnailConfig {
    /// Source asset path (used to derive the output thumbnail path).
    std::string assetPath;

    /// Thumbnail pixel dimensions.
    uint32_t width  = 128u;
    uint32_t height = 128u;

    /// Simulation frames per second. Determines the fixed dt used when
    /// advancing the effect to the capture frame. Must be > 0.
    float fps = 60.0f;

    /// Frame index at which to capture. The effect is advanced for this
    /// many fixed-dt ticks before the snapshot. Choosing a "peak" frame
    /// (e.g. frame 15 of a 30-frame explosion) produces the most
    /// recognisable thumbnail.
    uint32_t captureFrame = 15u;

    /// Maximum number of simulation ticks the capturer will advance.
    /// Guards against infinite loops on effects with burst-only spawning
    /// (zero alive at the target frame). When hit, the capture is taken
    /// at whatever the current state is.
    uint32_t maxTicks = 300u;

    /// Optional fixed random seed for the VFX system. When non-zero the
    /// capturer requests the VFX system to use this seed, making the
    /// capture deterministic across saves.
    uint32_t randomSeed = 42u;
};

// ─────────────────────────────────────────────────────────────────────────────
// Capture result
// ─────────────────────────────────────────────────────────────────────────────

/// Outcome of a thumbnail capture.
struct VfxThumbnailResult {
    bool        success     = false;   ///< True iff the PNG was written.
    std::string outputPath;            ///< Final thumbnail path on disk.
    uint32_t    framesCapturedAt = 0u; ///< Actual tick at which snapshot was taken.
};

// ─────────────────────────────────────────────────────────────────────────────
// Capturer state machine
// ─────────────────────────────────────────────────────────────────────────────

/// Orchestrates a single headless thumbnail capture for one VFX asset.
///
/// The class is stateful but lightweight — one instance per capture, then
/// discard. No GPU resources are owned: the caller drives the render
/// pipeline and hands the captured PNG data back into `FinishCapture`.
class VfxThumbnailCapturer {
public:
    VfxThumbnailCapturer() = default;

    /// Validates config and initialises the state machine. Must be called
    /// before `AdvanceFrame` / `IsReadyToCapture`.
    ///
    /// # Returns
    /// `true` when the config is valid (fps > 0, positive resolution).
    /// `false` with a `VFX/Error` log otherwise.
    [[nodiscard]] bool PrepareCapture(const VfxThumbnailConfig& cfg);

    /// Returns the fixed dt the caller should feed into `VFXSystem::Tick`
    /// each frame during the headless advance loop.
    [[nodiscard]] float GetStepDt() const noexcept { return stepDt_; }

    /// Returns the configured capture resolution.
    [[nodiscard]] uint32_t GetWidth() const noexcept { return config_.width; }
    [[nodiscard]] uint32_t GetHeight() const noexcept { return config_.height; }

    /// Returns the configured random seed (0 = no seed override).
    [[nodiscard]] uint32_t GetRandomSeed() const noexcept { return config_.randomSeed; }

    /// Returns the current tick count (incremented by AdvanceFrame).
    [[nodiscard]] uint32_t GetCurrentTick() const noexcept { return currentTick_; }

    /// Call once per tick after `VFXSystem::Tick(GetStepDt())`.
    void AdvanceFrame() noexcept;

    /// Returns true when the simulation has reached the target frame or
    /// the max-tick safety cap — the caller should capture now.
    [[nodiscard]] bool IsReadyToCapture() const noexcept { return ready_; }

    /// Writes `pngData` to the `.thumb.png` path derived from `config`.
    ///
    /// # Arguments
    /// - `pngData` — raw PNG bytes (IHDR…IEND) produced by the renderer.
    ///
    /// # Returns
    /// A result indicating success/failure and the output path used.
    [[nodiscard]] VfxThumbnailResult FinishCapture(
        const std::vector<uint8_t>& pngData) const;

private:
    VfxThumbnailConfig config_{};
    float    stepDt_     = 0.0f;
    uint32_t currentTick_= 0u;
    bool     ready_      = false;
    bool     prepared_   = false;
};

} // namespace uz
