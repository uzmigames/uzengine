#pragma once

/// VFX thumbnail queue drainer — viewport-loop state machine.
///
/// Phase 11.12.0. Bridges the phase 9.7 `VfxThumbnailCaptureQueue`
/// (thread-safe FIFO produced by PostSave) with the phase 8.7
/// `VfxThumbnailCapturer` (stateful simulation driver) and the host
/// application's render loop (that owns the GPU device / command list).
///
/// The drainer itself contains no GPU code; it issues callbacks the
/// host wires to render + readback. Keeping the logic pure-data lets
/// us unit-test the full state machine without booting a RHI.
///
/// # State machine
///
/// ```
///   Idle
///    │ queue non-empty
///    ▼
///   Preparing              (allocates capturer, fires onBeginCapture)
///    │ each Tick() until IsReadyToCapture()
///    ▼
///   Capturing              (fires onCapturePng; host renders + reads back)
///    │ PNG bytes arrive
///    ▼
///   Finishing              (writes PNG via FinishCapture, pops queue)
///    │ Tick()
///    ▼
///   Idle
/// ```
///
/// # Host integration
///
/// ```cpp
/// VfxThumbnailDrainer drainer;
/// drainer.Bind(&captureQueue);
///
/// drainer.onBeginCapture = [&] {
///     savedShowOverlay = overlay.IsVisible();
///     overlay.SetVisible(false);
///     savedShowTimeline = timeline.IsVisible();
///     timeline.SetVisible(false);
/// };
/// drainer.onCapturePng = [&](uint32_t w, uint32_t h) -> std::vector<uint8_t> {
///     return headlessCapture.RenderAndReadbackPng(w, h);
/// };
/// drainer.onEndCapture = [&] {
///     overlay.SetVisible(savedShowOverlay);
///     timeline.SetVisible(savedShowTimeline);
/// };
///
/// // Each frame:
/// drainer.Tick(simDt);
/// ```
///
/// @ref engine/vfx/editor/vfx_thumbnail_save_ext.h (queue + save hook)
/// @ref engine/vfx/baker/vfx_thumbnail.h            (capturer state)
/// @ref docs/engine/25-vfx/24-thumbnail-capture.md  (pipeline overview)

#include "engine/vfx/baker/vfx_thumbnail.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace uz {

class VfxThumbnailCaptureQueue;
struct VfxThumbnailCaptureRequest;

/// Drainer state — matches the ASCII diagram in the header comment.
enum class VfxThumbnailDrainState : uint8_t {
    Idle      = 0,
    Preparing = 1,
    Capturing = 2,
    Finishing = 3,
};

/// Stable name for state — for logs and diagnostics. Never null.
[[nodiscard]] constexpr const char* VfxThumbnailDrainStateName(
    VfxThumbnailDrainState s) noexcept {
    switch (s) {
        case VfxThumbnailDrainState::Idle:      return "Idle";
        case VfxThumbnailDrainState::Preparing: return "Preparing";
        case VfxThumbnailDrainState::Capturing: return "Capturing";
        case VfxThumbnailDrainState::Finishing: return "Finishing";
    }
    return "Unknown";
}

class VfxThumbnailDrainer {
public:
    VfxThumbnailDrainer() = default;

    /// Binds to a capture queue. Passing `nullptr` unbinds — in which
    /// case `Tick` is a no-op.
    void Bind(VfxThumbnailCaptureQueue* queue) noexcept { m_queue = queue; }

    [[nodiscard]] VfxThumbnailDrainState State() const noexcept { return m_state; }

    /// Returns the active capturer (non-null only while `State != Idle`).
    [[nodiscard]] const VfxThumbnailCapturer* Capturer() const noexcept {
        return (m_state == VfxThumbnailDrainState::Idle) ? nullptr : &m_capturer;
    }

    /// Advances the state machine one tick.
    ///
    /// # Argument
    /// - `simDt` — simulation delta-time (seconds). Ignored unless the
    ///   drainer is `Preparing` or `Capturing`, where it is forwarded
    ///   to `VfxThumbnailCapturer::AdvanceFrame()` implicitly by the
    ///   host's existing tick loop — the drainer only bookkeeps the
    ///   tick count via the capturer's own API.
    ///
    /// # Returns
    /// The new state.
    VfxThumbnailDrainState Tick(float simDt);

    /// Callback fired on the Idle → Preparing transition. Host uses
    /// this to hide the editor overlay + timeline scrubber so only
    /// particles land in the thumbnail.
    std::function<void()> onBeginCapture;

    /// Callback fired on the Capturing → Finishing transition. Host
    /// uses this to restore overlay + scrubber visibility.
    std::function<void()> onEndCapture;

    /// Callback fired once when the capturer signals
    /// `IsReadyToCapture()`. Host should render the current scene
    /// headlessly at `(width, height)` and return the PNG bytes.
    /// Empty return value signals a capture failure — the drainer
    /// pops the request and returns to Idle without writing a file.
    std::function<std::vector<uint8_t>(uint32_t width, uint32_t height)>
        onCapturePng;

    /// Diagnostic counter: number of completed captures since
    /// construction. Incremented whenever a PNG is written to disk.
    [[nodiscard]] uint32_t CompletedCount() const noexcept { return m_completed; }

    /// Diagnostic counter: number of dropped captures (empty PNG
    /// payload, missing queue, or capturer-prepare failure).
    [[nodiscard]] uint32_t DroppedCount() const noexcept { return m_dropped; }

    /// Returns the last successful capture's result, or default when
    /// none has completed yet.
    [[nodiscard]] const VfxThumbnailResult& LastResult() const noexcept {
        return m_lastResult;
    }

private:
    void TransitionTo(VfxThumbnailDrainState next);
    void DoPreparing();
    void DoCapturing();
    void DoFinishing(const std::vector<uint8_t>& pngBytes);

    VfxThumbnailCaptureQueue* m_queue = nullptr;
    VfxThumbnailDrainState    m_state = VfxThumbnailDrainState::Idle;
    VfxThumbnailCapturer      m_capturer{};
    VfxThumbnailResult        m_lastResult{};
    uint32_t                  m_completed = 0u;
    uint32_t                  m_dropped   = 0u;
};

} // namespace uz
