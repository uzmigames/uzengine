#pragma once

/// VFX thumbnail save-pipeline extension — queues headless thumbnail
/// captures on every PostSave of a VFX asset (phase 9.7.0).
///
/// # Architecture
///
/// The extension itself does NOT own GPU resources or drive a render
/// loop — it enqueues a `VfxThumbnailCaptureRequest` into a shared
/// `VfxThumbnailCaptureQueue`. The application's render loop drains the
/// queue each frame, using `VfxThumbnailCapturer` (phase 8.7.0) for
/// timing and `HeadlessFrameCapture` (phase 10.9.0) for GPU readback.
///
/// This decoupling keeps PostSave non-blocking (no GPU stall) and lets
/// the render loop manage device / command-list lifetimes.
///
/// # Registration
///
/// ```cpp
/// // At editor startup:
/// VfxThumbnailSaveExtension ext(&captureQueue);
/// EditorSavePipeline::Instance().Register(&ext);
/// ```
///
/// # Queue drain (render loop):
///
/// ```cpp
/// while (auto* req = captureQueue.Peek()) {
///     // advance VFX sim + render headless + FinishCapture...
///     captureQueue.Pop();
/// }
/// ```
///
/// @ref engine/vfx/baker/vfx_thumbnail.h      (capturer state machine)
/// @ref engine/vfx/editor/vfx_editor_save_pipeline.h (extension base)
/// @ref engine/renderer/headless/headless_frame_capture.h (GPU readback)
/// @ref docs/engine/25-vfx/24-thumbnail-capture.md

#include "engine/vfx/editor/vfx_editor_save_pipeline.h"
#include "engine/vfx/baker/vfx_thumbnail.h"

#include <mutex>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Capture request + queue
// ─────────────────────────────────────────────────────────────────────────────

/// A pending thumbnail capture request. Created by the save extension,
/// consumed by the render loop.
struct VfxThumbnailCaptureRequest {
    /// Config for the capturer (assetPath, resolution, seed, etc.).
    VfxThumbnailConfig config;

    /// The asset kind that was saved (for filtering / logging).
    VfxSaveAssetKind kind = VfxSaveAssetKind::Unknown;
};

/// Thread-safe FIFO queue of pending thumbnail captures.
///
/// PostSave (editor thread) enqueues; the render loop dequeues. The
/// queue is intentionally simple — at most one or two requests are
/// pending at any time (saves are not frequent).
class VfxThumbnailCaptureQueue {
public:
    /// Pushes a new capture request. Thread-safe.
    void Push(VfxThumbnailCaptureRequest req);

    /// Returns a pointer to the front request, or nullptr when empty.
    /// The pointer is valid until the next `Pop()` or `Push()`.
    /// NOT thread-safe against concurrent Push — call from the render
    /// loop only (the only concurrent writer is PostSave, which is
    /// serialised by the editor thread).
    [[nodiscard]] const VfxThumbnailCaptureRequest* Peek() const;

    /// Removes the front request. No-op when empty.
    void Pop();

    /// Returns the number of pending requests.
    [[nodiscard]] uint32_t Size() const;

    /// Discards all pending requests.
    void Clear();

private:
    mutable std::mutex mutex_;
    std::vector<VfxThumbnailCaptureRequest> queue_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Save extension
// ─────────────────────────────────────────────────────────────────────────────

/// IEditorSaveExtension that enqueues a thumbnail capture request after
/// every VFX asset save.
///
/// Only fires for asset kinds that have thumbnailable content:
/// `EmitterPreset`, `VfxSystem`, `NodeGraph`. Other kinds (Snapshot,
/// BakeAsset, etc.) are skipped silently.
class VfxThumbnailSaveExtension final : public IEditorSaveExtension {
public:
    /// Constructs the extension.
    ///
    /// # Arguments
    /// - `queue` — Shared capture queue. Must outlive the extension.
    ///   Passing null disables enqueuing (useful for tests that only
    ///   want to verify PostSave filtering).
    explicit VfxThumbnailSaveExtension(VfxThumbnailCaptureQueue* queue);

    // ── IEditorSaveExtension overrides ──────────────────────────────────

    /// PostSave enqueues a capture request for thumbnailable asset kinds.
    void PostSave(VfxSaveAssetKind kind,
                  const void* asset,
                  const std::string& outputPath) override;

    [[nodiscard]] const char* Name() const noexcept override {
        return "ThumbnailCapture";
    }

    // ── Configuration ───────────────────────────────────────────────────

    /// Sets the capture config template. Only `assetPath` is overridden
    /// per-save; all other fields (resolution, fps, seed) are reused.
    void SetDefaultConfig(const VfxThumbnailConfig& cfg) { defaultConfig_ = cfg; }

    /// Returns the default config (for inspection / tests).
    [[nodiscard]] const VfxThumbnailConfig& GetDefaultConfig() const { return defaultConfig_; }

    /// Returns the number of capture requests enqueued since construction
    /// (or last `ResetStats()`). Useful for tests.
    [[nodiscard]] uint32_t GetEnqueueCount() const noexcept { return enqueueCount_; }

    /// Resets the enqueue counter to zero.
    void ResetStats() noexcept { enqueueCount_ = 0; }

private:
    VfxThumbnailCaptureQueue* queue_ = nullptr;
    VfxThumbnailConfig defaultConfig_{};
    uint32_t enqueueCount_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Convenience: check if an asset kind is thumbnailable
// ─────────────────────────────────────────────────────────────────────────────

/// Returns true for asset kinds that produce visible particles and thus
/// benefit from thumbnail capture.
[[nodiscard]] inline bool IsVfxAssetKindThumbnailable(VfxSaveAssetKind kind) noexcept {
    return kind == VfxSaveAssetKind::EmitterPreset
        || kind == VfxSaveAssetKind::VfxSystem
        || kind == VfxSaveAssetKind::NodeGraph;
}

} // namespace uz
