/// EditorInputHandler — Routes mouse/keyboard to selection and gizmo systems.
///
/// Manages gizmo mode (Translate/Rotate/Scale) and coordinate space (World/Local).
/// Listens for hotkeys (W/E/R for mode, Space for space toggle) and forwards
/// left-click events to the SelectionManager for entity picking.
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
// EditorInputHandler
// ─────────────────────────────────────────────────────────────────────────────

/// Routes editor-mode input events to selection and gizmo systems.
///
/// Hotkeys:
/// - W: Switch to Translate mode
/// - E: Switch to Rotate mode
/// - R: Switch to Scale mode
/// - Space: Toggle World/Local coordinate space
/// - Left-click: Pick entity at cursor position via SelectionManager
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
    /// Checks hotkeys for gizmo mode/space changes and handles left-click
    /// for entity selection. No-op if `m_input` is null.
    ///
    /// # Arguments
    /// * `dt` — Frame delta time in seconds (unused currently, reserved for future drag smoothing).
    void Update(float dt);

    /// Returns the current gizmo manipulation mode.
    [[nodiscard]] GizmoMode GetGizmoMode() const { return m_mode; }

    /// Returns the current gizmo coordinate space.
    [[nodiscard]] GizmoSpace GetGizmoSpace() const { return m_space; }

private:
    InputSystem* m_input = nullptr;
    SelectionManager* m_selection = nullptr;
    Camera* m_camera = nullptr;
    World* m_world = nullptr;
    SceneRenderer* m_renderer = nullptr;
    GizmoMode m_mode = GizmoMode::Translate;
    GizmoSpace m_space = GizmoSpace::World;
};

} // namespace uz
