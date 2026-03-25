/// EditorInputHandler — Implementation of editor input routing.

#include "editor_input_handler.h"

#include "input.h"
#include "selection_manager.h"
#include "camera.h"
#include "world.h"
#include "scene_renderer.h"
#include "log.h"

UZ_DEFINE_LOG_CATEGORY(Editor, Trace);

namespace uz {

void EditorInputHandler::Init(InputSystem* input, SelectionManager* selection,
                               Camera* camera, World* world) {
    m_input = input;
    m_selection = selection;
    m_camera = camera;
    m_world = world;
}

void EditorInputHandler::Update(float /*dt*/) {
    if (!m_input) return;

    // ── Gizmo mode hotkeys ─────────────────────────────────────────────
    if (m_input->IsKeyJustPressed(Key::W)) {
        m_mode = GizmoMode::Translate;
        UZ_LOG(Editor, Trace, "Gizmo mode: Translate");
    }
    if (m_input->IsKeyJustPressed(Key::E)) {
        m_mode = GizmoMode::Rotate;
        UZ_LOG(Editor, Trace, "Gizmo mode: Rotate");
    }
    if (m_input->IsKeyJustPressed(Key::R)) {
        m_mode = GizmoMode::Scale;
        UZ_LOG(Editor, Trace, "Gizmo mode: Scale");
    }

    // ── Coordinate space toggle ────────────────────────────────────────
    if (m_input->IsKeyJustPressed(Key::Space)) {
        m_space = (m_space == GizmoSpace::World) ? GizmoSpace::Local : GizmoSpace::World;
        UZ_LOG(Editor, Trace, "Gizmo space: {}",
               m_space == GizmoSpace::World ? "World" : "Local");
    }

    // ── Left-click to select ───────────────────────────────────────────
    // Only pick when not dragging (right mouse button is used for camera)
    if (m_input->IsMouseJustPressed(MouseButton::Left) && !m_input->IsMouseDown(MouseButton::Right)) {
        if (m_selection && m_camera && m_world && m_renderer) {
            const float mx = m_input->GetMouseX();
            const float my = m_input->GetMouseY();
            const float vw = static_cast<float>(m_renderer->GetWidth());
            const float vh = static_cast<float>(m_renderer->GetHeight());
            m_selection->SelectAtScreenPos(mx, my, vw, vh, m_camera, m_world, m_renderer);
        }
    }
}

} // namespace uz
