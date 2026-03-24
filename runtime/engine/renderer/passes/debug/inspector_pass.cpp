/// UzEngine Pass Inspector — Implementation.
///
/// Manages per-frame target registration, visualization target lookup,
/// pass toggling, comparison, stats collection, dump request lifecycle,
/// and grid visualization for the render pass inspector.
///
/// Validation logic lives in inspector_pass_validate.cpp.
/// Dump execution logic lives in inspector_pass_dump.cpp.

#include "inspector_pass.h"

#include "log.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

UZ_DEFINE_LOG_CATEGORY(PassInspector, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::BeginFrame() {
    m_targets.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Target registration
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::RegisterTarget(const char* name, RHITextureHandle texture, uint32_t srvIndex,
                                    VisDisplayMode mode, uint32_t width, uint32_t height,
                                    RHIResourceState state) {
    if (!name || !texture.IsValid() || srvIndex == UINT32_MAX) {
        return;
    }

    RegisteredTarget target;
    target.name      = name;
    target.texture   = texture;
    target.srvIndex  = srvIndex;
    target.mode      = mode;
    target.width     = width;
    target.height    = height;
    target.lastState = state;

    m_targets.push_back(std::move(target));
}

// ─────────────────────────────────────────────────────────────────────────────
// Visualization control
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::SetVisTarget(const char* target) {
    if (!target || target[0] == '\0') {
        ClearVis();
        return;
    }

    // "off" and "final" are special — they disable visualization.
    if (std::strcmp(target, "off") == 0 || std::strcmp(target, "final") == 0) {
        ClearVis();
        return;
    }

    m_visTargetName = target;
    UZ_LOG(PassInspector, Info, "PassInspector: vis target set to '{}'", m_visTargetName);
}

void PassInspector::ClearVis() {
    if (!m_visTargetName.empty()) {
        UZ_LOG(PassInspector, Info, "PassInspector: vis disabled");
    }
    m_visTargetName.clear();
}

const RegisteredTarget* PassInspector::GetVisTarget() const noexcept {
    if (m_visTargetName.empty()) {
        return nullptr;
    }

    for (const auto& target : m_targets) {
        if (target.name == m_visTargetName) {
            return &target;
        }
    }

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass Toggle (Phase 4)
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::TogglePass(const std::string& passName) {
    if (passName.empty()) return;

    auto it = m_disabledPasses.find(passName);
    if (it != m_disabledPasses.end()) {
        m_disabledPasses.erase(it);
        UZ_LOG(PassInspector, Info, "PassInspector: pass '{}' enabled", passName);
    } else {
        m_disabledPasses.insert(passName);
        UZ_LOG(PassInspector, Info, "PassInspector: pass '{}' disabled", passName);
    }
}

bool PassInspector::IsPassEnabled(const std::string& passName) const {
    return m_disabledPasses.find(passName) == m_disabledPasses.end();
}

void PassInspector::SetPassEnabled(const std::string& passName, bool enabled) {
    if (passName.empty()) return;

    if (enabled) {
        m_disabledPasses.erase(passName);
    } else {
        m_disabledPasses.insert(passName);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass Compare (Phase 4)
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::SetComparePass(const std::string& passName) {
    if (passName.empty()) {
        ClearCompare();
        return;
    }

    m_comparePassName = passName;
    m_comparePhase = 0;
    UZ_LOG(PassInspector, Info, "PassInspector: comparison mode for '{}' — "
           "next 2 frames will capture with/without", passName);
}

void PassInspector::ClearCompare() {
    if (!m_comparePassName.empty()) {
        UZ_LOG(PassInspector, Info, "PassInspector: comparison mode ended");
    }
    m_comparePassName.clear();
    m_comparePhase = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass Stats (Phase 2)
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::SetTimingData(const std::vector<PassTimingEntry>& entries) {
    m_timingData = entries;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass Dump — Request lifecycle (execution in inspector_pass_dump.cpp)
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::RequestDump(uint32_t frame) {
    if (frame == UINT32_MAX) {
        m_dumpNextFrame = true;
        UZ_LOG(PassInspector, Info, "PassInspector: dump requested for next frame");
    } else {
        m_dumpFrame = frame;
        m_dumpNextFrame = false;
        UZ_LOG(PassInspector, Info, "PassInspector: dump scheduled for frame {}", frame);
    }
}

bool PassInspector::IsDumpPending(uint32_t currentFrame) const noexcept {
    if (m_dumpNextFrame) return true;
    return m_dumpFrame != UINT32_MAX && currentFrame >= m_dumpFrame;
}

void PassInspector::ClearDumpRequest() {
    m_dumpFrame = UINT32_MAX;
    m_dumpNextFrame = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass Grid (Phase 5)
// ─────────────────────────────────────────────────────────────────────────────

std::vector<const RegisteredTarget*> PassInspector::GetGridTargets() const {
    std::vector<const RegisteredTarget*> gridTargets;
    const uint32_t maxCells = GetGridCellCount();
    gridTargets.reserve(std::min(maxCells, static_cast<uint32_t>(m_targets.size())));

    for (uint32_t i = 0; i < static_cast<uint32_t>(m_targets.size()) && i < maxCells; ++i) {
        gridTargets.push_back(&m_targets[i]);
    }

    return gridTargets;
}

} // namespace uz
