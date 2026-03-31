/// VFX Module Stack Panel — Simulation Module Visibility & Toggle UI
///
/// Displays a vertical list of active simulation modules on the right side of
/// the screen, mirroring the style of Niagara's module stack. Each module row
/// shows an active/inactive indicator, the module name, and (where applicable)
/// an activity bar proportional to the module's strength parameter.
///
/// Modules whose state is driven by a scalar strength (Curl Noise, Vortex,
/// Wind) can be toggled via Enter: disabling saves the current strength and
/// sets it to 0; re-enabling restores the saved value (or falls back to a
/// sensible default if the saved value is also 0).
///
/// Modules controlled by a boolean flag (Ground Collision, Sub-Emitter) flip
/// the flag directly. Spawn and Update are always active and are not toggleable.
///
/// # Module List (enum class VFXModule)
/// - Spawn         — always active (cannot be toggled)
/// - Update        — always active (cannot be toggled)
/// - CurlNoise     — active when cfg.curlStrength > 0.001
/// - Vortex        — active when |vortexTangentialStrength| > 0.001 || |vortexRadialStrength| > 0.001
/// - Wind          — active when cfg.windStrength > 0.001
/// - KillVolume    — active when the demo has ≥1 active kill volumes
/// - GroundColl    — active when cfg.groundCollision != 0
/// - SubEmitter    — active when *subEmitterEnabled == true
///
/// # Keyboard Controls (when panel is visible)
/// - Up / Down   : move selection between module rows
/// - Enter       : toggle selected module (or open sub-panel for non-toggleable)
/// - Escape      : close sub-panel if open, else close panel
/// - Tab         : jump to the next active module (wrapping)
/// - Q           : toggle panel visibility (handled by demo, not by panel itself)
///
/// # isActive Logic
/// - Spawn / Update: always true
/// - CurlNoise:      cfg.curlStrength > 0.001f
/// - Vortex:         fabsf(tangential) > 0.001f || fabsf(radial) > 0.001f
/// - Wind:           cfg.windStrength > 0.001f
/// - KillVolume:     m_killVolumeCount > 0
/// - GroundColl:     cfg.groundCollision != 0
/// - SubEmitter:     *m_subEmitterEnabledPtr == true
///
/// # Integration
/// ```cpp
/// // In demo header: VFXModuleStackPanel m_moduleStack;
/// // In OnInit():
/// m_moduleStack.Init(&m_vfxEmitter, &m_subEmitterEnabled, &m_frustumCullingEnabled);
/// // In HandleInput() — before other input handlers:
/// if (input->IsKeyJustPressed(Key::Q) && !m_paramPanel.IsVisible())
///     m_moduleStack.SetVisible(!m_moduleStack.IsVisible());
/// if (m_moduleStack.HandleInput(input)) return;
/// // In OnFrame(), after stats overlay:
/// m_moduleStack.SetKillVolumeCount(m_killVolumeCount);
/// m_moduleStack.PrintStatus(); // called only when state changes
/// ```
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the render/main thread only.

#pragma once

#include "vfx_viewport_vfx.h"

#include <array>
#include <cstdint>

namespace uz {

class InputSystem;

// ─────────────────────────────────────────────────────────────────────────────
// Module Enum
// ─────────────────────────────────────────────────────────────────────────────

/// Identifies each simulation module tracked by the stack panel.
enum class VFXModule : uint8_t {
    Spawn          = 0,
    Update         = 1,
    CurlNoise      = 2,
    Vortex         = 3,
    Wind           = 4,
    KillVolume     = 5,
    GroundCollision = 6,
    SubEmitter     = 7,
    Count          = 8,
};

// ─────────────────────────────────────────────────────────────────────────────
// Module Descriptor
// ─────────────────────────────────────────────────────────────────────────────

/// Describes a single VFX simulation module for display and toggling.
struct VFXModuleDescriptor {
    VFXModule   id;                   ///< Enum value identifying this module.
    const char* displayName;          ///< Human-readable label, e.g. "Curl Noise".
    const char* shortcutHint;         ///< Informational hint, e.g. "(auto from curlStrength)".

    /// Pointer to the bool that controls this module's enabled state.
    /// nullptr if the module is not directly bool-toggleable (e.g. strength-based or always-on).
    bool*       enabledPtr;

    /// Pointer to the float strength parameter that acts as the on/off signal.
    /// nullptr if the module has no scalar strength (bool-only or always-on).
    float*      strengthPtr;

