// CPU-only temporal matrix and ping-pong content validity.
#pragma once

#include "mat4.h"
#include "vector3.h"

#include <cstdint>

namespace uz {

enum class TemporalReset : uint8_t {
    None, FirstUse, ResourcesRecreated, FrameGap, UpscalerChanged, OutputSizeChanged,
    SceneChanged, CameraCut, ExecuteFailed, Explicit
};

inline const char* TemporalResetName(TemporalReset why) {
    switch (why) {
    case TemporalReset::None: return "None";
    case TemporalReset::FirstUse: return "FirstUse";
    case TemporalReset::ResourcesRecreated: return "ResourcesRecreated";
    case TemporalReset::FrameGap: return "FrameGap";
    case TemporalReset::UpscalerChanged: return "UpscalerChanged";
    case TemporalReset::OutputSizeChanged: return "OutputSizeChanged";
    case TemporalReset::SceneChanged: return "SceneChanged";
    case TemporalReset::CameraCut: return "CameraCut";
    case TemporalReset::ExecuteFailed: return "ExecuteFailed";
    case TemporalReset::Explicit: return "Explicit";
    }
    return "Unknown";
}

struct TemporalFrameKey {
    uint32_t frameIndex = 0;
    uint32_t upscalerType = 0;
    uint32_t outputW = 0, outputH = 0;
    uint32_t cameraId = 0;
    Vec3 cameraPos{}, cameraFwd{};
    Mat4 unjitteredViewProj{};
    // No scene revision: streaming meshes must not reset temporal accumulation.
};

struct TemporalThresholds {
    float cutDistanceCm = 300.f;
    float cutCosAngle = 0.866f;
};

struct TemporalDecision {
    bool matrixValid = false, contentValid = false;
    TemporalReset matrixReason = TemporalReset::FirstUse;
    TemporalReset contentReason = TemporalReset::FirstUse;
    Mat4 prevViewProj{};
    [[nodiscard]] bool firstFrame() const noexcept { return !(matrixValid && contentValid); }
};

class TemporalHistoryState {
public:
    void ResetAll(TemporalReset why) {
        m_slot[0] = {};
        m_slot[1] = {};
        m_hasPrev = false;
        m_prev = {};
        m_pending = why;
        m_failures = 0;
        m_last = {};
        m_last.matrixReason = m_last.contentReason = why;
    }

    // Content invalidation survives an inactive resolve; consume the diagnostic
    // reason on Advance, but never allow an old slot to become valid again.
    void Invalidate(TemporalReset why) {
        if (why == TemporalReset::None) return;
        m_slot[0].written = m_slot[1].written = false;
        if (m_pending != TemporalReset::CameraCut) m_pending = why;
    }

    // Once per rendered frame, even when TAA is inactive. A previous matrix is
    // valid only across consecutive frames of the same camera and output size,
    // with motion within the cut thresholds. Content additionally requires a
    // write to the read slot on exactly the previous frame, by this upscaler.
    // Advance stores the current key; MarkWritten stamps only a recorded write.
    TemporalDecision Advance(const TemporalFrameKey& key, uint32_t readSlot,
                             const TemporalThresholds& thresholds) {
        TemporalDecision d{};
        d.prevViewProj = m_prev.unjitteredViewProj;
        const float dx = key.cameraPos.x - m_prev.cameraPos.x;
        const float dy = key.cameraPos.y - m_prev.cameraPos.y;
        const float dz = key.cameraPos.z - m_prev.cameraPos.z;
        const float dot = key.cameraFwd.x * m_prev.cameraFwd.x
                        + key.cameraFwd.y * m_prev.cameraFwd.y
                        + key.cameraFwd.z * m_prev.cameraFwd.z;
        if (!m_hasPrev) {
            d.matrixReason = m_pending == TemporalReset::None ? TemporalReset::FirstUse : m_pending;
        } else if (m_prev.frameIndex + 1 != key.frameIndex) {
            d.matrixReason = TemporalReset::FrameGap;
        } else if (m_prev.outputW != key.outputW || m_prev.outputH != key.outputH) {
            d.matrixReason = TemporalReset::OutputSizeChanged;
        } else if (m_pending == TemporalReset::CameraCut || m_prev.cameraId != key.cameraId
                   || dx * dx + dy * dy + dz * dz > thresholds.cutDistanceCm * thresholds.cutDistanceCm
                   || dot < thresholds.cutCosAngle) {
            d.matrixReason = TemporalReset::CameraCut;
        } else {
            d.matrixValid = true;
            d.matrixReason = TemporalReset::None;
        }

        const auto& slot = m_slot[readSlot];
        if (!d.matrixValid) {
            d.contentReason = d.matrixReason;
        } else if (m_pending != TemporalReset::None) {
            d.contentReason = m_pending;
        } else if (!slot.written || slot.frame + 1 != key.frameIndex) {
            d.contentReason = TemporalReset::FrameGap;
        } else if (slot.upscaler != key.upscalerType) {
            d.contentReason = TemporalReset::UpscalerChanged;
        } else if (slot.w != key.outputW || slot.h != key.outputH) {
            d.contentReason = TemporalReset::OutputSizeChanged;
        } else {
            d.contentValid = true;
            d.contentReason = TemporalReset::None;
        }
        m_prev = key;
        m_hasPrev = true;
        m_pending = TemporalReset::None;
        m_last = d;
        return d;
    }

    void MarkWritten(uint32_t slot) {
        m_slot[slot] = {true, m_prev.frameIndex, m_prev.upscalerType, m_prev.outputW, m_prev.outputH};
    }

    void MarkFailed(uint32_t slot) {
        m_slot[slot].written = false;
        ++m_failures;
        m_last.contentValid = false;
        m_last.contentReason = TemporalReset::ExecuteFailed;
    }

    [[nodiscard]] uint32_t SlotFrame(uint32_t slot) const { return m_slot[slot].frame; }
    [[nodiscard]] uint32_t SlotUpscaler(uint32_t slot) const { return m_slot[slot].upscaler; }
    [[nodiscard]] bool SlotWritten(uint32_t slot) const { return m_slot[slot].written; }
    [[nodiscard]] uint32_t FailureCount() const { return m_failures; }
    [[nodiscard]] const TemporalDecision& LastDecision() const { return m_last; }

private:
    struct Slot {
        bool written = false;
        uint32_t frame = 0, upscaler = 0, w = 0, h = 0;
    };
    Slot m_slot[2]{};
    bool m_hasPrev = false;
    TemporalFrameKey m_prev{};
    TemporalReset m_pending = TemporalReset::None;
    uint32_t m_failures = 0;
    TemporalDecision m_last{};
};

} // namespace uz
