/// VFX Parameter Panel — Real-time Emitter Configuration Editor
///
/// Provides keyboard-navigable editing of ALL VFXEmitterConfig fields organized
/// into 8 categories (Spawn, Particle, Color, Size, Physics, Forces, Rendering,
/// System). Changes apply immediately since the panel stores direct pointers to
/// the live config fields.
///
/// # Keyboard Controls (when panel is visible)
/// - **Up/Down**: Navigate between parameters within a category
/// - **Left/Right**: Adjust the selected parameter value (or toggle bools/cycle enums)
/// - **Tab**: Jump to next category
/// - **Shift+Tab**: Jump to previous category
/// - **Enter**: Expand/collapse current category
/// - **R**: Restart particle emission (needed after shape/mode changes)
/// - **Escape** or **F10**: Close the panel
///
/// # Integration
/// ```cpp
/// // In demo header:
/// VFXParameterPanel m_paramPanel;
///
/// // In OnInit():
/// m_paramPanel.Init(&m_vfxEmitter, &m_vfxSpawnRate, &m_vfxBurstCount,
///                   &m_vfxBurstMode, &m_subEmitterEnabled, &m_frustumCullingEnabled);
///
/// // In HandleInput():
/// bool needsRestart = false;
/// if (m_paramPanel.HandleInput(input, needsRestart)) {
///     if (needsRestart) m_vfxBurstPending = true;
///     return;
/// }
/// ```
///
/// # Thread Safety
/// NOT thread-safe. Call all methods from the main/render thread only.

#pragma once

#include <cstdint>
#include <vector>

namespace uz {

class InputSystem;
struct VFXEmitterConfig;

// ─────────────────────────────────────────────────────────────────────────────
// Parameter Entry Descriptor
// ─────────────────────────────────────────────────────────────────────────────

/// Describes a single editable parameter in the panel.
struct VFXParamEntry {
    const char* name = nullptr;  ///< Display label for this parameter.

    /// Parameter data type for display and adjustment logic.
    enum class Type : uint8_t {
        Float,   ///< Continuous float value, adjusted by `step`.
        Uint,    ///< Unsigned integer value, adjusted by `uintStep`.
        Bool,    ///< Boolean toggle (Left/Right both flip the value).
        Enum,    ///< Named enum, Left decrements mod N, Right increments mod N.
    } type = Type::Float;

    float*    floatPtr = nullptr;   ///< Pointer to float value (for Float type).
    uint32_t* uintPtr  = nullptr;   ///< Pointer to uint32_t value (for Uint/Bool/Enum types).

    float minVal  = 0.0f;   ///< Minimum clamp for float/uint values.
    float maxVal  = 1.0f;   ///< Maximum clamp for float/uint values.
    float step    = 0.01f;  ///< Per-keypress adjustment step for float values.
    uint32_t uintStep = 1;  ///< Per-keypress adjustment step for uint values.

    const char* const* enumNames = nullptr; ///< Null-terminated array of enum value names.
    uint32_t enumCount = 0;                 ///< Number of enum values.

    bool needsRestart = false;  ///< If true, changing this parameter sets the restart flag.
};

// ─────────────────────────────────────────────────────────────────────────────
// Parameter Category
// ─────────────────────────────────────────────────────────────────────────────

/// Groups related parameters under a collapsible category header.
struct VFXParamCategory {
    const char* name = nullptr;              ///< Category display name (e.g. "Spawn", "Physics").
    std::vector<VFXParamEntry> params;       ///< Parameters in this category.
    bool expanded = true;                    ///< Whether the category is expanded (showing params).
};

// ─────────────────────────────────────────────────────────────────────────────
// VFX Parameter Panel
// ─────────────────────────────────────────────────────────────────────────────

/// Real-time parameter editor for VFX emitter configuration.
///
/// Stores pointers directly into VFXEmitterConfig fields so that changes are
/// reflected immediately in the next GPU upload. Console-logged output shows
/// the current state after each adjustment.
class VFXParameterPanel {
public:
    /// Initializes the panel by building the category/parameter tree with pointers
    /// into the live config fields.
    ///
    /// # Arguments
    /// * `config`       — Pointer to the live VFXEmitterConfig struct.
    /// * `spawnRate`    — Pointer to the spawn rate float (outside VFXEmitterConfig).
    /// * `burstCount`   — Pointer to the burst count uint32.
    /// * `burstMode`    — Pointer to the burst mode bool.
    /// * `subEmitter`   — Pointer to the sub-emitter enabled bool.
    /// * `frustumCull`  — Pointer to the frustum culling enabled bool.
    void Init(VFXEmitterConfig* config, float* spawnRate, uint32_t* burstCount,
              bool* burstMode, bool* subEmitter, bool* frustumCull);

    /// Processes keyboard input when the panel is visible.
    ///
    /// # Arguments
    /// * `input`        — The input system for reading key states.
    /// * `needsRestart` — Set to true if a restart-requiring parameter was changed.
    ///
    /// # Returns
    /// `true` if the panel consumed the input event (caller should skip other handlers).
    /// `false` if the panel is not visible (input passes through).
    [[nodiscard]] bool HandleInput(const InputSystem* input, bool& needsRestart);

    /// Returns whether the panel overlay is currently visible.
    [[nodiscard]] bool IsVisible() const { return m_visible; }

    /// Toggles panel visibility. Logs current state to console when opening.
    void Toggle();

    /// Sets panel visibility explicitly.
    void SetVisible(bool visible);

private:
    bool m_visible = false;                          ///< Panel open/closed state.
    int  m_selectedCategory = 0;                     ///< Currently focused category index.
    int  m_selectedParam    = 0;                     ///< Currently focused parameter within category.
    std::vector<VFXParamCategory> m_categories;      ///< All parameter categories.

    // System category bool pointers (stored separately because bool* cannot
    // safely alias uint32_t* due to different sizes).
    bool* m_burstMode  = nullptr;  ///< Pointer to burst mode flag.
    bool* m_subEmitter = nullptr;  ///< Pointer to sub-emitter enabled flag.
    bool* m_frustumCull = nullptr; ///< Pointer to frustum culling flag.

    /// Builds the full category tree with pointers into config fields.
    void BuildCategories(VFXEmitterConfig* config, float* spawnRate, uint32_t* burstCount,
                         bool* burstMode, bool* subEmitter, bool* frustumCull);

    /// Adjusts a float parameter value by the given direction (+1 or -1).
    void AdjustFloat(VFXParamEntry& entry, float direction);

    /// Adjusts a uint parameter value by the given direction (+1 or -1).
    void AdjustUint(VFXParamEntry& entry, int direction);

    /// Toggles a bool parameter.
    void ToggleBool(VFXParamEntry& entry);

    /// Cycles an enum parameter in the given direction (+1 or -1, wrapping).
    void CycleEnum(VFXParamEntry& entry, int direction);

    /// Handles value adjustment for System category parameters (bool* special case).
    void HandleSystemParamAdjust(int paramIndex, float direction, bool& needsRestart);

    /// Renormalizes wind direction vector after component editing.
    void RenormalizeWindDir();

    /// Logs the full state of the currently selected category to the console.
    void LogCurrentCategory() const;

    /// Logs the currently selected parameter and its value.
    void LogCurrentParam() const;

    /// Returns a formatted string representation of the given parameter's value.
    void FormatParamValue(const VFXParamEntry& entry, char* buf, uint32_t bufSize) const;
};

} // namespace uz
