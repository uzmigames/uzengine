/// EditorInputHandler — Routes mouse/keyboard to selection and gizmo systems.
///
/// Manages gizmo mode (Translate/Rotate/Scale) and coordinate space (World/Local).
/// Listens for hotkeys (W/E/R for mode, Space for space toggle) and forwards
/// left-click events to the SelectionManager for entity picking. When a gizmo
/// handle is clicked, initiates drag interaction to translate/rotate/scale the
/// selected entity in real-time.
///
/// # Interaction Priority
/// 1. Gizmo drag (if mouse is over a gizmo handle, left-click starts drag)
/// 2. Entity selection (left-click on scene if gizmo not hit)
/// 3. Camera orbit (right-click, handled elsewhere)
///
/// # Lifecycle
/// ```cpp
/// EditorInputHandler handler;
/// handler.Init(input, selection, camera, world);
/// // Per frame (editor mode only):
/// handler.Update(dt);
/// ```
///
/// # Thread Safety
/// NOT thread-safe. All calls must be from the main thread.

#pragma once

#include "gizmo_interaction.h"

#include <cstdint>

namespace uz {

class InputSystem;
class SelectionManager;
class Camera;
class World;
class SceneRenderer;

// ─────────────────────────────────────────────────────────────────────────────
// Gizmo Enums
// ─────────────────────────────────────────────────────────────────────────────

/// Gizmo manipulation mode — determines which transform operation the gizmo performs.
enum class GizmoMode : uint8_t {
    Select,    ///< Selection mode — no gizmo drawn, LMB is pure ray-cast pick (hotkey: Q)
    Translate, ///< Move selected entity (hotkey: W)
    Rotate,    ///< Rotate selected entity (hotkey: E)
    Scale,     ///< Scale selected entity (hotkey: R)
};

/// Gizmo coordinate space — determines how gizmo axes are oriented.
enum class GizmoSpace : uint8_t {
    World, ///< Gizmo axes aligned to world axes.
    Local, ///< Gizmo axes aligned to the selected entity's local rotation.
};

// ─────────────────────────────────────────────────────────────────────────────
// Snap defaults
// ─────────────────────────────────────────────────────────────────────────────

/// Default translate snap grid in centimeters (10 cm).
static constexpr float kDefaultTranslateSnap = 10.0f;

/// Default rotate snap in degrees (15 degrees).
static constexpr float kDefaultRotateSnap = 15.0f;

/// Default scale snap increment (0.25 = quarter steps).
static constexpr float kDefaultScaleSnap = 0.25f;

// ─────────────────────────────────────────────────────────────────────────────
// EditorInputHandler
// ─────────────────────────────────────────────────────────────────────────────

/// Routes editor-mode input events to selection and gizmo systems.
///
/// Hotkeys:
/// - Q: Switch to Select mode (no gizmo, LMB picks entity)
/// - W: Switch to Translate mode
/// - E: Switch to Rotate mode
/// - R: Switch to Scale mode
/// - Grave (` / ~): Toggle World/Local coordinate space
/// - Left-click on gizmo: Begin drag to transform entity (Translate/Rotate/Scale only)
/// - Left-click on scene: Pick entity at cursor position via SelectionManager
/// - Ctrl + drag: Enable snapping (10cm translate, 15deg rotate, 0.25 scale)
///
/// # Ownership
/// Does NOT own any of the pointers passed to `Init()`. All must outlive
/// this handler. Null pointers disable the corresponding functionality.
class EditorInputHandler {
public:
    /// Connects the handler to the input, selection, camera, and world systems.
    ///
    /// All pointers are non-owning and must outlive this handler.
    /// A null `input` pointer disables all input handling (Update becomes a no-op).
    ///
    /// # Arguments
    /// * `input` — Input system for key/mouse queries. Required.
    /// * `selection` — Selection manager for entity picking. Required for click-to-select.
    /// * `camera` — Camera for ray construction during picking. Required for click-to-select.
    /// * `world` — ECS world for entity iteration during picking. Required for click-to-select.
    void Init(InputSystem* input, SelectionManager* selection,
              Camera* camera, World* world);

    /// Sets the scene renderer used for mesh bounds queries during picking.
    ///
    /// Must be called after Init() and before Update() if accurate mesh-based
    /// AABB picking is desired. Without a renderer, picking uses a default
    /// unit-cube AABB for each entity.
    ///
    /// # Arguments
    /// * `renderer` — Scene renderer with mesh bounds data. Non-owning, must outlive handler.
    void SetSceneRenderer(SceneRenderer* renderer) { m_renderer = renderer; }

    /// Processes input for the current frame.
    ///
    /// Handles gizmo mode/space hotkeys, gizmo hit testing (hover highlight),
    /// left-click for drag start or entity selection, drag update, and drag end.
    ///
    /// # Arguments
    /// * `dt` — Frame delta time in seconds.
    void Update(float dt);

    /// Returns the current gizmo manipulation mode.
    [[nodiscard]] GizmoMode GetGizmoMode() const { return m_mode; }

    /// Sets the gizmo manipulation mode programmatically (e.g., from Lua API).
    void SetGizmoMode(GizmoMode mode) { m_mode = mode; }

    /// Returns the current gizmo coordinate space.
    [[nodiscard]] GizmoSpace GetGizmoSpace() const { return m_space; }

    /// Sets the gizmo coordinate space programmatically (e.g., from Lua API).
    void SetGizmoSpace(GizmoSpace space) { m_space = space; }

    /// Returns the gizmo hovered axis as an int for the renderer.
    ///
    /// Encoding: -1=none, 0=X, 1=Y, 2=Z, 3=XY, 4=XZ, 5=YZ, 6=center.
    /// Matches the hoveredAxis/activeAxis parameter of GizmoRenderer::Draw.
    [[nodiscard]] int GetGizmoHoveredAxis() const;

    /// Returns the gizmo active (dragging) axis as an int for the renderer.
    ///
    /// Same encoding as GetGizmoHoveredAxis.
    [[nodiscard]] int GetGizmoActiveAxis() const;

    /// Returns true if a gizmo drag is in progress.
    [[nodiscard]] bool IsGizmoDragging() const { return m_gizmoInteraction.IsDragging(); }

private:
    /// Converts a GizmoHitResult to the int encoding used by GizmoRenderer::Draw.
    /// None → -1, AxisX → 0, AxisY → 1, AxisZ → 2, PlaneXY → 3, PlaneXZ → 4,
    /// PlaneYZ → 5, Center → 6.
    static int HitResultToAxisIndex(GizmoHitResult hit);

    InputSystem* m_input = nullptr;
    SelectionManager* m_selection = nullptr;
    Camera* m_camera = nullptr;
    World* m_world = nullptr;
    SceneRenderer* m_renderer = nullptr;
    GizmoMode m_mode = GizmoMode::Translate;
    GizmoSpace m_space = GizmoSpace::World;
    GizmoInteraction m_gizmoInteraction;
};

} // namespace uz
