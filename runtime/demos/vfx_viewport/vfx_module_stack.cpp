/// VFX Module Stack Panel — Implementation
///
/// Console-based simulation module visibility display and toggle logic.
/// All output goes through UZ_LOG. No GPU text rendering is required.

#include "vfx_module_stack.h"
#include "input.h"
#include "uzengine.h"

#include <cmath>
#include <cstdio>

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

void VFXModuleStackPanel::Init(VFXEmitterConfig* config,
                                bool* subEmitterEnabled,
                                bool* /*frustumCullingEnabled*/) {
    m_config               = config;
    m_subEmitterEnabledPtr = subEmitterEnabled;
    m_visible              = false;
    m_selectedModule       = 0;
    m_subPanelOpen         = false;
    m_toastTimer           = 0.0f;
    m_killVolumeCount      = 0;
    m_savedStrength.fill(0.0f);

    BuildDescriptors();
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildDescriptors — wire module table to live config pointers
// ─────────────────────────────────────────────────────────────────────────────

void VFXModuleStackPanel::BuildDescriptors() {
    // Spawn — always active, not toggleable
    m_modules[0] = {
        VFXModule::Spawn,
        "Spawn",
        "always active",
        nullptr,   // enabledPtr
        nullptr,   // strengthPtr
        0.0f, 0.0f
    };

    // Update — always active, not toggleable
    m_modules[1] = {
        VFXModule::Update,
        "Update",
        "always active",
        nullptr,
        nullptr,
        0.0f, 0.0f
    };

    // Curl Noise — strength-based toggle
    m_modules[2] = {
        VFXModule::CurlNoise,
        "Curl Noise",
        "auto from curlStrength",
        nullptr,
        &m_config->curlStrength,
        0.0f, 10.0f
    };

    // Vortex — strength-based toggle (tangential component drives activity indicator)
    m_modules[3] = {
        VFXModule::Vortex,
        "Vortex",
        "vortexTangential",
        nullptr,
        &m_config->vortexTangentialStrength,
        -20.0f, 20.0f
    };

    // Wind — strength-based toggle
    m_modules[4] = {
        VFXModule::Wind,
        "Wind",
        "windStrength",
        nullptr,
        &m_config->windStrength,
        0.0f, 50.0f
    };

    // Kill Volume — activity determined by m_killVolumeCount, no direct toggle
    m_modules[5] = {
        VFXModule::KillVolume,
        "Kill Volume",
        "(demo K key)",
        nullptr,
        nullptr,
        0.0f, 0.0f
    };

    // Ground Collision — uint32_t flag reinterpreted as bool*
    // The groundCollision field is uint32_t; we cast its address to bool* and
    // use the ^= 1u path in ToggleModule to flip it safely.
    m_modules[6] = {
        VFXModule::GroundCollision,
        "Ground Coll.",
        "groundCollision",
        reinterpret_cast<bool*>(&m_config->groundCollision),
        nullptr,
        0.0f, 0.0f
    };

    // Sub-Emitter — direct bool toggle
    m_modules[7] = {
        VFXModule::SubEmitter,
        "Sub-Emitter",
        "E key",
        m_subEmitterEnabledPtr,
        nullptr,
        0.0f, 0.0f
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeActiveFlags
// ─────────────────────────────────────────────────────────────────────────────

void VFXModuleStackPanel::ComputeActiveFlags(bool* outActive) const {
    constexpr float kThreshold = 0.001f;

    outActive[static_cast<int>(VFXModule::Spawn)]  = true;
    outActive[static_cast<int>(VFXModule::Update)] = true;

    outActive[static_cast<int>(VFXModule::CurlNoise)] =
        m_config->curlStrength > kThreshold;

    outActive[static_cast<int>(VFXModule::Vortex)] =
        std::fabsf(m_config->vortexTangentialStrength) > kThreshold ||
        std::fabsf(m_config->vortexRadialStrength)     > kThreshold;

    outActive[static_cast<int>(VFXModule::Wind)] =
        m_config->windStrength > kThreshold;

    outActive[static_cast<int>(VFXModule::KillVolume)] =
        m_killVolumeCount > 0;

    outActive[static_cast<int>(VFXModule::GroundCollision)] =
        m_config->groundCollision != 0;

    outActive[static_cast<int>(VFXModule::SubEmitter)] =
        m_subEmitterEnabledPtr != nullptr && *m_subEmitterEnabledPtr;
}

// ─────────────────────────────────────────────────────────────────────────────
// ToggleModule
// ─────────────────────────────────────────────────────────────────────────────

void VFXModuleStackPanel::ToggleModule(int idx) {
    const VFXModuleDescriptor& desc = m_modules[static_cast<size_t>(idx)];
    const VFXModule module = static_cast<VFXModule>(idx);

    // Compute current active state
    bool isActive[static_cast<int>(VFXModule::Count)]{};
    ComputeActiveFlags(isActive);
    const bool currentlyActive = isActive[idx];

    if (desc.enabledPtr != nullptr && desc.strengthPtr == nullptr) {
        // Bool-controlled module (GroundCollision uses uint32_t cast as bool*,
        // SubEmitter uses a real bool*).
        if (module == VFXModule::GroundCollision) {
            // Safe uint32_t flip — enabledPtr addresses a uint32_t field
            auto* uintPtr = reinterpret_cast<uint32_t*>(desc.enabledPtr);
            *uintPtr ^= 1u;
        } else {
            *desc.enabledPtr = !(*desc.enabledPtr);
        }
        // Re-compute after the flip for logging
        ComputeActiveFlags(isActive);
        LogModuleState(idx, isActive[idx]);
        return;
    }

    if (desc.strengthPtr != nullptr) {
        // Strength-based toggle
        if (currentlyActive) {
            // Save current strength and zero it out
            m_savedStrength[static_cast<size_t>(idx)] = *desc.strengthPtr;
            *desc.strengthPtr = 0.0f;
        } else {
            // Restore saved strength (or use a sensible default)
            float restore = m_savedStrength[static_cast<size_t>(idx)];
            if (std::fabsf(restore) < 0.001f) {
                restore = DefaultRestoreStrength(module);
            }
            *desc.strengthPtr = restore;
        }
        // Re-compute after change for logging
        ComputeActiveFlags(isActive);
        LogModuleState(idx, isActive[idx]);
        return;
    }

    // Non-toggleable module (Spawn, Update, KillVolume) — show informational toast
    m_toastTimer = 1.5f;
    UZ_LOG(App, Info, "[VFX Stack] %s is always active — cannot be toggled",
           desc.displayName);
}

// ─────────────────────────────────────────────────────────────────────────────
// DefaultRestoreStrength
// ─────────────────────────────────────────────────────────────────────────────

float VFXModuleStackPanel::DefaultRestoreStrength(VFXModule module) const {
    switch (module) {
        case VFXModule::CurlNoise: return 1.5f;
        case VFXModule::Vortex:    return 3.0f;
        case VFXModule::Wind:      return 5.0f;
        default:                   return 0.0f;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LogModuleState
// ─────────────────────────────────────────────────────────────────────────────

void VFXModuleStackPanel::LogModuleState(int idx, bool isActive) const {
    const VFXModuleDescriptor& desc = m_modules[static_cast<size_t>(idx)];
    if (desc.strengthPtr != nullptr) {
        UZ_LOG(App, Info, "[VFX Stack] Module %-16s : %s  (strength=%.3f)",
               desc.displayName,
               isActive ? "ON " : "OFF",
               *desc.strengthPtr);
    } else {
        UZ_LOG(App, Info, "[VFX Stack] Module %-16s : %s",
               desc.displayName,
               isActive ? "ON " : "OFF");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CountActiveModules
// ─────────────────────────────────────────────────────────────────────────────

int VFXModuleStackPanel::CountActiveModules() const {
    bool active[static_cast<int>(VFXModule::Count)]{};
    ComputeActiveFlags(active);
    int count = 0;
    for (bool b : active) {
        if (b) ++count;
    }
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
// PrintStatus
// ─────────────────────────────────────────────────────────────────────────────

void VFXModuleStackPanel::PrintStatus() const {
    bool active[static_cast<int>(VFXModule::Count)]{};
    ComputeActiveFlags(active);

    UZ_LOG(App, Info, "────────────────────────────────");
    UZ_LOG(App, Info, "[VFX Modules]");

    for (int i = 0; i < static_cast<int>(VFXModule::Count); ++i) {
        const VFXModuleDescriptor& desc = m_modules[static_cast<size_t>(i)];
        const bool isActive = active[i];
        const bool isSelected = (i == m_selectedModule);

        if (desc.strengthPtr != nullptr) {
            // Display strength alongside the on/off status
            UZ_LOG(App, Info, "  %s %-16s %-3s  (%.3f)  %s",
                   isActive ? "\xE2\x97\x8F" : "\xE2\x97\x8B",  // UTF-8 ● / ○
                   desc.displayName,
                   isActive ? "ON " : "OFF",
                   *desc.strengthPtr,
                   isSelected ? "<-- selected" : "");
        } else {
            UZ_LOG(App, Info, "  %s %-16s %-3s         %s",
                   isActive ? "\xE2\x97\x8F" : "\xE2\x97\x8B",
                   desc.displayName,
                   isActive ? "ON " : "OFF",
                   isSelected ? "<-- selected" : "");
        }
    }

    UZ_LOG(App, Info, "  Active: %d / %d modules",
           CountActiveModules(),
           static_cast<int>(VFXModule::Count));
    UZ_LOG(App, Info, "────────────────────────────────");
}

// ─────────────────────────────────────────────────────────────────────────────
// Toggle / SetVisible
// ─────────────────────────────────────────────────────────────────────────────

void VFXModuleStackPanel::Toggle() {
    SetVisible(!m_visible);
}

void VFXModuleStackPanel::SetVisible(bool visible) {
    m_visible = visible;
    if (m_visible) {
        UZ_LOG(App, Info, "[VFX Stack] Panel opened — current state:");
        PrintStatus();
        UZ_LOG(App, Info, "  ↑/↓ select  Enter toggle  Tab next-active  Esc close");
    } else {
        m_subPanelOpen = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// HandleInput
// ─────────────────────────────────────────────────────────────────────────────

bool VFXModuleStackPanel::HandleInput(const InputSystem* input) {
    if (!m_visible) return false;

    constexpr int kModuleCount = static_cast<int>(VFXModule::Count);

    // Escape: close sub-panel first, then close the stack panel
    if (input->IsKeyJustPressed(Key::Escape)) {
        if (m_subPanelOpen) {
            m_subPanelOpen = false;
            return true;
        }
        SetVisible(false);
        return true;
    }

    // Up: move selection up (wrap)
    if (input->IsKeyJustPressed(Key::Up)) {
        m_selectedModule = (m_selectedModule + kModuleCount - 1) % kModuleCount;
        UZ_LOG(App, Info, "[VFX Stack] Selected: %s",
               m_modules[static_cast<size_t>(m_selectedModule)].displayName);
        return true;
    }

    // Down: move selection down (wrap)
    if (input->IsKeyJustPressed(Key::Down)) {
        m_selectedModule = (m_selectedModule + 1) % kModuleCount;
        UZ_LOG(App, Info, "[VFX Stack] Selected: %s",
               m_modules[static_cast<size_t>(m_selectedModule)].displayName);
        return true;
    }

    // Tab: jump to next active module
    if (input->IsKeyJustPressed(Key::Tab)) {
        bool active[kModuleCount]{};
        ComputeActiveFlags(active);

        // Search forward from current position for the next active module
        int next = m_selectedModule;
        for (int step = 1; step < kModuleCount; ++step) {
            const int candidate = (m_selectedModule + step) % kModuleCount;
            if (active[candidate]) {
                next = candidate;
                break;
            }
        }
        m_selectedModule = next;
        UZ_LOG(App, Info, "[VFX Stack] Jumped to active module: %s",
               m_modules[static_cast<size_t>(m_selectedModule)].displayName);
        return true;
    }

    // Enter: toggle the selected module, or open/close the sub-panel
    if (input->IsKeyJustPressed(Key::Enter)) {
        const VFXModuleDescriptor& desc =
            m_modules[static_cast<size_t>(m_selectedModule)];

        const bool isToggleable =
            (desc.enabledPtr != nullptr) || (desc.strengthPtr != nullptr);

        if (isToggleable) {
            ToggleModule(m_selectedModule);
        } else {
            // Non-toggleable: toggle the sub-panel that shows informational text
            m_subPanelOpen = !m_subPanelOpen;
            if (m_subPanelOpen) {
                LogSubPanelInfo(m_selectedModule);
            }
        }
        return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// LogSubPanelInfo — display-only parameter info for the selected module
// ─────────────────────────────────────────────────────────────────────────────

void VFXModuleStackPanel::LogSubPanelInfo(int idx) const {
    const VFXModuleDescriptor& desc = m_modules[static_cast<size_t>(idx)];
    const VFXModule module = static_cast<VFXModule>(idx);

    UZ_LOG(App, Info, "  ─── %s params ───", desc.displayName);

    switch (module) {
        case VFXModule::Spawn:
        case VFXModule::Update:
        case VFXModule::KillVolume:
            UZ_LOG(App, Info, "    No editable params in this panel");
            break;

        case VFXModule::CurlNoise:
            UZ_LOG(App, Info, "    curlStrength  = %.3f", m_config->curlStrength);
            UZ_LOG(App, Info, "    curlFrequency = %.3f", m_config->curlFrequency);
            UZ_LOG(App, Info, "    curlSpeed     = %.3f", m_config->curlSpeed);
            break;

        case VFXModule::Vortex:
            UZ_LOG(App, Info, "    vortexTangentialStrength = %.3f",
                   m_config->vortexTangentialStrength);
            UZ_LOG(App, Info, "    vortexRadialStrength     = %.3f",
                   m_config->vortexRadialStrength);
            UZ_LOG(App, Info, "    vortexMaxRadius          = %.3f",
                   m_config->vortexMaxRadius);
            break;

        case VFXModule::Wind:
            UZ_LOG(App, Info, "    windStrength = %.3f", m_config->windStrength);
            UZ_LOG(App, Info, "    windDir      = (%.2f, %.2f, %.2f)",
                   m_config->windDir[0],
                   m_config->windDir[1],
                   m_config->windDir[2]);
            break;

        case VFXModule::GroundCollision:
            UZ_LOG(App, Info, "    groundCollision    = %u", m_config->groundCollision);
            UZ_LOG(App, Info, "    restitution        = %.3f", m_config->restitution);
            UZ_LOG(App, Info, "    collisionResponse  = %u (0=Bounce 1=Stick 2=Kill)",
                   m_config->collisionResponse);
            UZ_LOG(App, Info, "    collisionFriction  = %.3f", m_config->collisionFriction);
            break;

        case VFXModule::SubEmitter:
            UZ_LOG(App, Info, "    Use E key to toggle sub-emitter");
            break;

        default:
            break;
    }
}

} // namespace uz
