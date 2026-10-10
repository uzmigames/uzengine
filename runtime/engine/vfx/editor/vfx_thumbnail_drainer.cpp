/// VfxThumbnailDrainer — implementation.

#include "vfx_thumbnail_drainer.h"

#include "vfx_thumbnail_save_ext.h"

namespace uz {

void VfxThumbnailDrainer::TransitionTo(VfxThumbnailDrainState next) {
    m_state = next;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick: state machine driver
// ─────────────────────────────────────────────────────────────────────────────

VfxThumbnailDrainState VfxThumbnailDrainer::Tick(float /*simDt*/) {
    if (m_queue == nullptr) return m_state;

    switch (m_state) {
    case VfxThumbnailDrainState::Idle: {
        const VfxThumbnailCaptureRequest* req = m_queue->Peek();
        if (req == nullptr) break;

        // PrepareCapture validates config + seeds the state machine.
        if (!m_capturer.PrepareCapture(req->config)) {
            // Bad config — drop the request and stay Idle.
            m_queue->Pop();
            ++m_dropped;
            break;
        }
        if (onBeginCapture) onBeginCapture();
        TransitionTo(VfxThumbnailDrainState::Preparing);
        break;
    }
    case VfxThumbnailDrainState::Preparing:
        DoPreparing();
        break;
    case VfxThumbnailDrainState::Capturing:
        DoCapturing();
        break;
    case VfxThumbnailDrainState::Finishing:
        // Finishing is atomic inside DoCapturing's tail — Tick()
        // shouldn't normally observe this state for more than one
        // frame, but handle it defensively.
        TransitionTo(VfxThumbnailDrainState::Idle);
        break;
    }
    return m_state;
}

// ─────────────────────────────────────────────────────────────────────────────
// Preparing: advance capturer ticks until ready
// ─────────────────────────────────────────────────────────────────────────────

void VfxThumbnailDrainer::DoPreparing() {
    m_capturer.AdvanceFrame();
    if (m_capturer.IsReadyToCapture()) {
        TransitionTo(VfxThumbnailDrainState::Capturing);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Capturing: ask host for PNG bytes + write to disk
// ─────────────────────────────────────────────────────────────────────────────

void VfxThumbnailDrainer::DoCapturing() {
    std::vector<uint8_t> pngBytes;
    if (onCapturePng) {
        pngBytes = onCapturePng(m_capturer.GetWidth(), m_capturer.GetHeight());
    }
    DoFinishing(pngBytes);
}

// ─────────────────────────────────────────────────────────────────────────────
// Finishing: FinishCapture + pop + return to Idle
// ─────────────────────────────────────────────────────────────────────────────

void VfxThumbnailDrainer::DoFinishing(const std::vector<uint8_t>& pngBytes) {
    if (pngBytes.empty()) {
        // Host declined to produce bytes — drop the request.
        ++m_dropped;
    } else {
        m_lastResult = m_capturer.FinishCapture(pngBytes);
        if (m_lastResult.success) {
            ++m_completed;
        } else {
            ++m_dropped;
        }
    }

    // Always restore overlay/timeline visibility before popping —
    // even on failure — so the editor doesn't stay hidden forever.
    if (onEndCapture) onEndCapture();

    if (m_queue != nullptr) m_queue->Pop();
    TransitionTo(VfxThumbnailDrainState::Idle);
}

} // namespace uz