    float strengthMin;   ///< Minimum of the strength range (for activity bar normalisation).
    float strengthMax;   ///< Maximum of the strength range (for activity bar normalisation).
};

// ─────────────────────────────────────────────────────────────────────────────
// VFX Module Stack Panel
// ─────────────────────────────────────────────────────────────────────────────

/// Console-based module stack panel that shows which VFX simulation modules are
/// active and provides keyboard toggle capability.
///
/// The panel uses UZ_LOG for all output (no GPU text rendering required) and
/// follows the same design contract as VFXParameterPanel.
class VFXModuleStackPanel {
public:
    /// Initialises the panel and wires up pointers into live demo state.
    ///
    /// # Arguments
    /// * `config`             — Pointer to the live VFXEmitterConfig.
    /// * `subEmitterEnabled`  — Pointer to the sub-emitter enabled flag in the demo.
    /// * `frustumCullingEnabled` — Pointer to the frustum-culling enabled flag.
    void Init(VFXEmitterConfig* config,
              bool* subEmitterEnabled,
              bool* frustumCullingEnabled);

    /// Processes keyboard input when the panel is visible.
    ///
    /// # Arguments
    /// * `input` — The input system for reading key states.
    ///
    /// # Returns
    /// `true` if the panel consumed the event (caller should skip other handlers).
    /// `false` if the panel is not visible or the key was not handled.
    [[nodiscard]] bool HandleInput(const InputSystem* input);

    /// Logs the full module stack state to the console.
    ///
    /// Prints each module with its on/off status and, where applicable, the
    /// current strength value. Call after a toggle or on panel open.
    void PrintStatus() const;

    /// Informs the panel of the current number of active kill volumes so the
    /// KillVolume module's isActive status can be computed correctly.
    ///
    /// # Arguments
    /// * `count` — Number of kill volumes currently active in the demo.
    void SetKillVolumeCount(uint32_t count) { m_killVolumeCount = count; }

    /// Returns whether the panel overlay is currently visible.
    [[nodiscard]] bool IsVisible() const { return m_visible; }

    /// Toggles panel visibility. Logs current stack status when opening.
    void Toggle();

    /// Sets panel visibility explicitly.
    void SetVisible(bool visible);

private:
    // ── Panel state ──────────────────────────────────────────────────────────
    bool     m_visible        = false;  ///< Panel open/closed state.
    int      m_selectedModule = 0;      ///< Index of the currently highlighted module row.
    bool     m_subPanelOpen   = false;  ///< Whether the sub-panel for the selected module is shown.
    float    m_toastTimer     = 0.0f;   ///< Countdown (s) for the "always active" toast message.

    // ── Bound demo state ─────────────────────────────────────────────────────
    VFXEmitterConfig* m_config                = nullptr;  ///< Pointer to the live emitter config.
    bool*             m_subEmitterEnabledPtr  = nullptr;  ///< Pointer to sub-emitter enabled flag.
    uint32_t          m_killVolumeCount       = 0;        ///< Current kill volume count (set per frame).

    // ── Module descriptors ───────────────────────────────────────────────────
    std::array<VFXModuleDescriptor, static_cast<int>(VFXModule::Count)> m_modules{};

    // ── Saved strengths for toggle-restore ───────────────────────────────────
    /// Saved non-zero strength values so that toggling a strength-based module
    /// off and back on restores the original value.
    std::array<float, static_cast<int>(VFXModule::Count)> m_savedStrength{};

    // ── Private helpers ──────────────────────────────────────────────────────

    /// Builds the module descriptor table with pointers into the bound state.
    void BuildDescriptors();

    /// Computes the isActive flag for every module and writes the results into
    /// the provided array. Array must be at least VFXModule::Count elements.
    void ComputeActiveFlags(bool* outActive) const;

    /// Toggles the module at index `idx`. For bool-controlled modules, flips the
    /// flag. For strength-based modules, saves/restores the strength.
    void ToggleModule(int idx);

    /// Returns the default restore strength for modules where the saved value is 0.
    /// CurlNoise=1.5, Vortex=3.0, Wind=5.0, others=0.
    [[nodiscard]] float DefaultRestoreStrength(VFXModule module) const;

    /// Logs the state of a single module (used after a toggle).
    void LogModuleState(int idx, bool isActive) const;

    /// Returns the number of currently active modules (for the footer summary).
    [[nodiscard]] int CountActiveModules() const;

    /// Logs display-only parameter details for the selected module in the sub-panel.
    void LogSubPanelInfo(int idx) const;
};

} // namespace uz
