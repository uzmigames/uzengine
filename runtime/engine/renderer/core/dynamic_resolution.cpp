/// UzEngine Dynamic Resolution Controller — implementation.
///
/// @source Unity DynamicResolutionHandler — scale-up hysteresis

#include "dynamic_resolution.h"

#include <algorithm>

namespace uz {

// ─── Target Configuration ──────────────────────────────────────────────────

void DynamicResolutionController::SetTargetFrameTime(float seconds) {
    // Guard against non-positive values to avoid division by zero in GetTargetFPS().
    if (seconds > 0.0f) {
        m_targetFrameTime = seconds;
    }
}

void DynamicResolutionController::SetTargetFPS(float fps) {
    if (fps > 0.0f) {
        m_targetFrameTime = 1.0f / fps;
    }
}

// ─── Screen Percentage Range ───────────────────────────────────────────────

void DynamicResolutionController::SetMinScreenPercentage(float pct) {
    m_minPct = std::clamp(pct, kAbsoluteMinPct, m_maxPct);
    // Ensure current percentage respects the new minimum.
    m_currentPct = std::clamp(m_currentPct, m_minPct, m_maxPct);
}

void DynamicResolutionController::SetMaxScreenPercentage(float pct) {
    m_maxPct = std::clamp(pct, m_minPct, kAbsoluteMaxPct);
    // Ensure current percentage respects the new maximum.
    m_currentPct = std::clamp(m_currentPct, m_minPct, m_maxPct);
}

// ─── Per-Frame Update ──────────────────────────────────────────────────────

float DynamicResolutionController::Update(float gpuFrameTimeSeconds) {
    if (!m_enabled) {
        m_currentPct = m_maxPct;
        m_framesUnderBudget = 0;
        return m_currentPct;
    }

    // Treat non-positive GPU times as idle (no scaling action needed).
    if (gpuFrameTimeSeconds <= 0.0f) {
        return m_currentPct;
    }

    const float scaleDownBudget = m_targetFrameTime * kScaleDownRatio;
    const float scaleUpBudget   = m_targetFrameTime * kScaleUpRatio;

    if (gpuFrameTimeSeconds > scaleDownBudget) {
        // Over budget — scale down immediately to prevent compounding frame drops.
        m_currentPct -= kScaleStep;
        m_framesUnderBudget = 0;
    } else if (gpuFrameTimeSeconds < scaleUpBudget) {
        // Under budget — increment hysteresis counter.
        // @source Unity DynamicResolutionHandler — conservative scale-up
        m_framesUnderBudget++;
        if (m_framesUnderBudget >= kScaleUpThreshold) {
            m_currentPct += kScaleStep;
            m_framesUnderBudget = 0;
        }
    } else {
        // Dead zone (85%–95% of budget) — hold steady, reset counter.
        // Prevents partial scale-up accumulation from brief GPU dips.
        m_framesUnderBudget = 0;
    }

    m_currentPct = std::clamp(m_currentPct, m_minPct, m_maxPct);
    return m_currentPct;
}

// ─── Reset ─────────────────────────────────────────────────────────────────

void DynamicResolutionController::Reset() {
    m_currentPct = m_maxPct;
    m_framesUnderBudget = 0;
}

} // namespace uz
