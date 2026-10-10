#include "vfx_timestep.h"

#include <cmath>

namespace uz {

void VfxTimestepAccumulator::SetFrameRate() noexcept {
    m_mode        = VfxTimestepMode::FrameRate;
    m_accumulator = 0.0f;
    m_subStepDt   = 0.0f;
}

void VfxTimestepAccumulator::SetFixedRate(float fixedDt, uint32_t maxSubSteps) noexcept {
    if (!(fixedDt > 0.0f) || !std::isfinite(fixedDt)) {
        // Defensive fall-back: zero or negative `fixedDt` would either spiral
        // or divide-by-zero, so we downgrade to FrameRate and leave the
        // accumulator cleared.
        SetFrameRate();
        return;
    }
    m_mode          = VfxTimestepMode::FixedRate;
    m_fixedDt       = fixedDt;
    m_maxSubSteps   = (maxSubSteps == 0u) ? 1u : maxSubSteps;
    m_accumulator   = 0.0f;
    m_subStepDt     = fixedDt;
}

void VfxTimestepAccumulator::Reset() noexcept {
    m_accumulator = 0.0f;
    if (m_mode == VfxTimestepMode::FrameRate) {
        m_subStepDt = 0.0f;
    }
}

uint32_t VfxTimestepAccumulator::Advance(float frameDt) noexcept {
    // Reject NaN/Inf and negative time; simulation does not rewind.
    if (!(frameDt > 0.0f) || !std::isfinite(frameDt)) {
        if (m_mode == VfxTimestepMode::FrameRate) {
            m_subStepDt = 0.0f;
        }
        return 0u;
    }

    if (m_mode == VfxTimestepMode::FrameRate) {
        m_subStepDt = frameDt;
        return 1u;
    }

    // FixedRate path: accumulate and walk fixed sub-steps.
    m_accumulator += frameDt;

    // Avoid a degenerate accumulator divide; m_fixedDt > 0 guaranteed by SetFixedRate.
    uint32_t stepsNeeded = static_cast<uint32_t>(m_accumulator / m_fixedDt);

    if (stepsNeeded > m_maxSubSteps) {
        // Anti-spiral: run at most `maxSubSteps` and drop the excess. Simulation
        // progresses slightly slower than real time during the hitch but the
        // feedback loop (slow frame -> more steps -> slower frame) is broken.
        stepsNeeded    = m_maxSubSteps;
        m_accumulator  = 0.0f;
    } else {
        m_accumulator -= static_cast<float>(stepsNeeded) * m_fixedDt;
        // Clamp tiny floating-point residuals to avoid slow drift.
        if (m_accumulator < 0.0f) {
            m_accumulator = 0.0f;
        }
    }

    m_subStepDt = m_fixedDt;
    return stepsNeeded;
}

} // namespace uz
